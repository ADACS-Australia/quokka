//==============================================================================
// TwoMomentRad - a radiation transport library for patch-based AMR codes
// Copyright 2020 Benjamin Wibking.
// Released under the MIT license. See LICENSE file included in the GitHub repo.
//==============================================================================
/// \file cloud.cpp
/// \brief Implements a shock-cloud problem with radiative cooling.
///

#include <random>
#include <variant>
#include <vector>

#include "AMReX.H"
#include "AMReX_BC_TYPES.H"
#include "AMReX_BLProfiler.H"
#include "AMReX_BLassert.H"
#include "AMReX_FabArray.H"
#include "AMReX_Geometry.H"
#include "AMReX_GpuContainers.H"
#include "AMReX_GpuDevice.H"
#include "AMReX_GpuQualifiers.H"
#include "AMReX_IntVect.H"
#include "AMReX_MFParallelFor.H"
#include "AMReX_MultiFab.H"
#include "AMReX_ParallelContext.H"
#include "AMReX_ParallelDescriptor.H"
#include "AMReX_REAL.H"
#include "AMReX_SPACE.H"
#include "AMReX_TableData.H"
#include "AMReX_iMultiFab.H"

#include "CloudyCooling.hpp"
#include "ODEIntegrate.hpp"
#include "RadhydroSimulation.hpp"
#include "hydro_system.hpp"
#include "radiation_system.hpp"

#include "cloud.hpp"

using amrex::Real;
using namespace amrex::literals;

struct ShockCloud {
}; // dummy type to allow compile-type polymorphism via template specialization

constexpr double m_H = hydrogen_mass_cgs_;
constexpr double seconds_in_year = 3.154e7;

template <> struct EOS_Traits<ShockCloud> {
  static constexpr double gamma = 5. / 3.; // default value
  // if true, reconstruct e_int instead of pressure
  static constexpr bool reconstruct_eint = true;
};

constexpr Real Tgas0 = 1.0e7;            // K
constexpr Real nH0 = 1.0e-4;             // cm^-3
constexpr Real nH1 = 1.0e-2;             // cm^-3
constexpr Real R_cloud = 5.0 * 3.086e18; // cm [5 pc]
constexpr Real M0 = 2.0;                 // Mach number of shock

constexpr Real T_floor = 100.0;                            // K
constexpr Real P0 = nH0 * Tgas0 * boltzmann_constant_cgs_; // erg cm^-3
constexpr Real rho0 = nH0 * m_H;                           // g cm^-3
constexpr Real rho1 = nH1 * m_H;

// needed for Dirichlet boundary condition
AMREX_GPU_MANAGED static Real delta_vx = 0;

template <>
void RadhydroSimulation<ShockCloud>::setInitialConditionsAtLevel(int lev) {
  amrex::GpuArray<Real, AMREX_SPACEDIM> dx = geom[lev].CellSizeArray();
  amrex::GpuArray<Real, AMREX_SPACEDIM> prob_lo = geom[lev].ProbLoArray();
  amrex::GpuArray<Real, AMREX_SPACEDIM> prob_hi = geom[lev].ProbHiArray();

  Real const Lx = (prob_hi[0] - prob_lo[0]);
  Real const Ly = (prob_hi[1] - prob_lo[1]);
  Real const Lz = (prob_hi[2] - prob_lo[2]);

  Real const x0 = prob_lo[0] + 0.2 * (prob_hi[0] - prob_lo[0]);
  Real const y0 = prob_lo[1] + 0.5 * (prob_hi[1] - prob_lo[1]);
  Real const z0 = prob_lo[2] + 0.5 * (prob_hi[2] - prob_lo[2]);

  // perturbation parameters
  const int kmin = 0;
  const int kmax = 16;
  Real const A = 0.05 / kmax;

  // generate random phases
  amrex::Array<int, AMREX_SPACEDIM> tlo{AMREX_D_DECL(kmin, kmin, kmin)};
  amrex::Array<int, AMREX_SPACEDIM> thi{AMREX_D_DECL(kmax, kmax, kmax)};
  amrex::TableData<Real, AMREX_SPACEDIM> table_data(tlo, thi);
#ifdef AMREX_USE_GPU
  amrex::TableData<Real, AMREX_SPACEDIM> h_table_data(
      tlo, thi, amrex::The_Pinned_Arena());
  auto const &h_table = h_table_data.table();
#else
  auto const &h_table = table_data.table();
#endif

  // Initialize data on the host

  // 64-bit Mersenne Twister (do not use 32-bit version for sampling doubles!)
  std::mt19937_64 rng(1); // NOLINT
  std::uniform_real_distribution<double> sample_phase(0., 2.0 * M_PI);

  for (int j = tlo[0]; j <= thi[0]; ++j) {
    for (int i = tlo[1]; i <= thi[1]; ++i) {
      for (int k = tlo[2]; k <= thi[2]; ++k) {
        h_table(i, j, k) = sample_phase(rng);
      }
    }
  }

#ifdef AMREX_USE_GPU
  // Copy data to GPU memory
  table_data.copy(h_table_data);
  amrex::Gpu::streamSynchronize();
#endif

  auto const &phase = table_data.const_table(); // const makes it read only

  // cooling tables
  auto tables = cloudyTables.const_tables();

  for (amrex::MFIter iter(state_old_[lev]); iter.isValid(); ++iter) {
    const amrex::Box &indexRange = iter.validbox();
    auto const &state = state_new_[lev].array(iter);

    amrex::ParallelFor(indexRange, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
      Real const x = prob_lo[0] + (i + Real(0.5)) * dx[0];
      Real const y = prob_lo[1] + (j + Real(0.5)) * dx[1];
      Real const z = prob_lo[2] + (k + Real(0.5)) * dx[2];
      Real const R = std::sqrt(std::pow(x - x0, 2) + std::pow(y - y0, 2) +
                               std::pow(z - z0, 2));

      // compute perturbations
      Real delta_rho = 0;
      for (int ki = kmin; ki < kmax; ++ki) {
        for (int kj = kmin; kj < kmax; ++kj) {
          for (int kk = kmin; kk < kmax; ++kk) {
            if ((ki == 0) && (kj == 0) && (kk == 0)) {
              continue;
            }
            Real const kx = 2.0 * M_PI * Real(ki) / Lx;
            Real const ky = 2.0 * M_PI * Real(kj) / Lx;
            Real const kz = 2.0 * M_PI * Real(kk) / Lx;
            delta_rho +=
                A * std::sin(x * kx + y * ky + z * kz + phase(ki, kj, kk));
          }
        }
      }
      AMREX_ALWAYS_ASSERT(delta_rho > -1.0);

      Real rho = rho0 * (1.0 + delta_rho); // background density
      Real C = 0.0; // concentration is zero on the background
      if (R < R_cloud) {
        rho = rho1 * (1.0 + delta_rho); // cloud density
        C = 1.0; // concentration is unity inside the cloud
      }
      Real const xmom = 0;
      Real const ymom = 0;
      Real const zmom = 0;
      Real const Eint = (HydroSystem<ShockCloud>::gamma_ - 1.) * P0;
      Real const Egas = RadSystem<ShockCloud>::ComputeEgasFromEint(
          rho, xmom, ymom, zmom, Eint);

      state(i, j, k, RadSystem<ShockCloud>::gasEnergy_index) = Egas;
      state(i, j, k, RadSystem<ShockCloud>::gasDensity_index) = rho;
      state(i, j, k, RadSystem<ShockCloud>::x1GasMomentum_index) = xmom;
      state(i, j, k, RadSystem<ShockCloud>::x2GasMomentum_index) = ymom;
      state(i, j, k, RadSystem<ShockCloud>::x3GasMomentum_index) = zmom;
      state(i, j, k, RadSystem<ShockCloud>::passiveScalar_index) = C;

      state(i, j, k, RadSystem<ShockCloud>::radEnergy_index) = 0;
      state(i, j, k, RadSystem<ShockCloud>::x1RadFlux_index) = 0;
      state(i, j, k, RadSystem<ShockCloud>::x2RadFlux_index) = 0;
      state(i, j, k, RadSystem<ShockCloud>::x3RadFlux_index) = 0;
    });
  }

  // set flag
  areInitialConditionsDefined_ = true;
}

template <>
AMREX_GPU_DEVICE AMREX_FORCE_INLINE void
AMRSimulation<ShockCloud>::setCustomBoundaryConditions(
    const amrex::IntVect &iv, amrex::Array4<Real> const &consVar, int /*dcomp*/,
    int /*numcomp*/, amrex::GeometryData const &geom, const Real /*time*/,
    const amrex::BCRec * /*bcr*/, int /*bcomp*/, int /*orig_comp*/) {
  auto [i, j, k] = iv.toArray();

  amrex::Box const &box = geom.Domain();
  const auto &domain_lo = box.loVect();
  const int ilo = domain_lo[0];

  const Real delta_vx = ::delta_vx;

  if (i < ilo) {
    // x1 lower boundary -- constant
    // compute downstream shock conditions from rho0, P0, and M0
    constexpr Real gamma = HydroSystem<ShockCloud>::gamma_;
    constexpr Real rho_shock =
        rho0 * (gamma + 1.) * M0 * M0 / ((gamma - 1.) * M0 * M0 + 2.);
    constexpr Real P_shock =
        P0 * (2. * gamma * M0 * M0 - (gamma - 1.)) / (gamma + 1.);
    Real const v_shock = M0 * std::sqrt(gamma * P_shock / rho_shock);

    Real const rho = rho_shock;
    Real const xmom = rho_shock * (v_shock - delta_vx);
    Real const ymom = 0;
    Real const zmom = 0;
    Real const Eint = (gamma - 1.) * P_shock;
    Real const Egas =
        RadSystem<ShockCloud>::ComputeEgasFromEint(rho, xmom, ymom, zmom, Eint);

    consVar(i, j, k, RadSystem<ShockCloud>::gasDensity_index) = rho;
    consVar(i, j, k, RadSystem<ShockCloud>::x1GasMomentum_index) = xmom;
    consVar(i, j, k, RadSystem<ShockCloud>::x2GasMomentum_index) = ymom;
    consVar(i, j, k, RadSystem<ShockCloud>::x3GasMomentum_index) = zmom;
    consVar(i, j, k, RadSystem<ShockCloud>::gasEnergy_index) = Egas;
    consVar(i, j, k, RadSystem<ShockCloud>::passiveScalar_index) = 0;

    consVar(i, j, k, RadSystem<ShockCloud>::radEnergy_index) = 0;
    consVar(i, j, k, RadSystem<ShockCloud>::x1RadFlux_index) = 0;
    consVar(i, j, k, RadSystem<ShockCloud>::x2RadFlux_index) = 0;
    consVar(i, j, k, RadSystem<ShockCloud>::x3RadFlux_index) = 0;
  }
}

struct ODEUserData {
  Real rho;
  cloudyGpuConstTables tables;
};

AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE auto
user_rhs(Real /*t*/, quokka::valarray<Real, 1> &y_data,
         quokka::valarray<Real, 1> &y_rhs, void *user_data) -> int {
  // unpack user_data
  auto *udata = static_cast<ODEUserData *>(user_data);
  const Real rho = udata->rho;
  const Real gamma = HydroSystem<ShockCloud>::gamma_;
  cloudyGpuConstTables &tables = udata->tables;

  // check whether temperature is out-of-bounds
  const Real Tmin = 10.;
  const Real Tmax = 1.0e9;
  const Real Eint_min = ComputeEgasFromTgas(rho, Tmin, gamma, tables);
  const Real Eint_max = ComputeEgasFromTgas(rho, Tmax, gamma, tables);

  // compute temperature and cooling rate
  const Real Eint = y_data[0];

  if (Eint <= Eint_min) {
    // set cooling to value at Tmin
    y_rhs[0] = cloudy_cooling_function(rho, Tmin, tables);
  } else if (Eint >= Eint_max) {
    // set cooling to value at Tmax
    y_rhs[0] = cloudy_cooling_function(rho, Tmax, tables);
  } else {
    // ok, within tabulated cooling limits
    const Real T = ComputeTgasFromEgas(rho, Eint, gamma, tables);
    if (!std::isnan(T)) { // temp iteration succeeded
      y_rhs[0] = cloudy_cooling_function(rho, T, tables);
    } else { // temp iteration failed
      y_rhs[0] = NAN;
      return 1; // failed
    }
  }

  return 0; // success
}

void computeCooling(amrex::MultiFab &mf, const Real dt_in,
                    cloudy_tables &cloudyTables) {
  BL_PROFILE("RadhydroSimulation::computeCooling()")

  const Real dt = dt_in;
  const Real reltol_floor = 0.01;
  const Real rtol = 1.0e-4; // not recommended to change this

  auto tables = cloudyTables.const_tables();

  const auto &ba = mf.boxArray();
  const auto &dmap = mf.DistributionMap();
  amrex::iMultiFab nsubstepsMF(ba, dmap, 1, 0);

  for (amrex::MFIter iter(mf); iter.isValid(); ++iter) {
    const amrex::Box &indexRange = iter.validbox();
    auto const &state = mf.array(iter);
    auto const &nsubsteps = nsubstepsMF.array(iter);

    amrex::ParallelFor(indexRange, [=] AMREX_GPU_DEVICE(int i, int j,
                                                        int k) noexcept {
      const Real rho = state(i, j, k, HydroSystem<ShockCloud>::density_index);
      const Real x1Mom =
          state(i, j, k, HydroSystem<ShockCloud>::x1Momentum_index);
      const Real x2Mom =
          state(i, j, k, HydroSystem<ShockCloud>::x2Momentum_index);
      const Real x3Mom =
          state(i, j, k, HydroSystem<ShockCloud>::x3Momentum_index);
      const Real Egas = state(i, j, k, HydroSystem<ShockCloud>::energy_index);

      Real Eint = RadSystem<ShockCloud>::ComputeEintFromEgas(rho, x1Mom, x2Mom,
                                                             x3Mom, Egas);

      ODEUserData user_data{rho, tables};
      quokka::valarray<Real, 1> y = {Eint};
      quokka::valarray<Real, 1> abstol = {
          reltol_floor * ComputeEgasFromTgas(rho, T_floor,
                                             HydroSystem<ShockCloud>::gamma_,
                                             tables)};

      // do integration with RK2 (Heun's method)
      int nsteps = 0;
      rk_adaptive_integrate(user_rhs, 0, y, dt, &user_data, rtol, abstol,
                            nsteps);
      nsubsteps(i, j, k) = nsteps;

      // check if integration failed
      if (nsteps >= maxStepsODEIntegrate) {
        Real T = ComputeTgasFromEgas(rho, Eint, HydroSystem<ShockCloud>::gamma_,
                                     tables);
        Real Edot = cloudy_cooling_function(rho, T, tables);
        Real t_cool = Eint / Edot;
        printf(
            "max substeps exceeded! rho = %.17e, Eint = %.17e, T = %g, cooling "
            "time = %g, dt = %.17e\n",
            rho, Eint, T, t_cool, dt);
      }

      const Real Egas_new = RadSystem<ShockCloud>::ComputeEgasFromEint(
          rho, x1Mom, x2Mom, x3Mom, y[0]);

      state(i, j, k, HydroSystem<ShockCloud>::energy_index) = Egas_new;
    });
  }

  int nmin = nsubstepsMF.min(0);
  int nmax = nsubstepsMF.max(0);
  Real navg = static_cast<Real>(nsubstepsMF.sum(0)) /
              static_cast<Real>(nsubstepsMF.boxArray().numPts());

  if (nmax >= maxStepsODEIntegrate) {
    amrex::Print() << fmt::format(
        "\tcooling substeps (per cell): min {}, avg {}, max {}\n", nmin, navg,
        nmax);
    amrex::Abort("Max steps exceeded in cooling solve!");
  }
}

template <>
void RadhydroSimulation<ShockCloud>::computeAfterLevelAdvance(
    int lev, Real /*time*/, Real dt_lev, int /*iteration*/, int /*ncycle*/) {
  // compute operator split physics
  computeCooling(state_new_[lev], dt_lev, cloudyTables);
}

template <>
void RadhydroSimulation<ShockCloud>::computeAfterTimestep(
    const amrex::Real dt_coarse) {
  // perform Galilean transformation (velocity shift to center-of-mass frame)

  // N.B. must weight by passive scalar of cloud, since the background has
  // non-negligible momentum!
  int nc = 1; // number of components in temporary MF
  int ng = 0; // number of ghost cells in temporary MF
  amrex::MultiFab temp_mf(boxArray(0), DistributionMap(0), nc, ng);

  // compute x-momentum
  amrex::MultiFab::Copy(temp_mf, state_new_[0],
                        HydroSystem<ShockCloud>::x1Momentum_index, 0, nc, ng);
  amrex::MultiFab::Multiply(temp_mf, state_new_[0],
                            HydroSystem<ShockCloud>::scalar_index, 0, nc, ng);
  const Real xmom = temp_mf.sum(0);

  // compute cloud mass within simulation box
  amrex::MultiFab::Copy(temp_mf, state_new_[0],
                        HydroSystem<ShockCloud>::density_index, 0, nc, ng);
  amrex::MultiFab::Multiply(temp_mf, state_new_[0],
                            HydroSystem<ShockCloud>::scalar_index, 0, nc, ng);
  const Real cloud_mass = temp_mf.sum(0);

  // compute center-of-mass velocity of the cloud
  const Real vx_cm = xmom / cloud_mass;

  // save cumulative position, velocity offsets in simulationMetadata_
  const Real delta_x_prev = std::get<Real>(simulationMetadata_["delta_x"]);
  const Real delta_vx_prev = std::get<Real>(simulationMetadata_["delta_vx"]);
  const Real delta_x = delta_x_prev + dt_coarse * delta_vx_prev;
  const Real delta_vx = delta_vx_prev + vx_cm;
  simulationMetadata_["delta_x"] = delta_x;
  simulationMetadata_["delta_vx"] = delta_vx;
  ::delta_vx = delta_vx;

  amrex::Print() << "\tDelta x = " << (delta_x / 3.086e18) << " pc,"
                 << " Delta vx = " << (delta_vx / 1.0e5) << " km/s\n";

  // subtract center-of-mass y-velocity on each level
  // N.B. must update both y-momentum *and* energy!
  for (int lev = 0; lev <= finest_level; ++lev) {
    auto const &mf = state_new_[lev];
    auto const &state = state_new_[lev].arrays();
    amrex::ParallelFor(mf, [=] AMREX_GPU_DEVICE(int box, int i, int j,
                                                int k) noexcept {
      Real rho = state[box](i, j, k, HydroSystem<ShockCloud>::density_index);
      Real xmom =
          state[box](i, j, k, HydroSystem<ShockCloud>::x1Momentum_index);
      Real ymom =
          state[box](i, j, k, HydroSystem<ShockCloud>::x2Momentum_index);
      Real zmom =
          state[box](i, j, k, HydroSystem<ShockCloud>::x3Momentum_index);
      Real E = state[box](i, j, k, HydroSystem<ShockCloud>::energy_index);
      Real KE = 0.5 * (xmom * xmom + ymom * ymom + zmom * zmom) / rho;
      Real Eint = E - KE;
      Real new_xmom = xmom - rho * vx_cm;
      Real new_KE =
          0.5 * (new_xmom * new_xmom + ymom * ymom + zmom * zmom) / rho;

      state[box](i, j, k, HydroSystem<ShockCloud>::x1Momentum_index) = new_xmom;
      state[box](i, j, k, HydroSystem<ShockCloud>::energy_index) =
          Eint + new_KE;
    });
  }
  amrex::Gpu::streamSynchronize();
}

template <>
void RadhydroSimulation<ShockCloud>::ComputeDerivedVar(
    int lev, std::string const &dname, amrex::MultiFab &mf,
    const int ncomp_in) const {
  // compute derived variables and save in 'mf'
  if (dname == "temperature") {
    const int ncomp = ncomp_in;
    auto tables = cloudyTables.const_tables();

    for (amrex::MFIter iter(mf); iter.isValid(); ++iter) {
      const amrex::Box &indexRange = iter.validbox();
      auto const &output = mf.array(iter);
      auto const &state = state_new_[lev].const_array(iter);

      amrex::ParallelFor(indexRange, [=] AMREX_GPU_DEVICE(int i, int j,
                                                          int k) noexcept {
        Real rho = state(i, j, k, HydroSystem<ShockCloud>::density_index);
        Real x1Mom = state(i, j, k, HydroSystem<ShockCloud>::x1Momentum_index);
        Real x2Mom = state(i, j, k, HydroSystem<ShockCloud>::x2Momentum_index);
        Real x3Mom = state(i, j, k, HydroSystem<ShockCloud>::x3Momentum_index);
        Real Egas = state(i, j, k, HydroSystem<ShockCloud>::energy_index);
        Real Eint = RadSystem<ShockCloud>::ComputeEintFromEgas(
            rho, x1Mom, x2Mom, x3Mom, Egas);
        Real Tgas = ComputeTgasFromEgas(
            rho, Eint, HydroSystem<ShockCloud>::gamma_, tables);

        output(i, j, k, ncomp) = Tgas;
      });
    }
  }
}

template <>
void HydroSystem<ShockCloud>::EnforcePressureFloor(
    Real const densityFloor, Real const /*pressureFloor*/,
    amrex::Box const &indexRange, amrex::Array4<Real> const &state) {
  // prevent vacuum creation
  Real const rho_floor = densityFloor; // workaround nvcc bug

  amrex::ParallelFor(indexRange, [=] AMREX_GPU_DEVICE(int i, int j,
                                                      int k) noexcept {
    Real const rho = state(i, j, k, density_index);
    Real const vx1 = state(i, j, k, x1Momentum_index) / rho;
    Real const vx2 = state(i, j, k, x2Momentum_index) / rho;
    Real const vx3 = state(i, j, k, x3Momentum_index) / rho;
    Real const vsq = (vx1 * vx1 + vx2 * vx2 + vx3 * vx3);
    Real const Etot = state(i, j, k, energy_index);

    Real rho_new = rho;
    if (rho < rho_floor) {
      rho_new = rho_floor;
      state(i, j, k, density_index) = rho_new;
    }

    Real const P_floor = (rho_new / m_H) * boltzmann_constant_cgs_ * T_floor;

    // recompute gas energy (to prevent P < 0)
    Real const Eint_star = Etot - 0.5 * rho_new * vsq;
    Real const P_star = Eint_star * (gamma_ - 1.);
    Real P_new = P_star;
    if (P_star < P_floor) {
      P_new = P_floor;
      Real const Etot_new = P_new / (gamma_ - 1.) + 0.5 * rho_new * vsq;
      state(i, j, k, energy_index) = Etot_new;
    }
  });
}

template <>
void RadhydroSimulation<ShockCloud>::ErrorEst(int lev, amrex::TagBoxArray &tags,
                                              Real /*time*/, int /*ngrow*/) {
  // tag cells for refinement
  const Real eta_threshold = 0.1; // gradient refinement threshold
  const Real C_min = 1.0e-5;      // minimum concentration for refinement

  for (amrex::MFIter mfi(state_new_[lev]); mfi.isValid(); ++mfi) {
    const amrex::Box &box = mfi.validbox();
    const auto state = state_new_[lev].const_array(mfi);
    const auto tag = tags.array(mfi);
    const int nidx = HydroSystem<ShockCloud>::density_index;

    amrex::ParallelFor(box, [=] AMREX_GPU_DEVICE(int i, int j, int k) noexcept {
      Real const q = state(i, j, k, nidx);
      Real const C = state(i, j, k, HydroSystem<ShockCloud>::scalar_index);

      Real const q_xplus = state(i + 1, j, k, nidx);
      Real const q_xminus = state(i - 1, j, k, nidx);
      Real const q_yplus = state(i, j + 1, k, nidx);
      Real const q_yminus = state(i, j - 1, k, nidx);
      Real const q_zplus = state(i, j, k + 1, nidx);
      Real const q_zminus = state(i, j, k - 1, nidx);

      Real const del_x =
          std::max(std::abs(q_xplus - q), std::abs(q - q_xminus));
      Real const del_y =
          std::max(std::abs(q_yplus - q), std::abs(q - q_yminus));
      Real const del_z =
          std::max(std::abs(q_zplus - q), std::abs(q - q_zminus));

      Real const gradient_indicator = std::max({del_x, del_y, del_z}) / q;

      if ((gradient_indicator > eta_threshold) && (C > C_min)) {
        tag(i, j, k) = amrex::TagBox::SET;
      }
    });
  }
}

auto problem_main() -> int {
  // Problem parameters
  const double CFL_number = 0.25;
  const double max_time = 10.0e6 * seconds_in_year; // 10 Myr
  const int max_timesteps = 1e5;

  // Problem initialization
  constexpr int nvars = RadhydroSimulation<ShockCloud>::nvarTotal_;
  amrex::Vector<amrex::BCRec> boundaryConditions(nvars);
  for (int n = 0; n < nvars; ++n) {
    boundaryConditions[n].setLo(0, amrex::BCType::ext_dir);  // Dirichlet
    boundaryConditions[n].setHi(0, amrex::BCType::foextrap); // extrapolate

    boundaryConditions[n].setLo(1, amrex::BCType::int_dir); // periodic
    boundaryConditions[n].setHi(1, amrex::BCType::int_dir);

    boundaryConditions[n].setLo(2, amrex::BCType::int_dir);
    boundaryConditions[n].setHi(2, amrex::BCType::int_dir);
  }

  RadhydroSimulation<ShockCloud> sim(boundaryConditions);
  sim.is_hydro_enabled_ = true;
  sim.is_radiation_enabled_ = false;

  // Standard PPM gives unphysically enormous temperatures when used for
  // this problem (e.g., ~1e14 K or higher), but can be fixed by
  // reconstructing the temperature instead of the pressure
  sim.reconstructionOrder_ = 3;      // PLM
  sim.densityFloor_ = 1.0e-2 * rho0; // density floor (to prevent vacuum)

  sim.cflNumber_ = CFL_number;
  sim.maxTimesteps_ = max_timesteps;
  sim.stopTime_ = max_time;
  sim.plotfileInterval_ = 100;
  sim.checkpointInterval_ = 2000;

  // set metadata
  sim.simulationMetadata_["delta_x"] = 0._rt;
  sim.simulationMetadata_["delta_vx"] = 0._rt;

  // Read Cloudy tables
  readCloudyData(sim.cloudyTables);

  // Set initial conditions
  sim.setInitialConditions();

  // run simulation
  sim.evolve();

  // Cleanup and exit
  int status = 0;
  return status;
}
