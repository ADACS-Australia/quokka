//==============================================================================
// TwoMomentRad - a radiation transport library for patch-based AMR codes
// Copyright 2020 Benjamin Wibking.
// Released under the MIT license. See LICENSE file included in the GitHub repo.
//==============================================================================
/// \file test_hydro_shocktube.cpp
/// \brief Defines a test problem for a shock tube.
///

#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

#include "AMReX_BC_TYPES.H"
#include "AMReX_BLassert.H"

#include "ArrayUtil.hpp"
#include "RadhydroSimulation.hpp"
#include "fextract.hpp"
#include "hydro_system.hpp"
#include "test_hydro_leblanc.hpp"
#ifdef HAVE_PYTHON
#include "matplotlibcpp.h"
#endif

struct ShocktubeProblem {};

template <> struct EOS_Traits<ShocktubeProblem> {
  static constexpr double gamma = (5. / 3.);
  static constexpr bool reconstruct_eint = true;
};

template <>
void RadhydroSimulation<ShocktubeProblem>::setInitialConditionsAtLevel(
    int lev) {
  amrex::GpuArray<amrex::Real, AMREX_SPACEDIM> dx = geom[lev].CellSizeArray();
  amrex::GpuArray<amrex::Real, AMREX_SPACEDIM> prob_lo =
      geom[lev].ProbLoArray();
  const int ncomp = ncomp_;

  for (amrex::MFIter iter(state_new_[lev]); iter.isValid(); ++iter) {
    const amrex::Box &indexRange = iter.validbox(); // excludes ghost zones
    auto const &state = state_new_[lev].array(iter);

    amrex::ParallelFor(indexRange, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
      amrex::Real const x = prob_lo[0] + (i + amrex::Real(0.5)) * dx[0];

      const double vx = 0.0;
      double rho = NAN;
      double P = NAN;

      if (x < 3.0) {
        rho = 1.0;
        P = (2. / 3.) * 1.0e-1;
      } else {
        rho = 1.0e-3;
        P = (2. / 3.) * 1.0e-10;
      }

      AMREX_ASSERT(!std::isnan(vx));
      AMREX_ASSERT(!std::isnan(rho));
      AMREX_ASSERT(!std::isnan(P));

      for (int n = 0; n < ncomp; ++n) {
        state(i, j, k, n) = 0.;
      }

      const auto gamma = HydroSystem<ShocktubeProblem>::gamma_;
      state(i, j, k, HydroSystem<ShocktubeProblem>::density_index) = rho;
      state(i, j, k, HydroSystem<ShocktubeProblem>::x1Momentum_index) =
          rho * vx;
      state(i, j, k, HydroSystem<ShocktubeProblem>::x2Momentum_index) = 0.;
      state(i, j, k, HydroSystem<ShocktubeProblem>::x3Momentum_index) = 0.;
      state(i, j, k, HydroSystem<ShocktubeProblem>::energy_index) =
          P / (gamma - 1.) + 0.5 * rho * (vx * vx);
    });
  }

  // set flag
  areInitialConditionsDefined_ = true;
}

template <>
AMREX_GPU_DEVICE AMREX_FORCE_INLINE void
AMRSimulation<ShocktubeProblem>::setCustomBoundaryConditions(
    const amrex::IntVect &iv, amrex::Array4<amrex::Real> const &consVar,
    int /*dcomp*/, int numcomp, amrex::GeometryData const &geom,
    const amrex::Real /*time*/, const amrex::BCRec * /*bcr*/, int /*bcomp*/,
    int /*orig_comp*/) {
#if (AMREX_SPACEDIM == 1)
  auto i = iv.toArray()[0];
  int j = 0;
  int k = 0;
#endif
#if (AMREX_SPACEDIM == 2)
  auto [i, j] = iv.toArray();
  int k = 0;
#endif
#if (AMREX_SPACEDIM == 3)
  auto [i, j, k] = iv.toArray();
#endif

  amrex::Box const &box = geom.Domain();
  amrex::GpuArray<int, 3> lo = box.loVect3d();
  amrex::GpuArray<int, 3> hi = box.hiVect3d();

  const double vx = 0.0;
  double rho = NAN;
  double P = NAN;

  if (i < lo[0]) {
    rho = 1.0;
    P = (2. / 3.) * 1.0e-1;
  } else if (i >= hi[0]) {
    rho = 1.0e-3;
    P = (2. / 3.) * 1.0e-10;
  }

  double E =
      P / (HydroSystem<ShocktubeProblem>::gamma_ - 1.) + 0.5 * rho * (vx * vx);

  for (int n = 0; n < numcomp; ++n) {
    consVar(i, j, k, n) = 0;
  }

  consVar(i, j, k, RadSystem<ShocktubeProblem>::gasDensity_index) = rho;
  consVar(i, j, k, RadSystem<ShocktubeProblem>::x1GasMomentum_index) = rho * vx;
  consVar(i, j, k, RadSystem<ShocktubeProblem>::x2GasMomentum_index) = 0.;
  consVar(i, j, k, RadSystem<ShocktubeProblem>::x3GasMomentum_index) = 0.;
  consVar(i, j, k, RadSystem<ShocktubeProblem>::gasEnergy_index) = E;
}

template <>
void RadhydroSimulation<ShocktubeProblem>::computeReferenceSolution(
    amrex::MultiFab &ref,
    amrex::GpuArray<amrex::Real, AMREX_SPACEDIM> const &dx,
    amrex::GpuArray<amrex::Real, AMREX_SPACEDIM> const &prob_lo) {

  // read in exact solution
  std::vector<double> xs_exact;
  std::vector<double> density_exact;
  std::vector<double> pressure_exact;
  std::vector<double> velocity_exact;
  std::vector<double> eint_exact;

  std::string filename = "../extern/ppm1d/leblanc.dat";
  std::ifstream fstream(filename, std::ios::in);
  AMREX_ALWAYS_ASSERT(fstream.is_open());
  std::string header;
  std::string blank_line;
  std::getline(fstream, header);
  std::getline(fstream, blank_line);

  for (std::string line; std::getline(fstream, line);) {
    std::istringstream iss(line);
    std::vector<double> values;

    for (double value = NAN; iss >> value;) {
      values.push_back(value);
    }
    auto x = values.at(1);
    auto density = values.at(2);
    auto pressure = values.at(3);
    auto velocity = values.at(4);
    auto eint =
        pressure / ((HydroSystem<ShocktubeProblem>::gamma_ - 1.0) * density);

    xs_exact.push_back(x);
    density_exact.push_back(density);
    pressure_exact.push_back(pressure);
    velocity_exact.push_back(velocity);
    eint_exact.push_back(eint);
  }

  // interpolate exact solution onto coarse grid
  auto const box = geom[0].Domain();
  int nx = (box.hiVect3d()[0] - box.loVect3d()[0]) + 1;
  std::vector<double> xs(nx);
  for (int i = 0; i < nx; ++i) {
    xs.at(i) = prob_lo[0] + (i + amrex::Real(0.5)) * dx[0];
  }

  std::vector<double> density_exact_interp(xs.size());
  std::vector<double> velocity_exact_interp(xs.size());
  std::vector<double> pressure_exact_interp(xs.size());
  std::vector<double> eint_exact_interp(xs.size());

  interpolate_arrays(xs.data(), density_exact_interp.data(),
                     static_cast<int>(xs.size()), xs_exact.data(),
                     density_exact.data(), static_cast<int>(xs_exact.size()));
  interpolate_arrays(xs.data(), velocity_exact_interp.data(),
                     static_cast<int>(xs.size()), xs_exact.data(),
                     velocity_exact.data(), static_cast<int>(xs_exact.size()));
  interpolate_arrays(xs.data(), pressure_exact_interp.data(),
                     static_cast<int>(xs.size()), xs_exact.data(),
                     pressure_exact.data(), static_cast<int>(xs_exact.size()));
  interpolate_arrays(xs.data(), eint_exact_interp.data(),
                     static_cast<int>(xs.size()), xs_exact.data(),
                     eint_exact.data(), static_cast<int>(xs_exact.size()));

  // fill reference solution multifab
  for (amrex::MFIter iter(ref); iter.isValid(); ++iter) {
    const amrex::Box &indexRange = iter.validbox();
    auto const &stateExact = ref.array(iter);
    auto const ncomp = ref.nComp();

    amrex::LoopConcurrentOnCpu(indexRange, [=](int i, int j, int k) noexcept {
      for (int n = 0; n < ncomp; ++n) {
        stateExact(i, j, k, n) = 0.;
      }
      amrex::Real rho = density_exact_interp.at(i);
      amrex::Real vx = velocity_exact_interp.at(i);
      amrex::Real P = pressure_exact_interp.at(i);

      const auto gamma = HydroSystem<ShocktubeProblem>::gamma_;
      stateExact(i, j, k, HydroSystem<ShocktubeProblem>::density_index) = rho;
      stateExact(i, j, k, HydroSystem<ShocktubeProblem>::x1Momentum_index) =
          rho * vx;
      stateExact(i, j, k, HydroSystem<ShocktubeProblem>::x2Momentum_index) = 0.;
      stateExact(i, j, k, HydroSystem<ShocktubeProblem>::x3Momentum_index) = 0.;
      stateExact(i, j, k, HydroSystem<ShocktubeProblem>::energy_index) =
          P / (gamma - 1.) + 0.5 * rho * (vx * vx);
    });
  }

#ifdef HAVE_PYTHON

  // Plot results
  auto [position, values] = fextract(state_new_[0], geom[0], 0, 0.5);
  auto [pos_exact, val_exact] = fextract(ref, geom[0], 0, 0.5);

  if (amrex::ParallelDescriptor::IOProcessor()) {
    // extract values
    std::vector<double> d(nx);
    std::vector<double> vx(nx);
    std::vector<double> P(nx);
    std::vector<double> eint(nx);

    for (int i = 0; i < nx; ++i) {
      amrex::Real rho =
          values.at(HydroSystem<ShocktubeProblem>::density_index).at(i);
      amrex::Real xmom =
          values.at(HydroSystem<ShocktubeProblem>::x1Momentum_index).at(i);
      amrex::Real Egas =
          values.at(HydroSystem<ShocktubeProblem>::energy_index).at(i);

      amrex::Real xvel = xmom / rho;
      amrex::Real Eint = Egas - xmom * xmom / (2.0 * rho);
      amrex::Real specific_Eint = Eint / rho;
      amrex::Real pressure =
          (HydroSystem<ShocktubeProblem>::gamma_ - 1.) * Eint;

      d.at(i) = rho;
      vx.at(i) = xvel;
      P.at(i) = pressure;
      eint.at(i) = specific_Eint;
    }

    // density, velocity, pressure plot
    matplotlibcpp::clf();
    double msize = 5.0;
    int s = 8; // stride

    std::map<std::string, std::string> d_args;
    std::unordered_map<std::string, std::string> dexact_args;
    d_args["label"] = "density";
    d_args["color"] = "C0";
    dexact_args["marker"] = "o";
    dexact_args["color"] = "C0";
    // dexact_args["edgecolors"] = "k";
    matplotlibcpp::plot(xs, d, d_args);
    matplotlibcpp::scatter(strided_vector_from(xs_exact, s),
                           strided_vector_from(density_exact, s), msize,
                           dexact_args);

    std::map<std::string, std::string> vx_args;
    vx_args["label"] = "velocity";
    vx_args["color"] = "C3";
    std::unordered_map<std::string, std::string> vexact_args;
    vexact_args["marker"] = "o";
    vexact_args["color"] = "C3";
    // vexact_args["edgecolors"] = "k";
    matplotlibcpp::plot(xs, vx, vx_args);
    matplotlibcpp::scatter(strided_vector_from(xs_exact, s),
                           strided_vector_from(velocity_exact, s), msize,
                           vexact_args);

    std::map<std::string, std::string> P_args;
    P_args["label"] = "pressure";
    P_args["color"] = "C4";
    std::unordered_map<std::string, std::string> Pexact_args;
    Pexact_args["color"] = "C4";
    // Pexact_args["edgecolors"] = "k";
    Pexact_args["marker"] = "o";
    matplotlibcpp::plot(xs, P, P_args);
    matplotlibcpp::scatter(strided_vector_from(xs_exact, s),
                           strided_vector_from(pressure_exact, s), msize,
                           Pexact_args);

    matplotlibcpp::legend();
    matplotlibcpp::xlabel("length x");
    // matplotlibcpp::title(fmt::format("t = {:.4f}", tNew_[0]));
    matplotlibcpp::tight_layout();
    matplotlibcpp::save(fmt::format("./hydro_leblanc_{:.4f}.pdf", tNew_[0]));

    // internal energy plot
    matplotlibcpp::clf();

    std::map<std::string, std::string> e_args;
    std::unordered_map<std::string, std::string> eexact_args;
    e_args["label"] = "specific internal energy";
    e_args["color"] = "C5";
    eexact_args["marker"] = "o";
    eexact_args["color"] = "C5";
    eexact_args["edgecolors"] = "k";
    matplotlibcpp::plot(xs, eint, e_args);
    matplotlibcpp::scatter(strided_vector_from(xs_exact, s),
                           strided_vector_from(eint_exact, s), msize,
                           eexact_args);

    matplotlibcpp::legend();
    // matplotlibcpp::title(fmt::format("t = {:.4f}", tNew_[0]));
    matplotlibcpp::xlabel("length x");
    matplotlibcpp::tight_layout();
    matplotlibcpp::save(
        fmt::format("./hydro_leblanc_eint_{:.4f}.pdf", tNew_[0]));
  }
#endif
}

auto problem_main() -> int {
  // Problem parameters

  // const int nx = 2000;
  // const double Lx = 9.0;
  const double CFL_number = 0.1;
  const double max_time = 6.0;
  const double max_dt = 1e-3;
  const double initial_dt = 1e-5;
  const int max_timesteps = 50000;

  // Problem initialization
  const int nvars = RadhydroSimulation<ShocktubeProblem>::nvarTotal_;
  amrex::Vector<amrex::BCRec> boundaryConditions(nvars);
  for (int n = 0; n < nvars; ++n) {
    boundaryConditions[0].setLo(0, amrex::BCType::foextrap); // Dirichlet
    boundaryConditions[0].setHi(0, amrex::BCType::foextrap);
    for (int i = 1; i < AMREX_SPACEDIM; ++i) {
      boundaryConditions[n].setLo(i, amrex::BCType::int_dir); // periodic
      boundaryConditions[n].setHi(i, amrex::BCType::int_dir);
    }
  }

  RadhydroSimulation<ShocktubeProblem> sim(boundaryConditions);
  sim.is_hydro_enabled_ = true;
  sim.is_radiation_enabled_ = false;
  sim.cflNumber_ = CFL_number;
  sim.maxDt_ = max_dt;
  sim.stopTime_ = max_time;
  sim.maxTimesteps_ = max_timesteps;
  sim.initDt_ = initial_dt;
  sim.computeReferenceSolution_ = true;
  sim.plotfileInterval_ = -1;

  // Main time loop
  sim.setInitialConditions();
  sim.evolve();

  // Compute test success condition
  int status = 0;
  const double error_tol = 0.002;
  if (sim.errorNorm_ > error_tol) {
    status = 1;
  }

  return status;
}
