//==============================================================================
// TwoMomentRad - a radiation transport library for patch-based AMR codes
// Copyright 2020 Benjamin Wibking.
// Released under the MIT license. See LICENSE file included in the GitHub repo.
//==============================================================================
/// \file test_advection.cpp
/// \brief Defines a test problem for linear advection.
///

#include "AMReX_Algorithm.H"
#include "AMReX_Array.H"
#include "AMReX_BC_TYPES.H"
#include "AMReX_BoxArray.H"
#include "AMReX_Config.H"
#include "AMReX_CoordSys.H"
#include "AMReX_DistributionMapping.H"
#include "AMReX_FArrayBox.H"
#include "AMReX_MultiFab.H"
#include "AMReX_ParallelDescriptor.H"
#include "AMReX_REAL.H"

#include "AdvectionSimulation.hpp"
#include "fextract.hpp"
#include "test_advection.hpp"

struct SawtoothProblem {
};

AMREX_GPU_DEVICE void ComputeExactSolution(
    int i, int j, int k, int n, amrex::Array4<amrex::Real> const &exact_arr,
    amrex::GpuArray<amrex::Real, AMREX_SPACEDIM> const &dx,
    amrex::GpuArray<amrex::Real, AMREX_SPACEDIM> const &prob_lo,
	amrex::GpuArray<amrex::Real, AMREX_SPACEDIM> const &prob_hi)
{
	// compute exact solution
	amrex::Real const x_length = prob_hi[0] - prob_lo[0];
	amrex::Real const x = prob_lo[0] + (i + amrex::Real(0.5)) * dx[0];
	auto value = std::fmod(x + 0.5*x_length, x_length);
	exact_arr(i, j, k, n) = value;
}

template <> void AdvectionSimulation<SawtoothProblem>::setInitialConditionsAtLevel(int level)
{
	auto const &prob_lo = geom[level].ProbLoArray();
	auto const &prob_hi = geom[level].ProbHiArray();
	auto const &dx = geom[level].CellSizeArray();

	for (amrex::MFIter iter(state_old_[level]); iter.isValid(); ++iter) {
		const amrex::Box &indexRange = iter.validbox(); // excludes ghost zones
		auto const &state = state_new_[level].array(iter);
		amrex::ParallelFor(indexRange, ncomp_,
				   [=] AMREX_GPU_DEVICE(int i, int j, int k, int n) {
						ComputeExactSolution(i, j, k, n, state, dx, prob_lo, prob_hi);
				   });
	}

	// set flag
	areInitialConditionsDefined_ = true;
}


template <>
void AdvectionSimulation<SawtoothProblem>::computeReferenceSolution(
    amrex::MultiFab &ref,
    amrex::GpuArray<amrex::Real, AMREX_SPACEDIM> const &dx,
    amrex::GpuArray<amrex::Real, AMREX_SPACEDIM> const &prob_lo,
	amrex::GpuArray<amrex::Real, AMREX_SPACEDIM> const &prob_hi) {

  // fill reference solution multifab
  for (amrex::MFIter iter(ref); iter.isValid(); ++iter) {
    const amrex::Box &indexRange = iter.validbox();
    auto const &stateExact = ref.array(iter);
    auto const ncomp = ref.nComp();

    amrex::ParallelFor(indexRange, ncomp,
		[=] AMREX_GPU_DEVICE(int i, int j, int k, int n) noexcept {
          ComputeExactSolution(i, j, k, n, stateExact, dx, prob_lo, prob_hi);
        });
  }

#ifdef HAVE_PYTHON
  // Plot results
  auto [position, values] = fextract(state_new_[0], geom[0], 0, 0.5);
  auto [pos_exact, val_exact] = fextract(ref, geom[0], 0, 0.5);

  // interpolate exact solution onto coarse grid
  int nx = static_cast<int>(position.size());
  std::vector<double> xs(nx);
  for (int i = 0; i < nx; ++i) {
    xs.at(i) = prob_lo[0] + (i + amrex::Real(0.5)) * dx[0];
  }

  if (amrex::ParallelDescriptor::IOProcessor()) {
    // extract values
    std::vector<double> d(nx);
    std::vector<double> d_exact(nx);
    for (int i = 0; i < nx; ++i) {
      amrex::Real rho = values.at(0).at(i);
      amrex::Real rho_exact = val_exact.at(0).at(i);
      d.at(i) = rho;
      d_exact.at(i) = rho_exact;
    }

	// Plot results
	std::map<std::string, std::string> d_initial_args;
	std::map<std::string, std::string> d_final_args;
	d_initial_args["label"] = "density (initial)";
	d_final_args["label"] = "density (final)";

	matplotlibcpp::plot(xs, d_exact, d_initial_args);
	matplotlibcpp::plot(xs, d, d_final_args);
	matplotlibcpp::legend();
	matplotlibcpp::save(std::string("./advection_sawtooth.pdf"));
  }
#endif
}


auto problem_main() -> int
{
	// Problem parameters
	//const int nx = 400;
	//const double Lx = 1.0;
	const double advection_velocity = 1.0;
	const double CFL_number = 0.4;
	const double max_time = 1.0;
	const double max_dt = 1.0e-4;
	const int max_timesteps = 1e4;
	const int nvars = 1; // only density

	amrex::Vector<amrex::BCRec> boundaryConditions(nvars);
	for (int n = 0; n < nvars; ++n) {
		for (int i = 0; i < AMREX_SPACEDIM; ++i) {
			boundaryConditions[n].setLo(i, amrex::BCType::int_dir); // periodic
			boundaryConditions[n].setHi(i, amrex::BCType::int_dir);
		}
	}

	// Problem initialization
	AdvectionSimulation<SawtoothProblem> sim(boundaryConditions);
	sim.maxDt_ = max_dt;
	sim.stopTime_ = max_time;
	sim.cflNumber_ = CFL_number;
	sim.maxTimesteps_ = max_timesteps;
	sim.plotfileInterval_ = -1;
	sim.advectionVx_ = 1.0;
	sim.advectionVy_ = advection_velocity;

	// set initial conditions
	sim.setInitialConditions();

	// run simulation
	sim.evolve();

	int status = 0;
	const double err_tol = 0.015;
	if (sim.errorNorm_ > err_tol) {
		status = 1;
	}
	return status;
}
