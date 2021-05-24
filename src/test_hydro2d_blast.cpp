//==============================================================================
// TwoMomentRad - a radiation transport library for patch-based AMR codes
// Copyright 2020 Benjamin Wibking.
// Released under the MIT license. See LICENSE file included in the GitHub repo.
//==============================================================================
/// \file test_hydro_shocktube.cpp
/// \brief Defines a test problem for a shock tube.
///

#include "test_hydro2d_blast.hpp"
#include "AMReX_BLassert.H"
#include "AMReX_Config.H"
#include "AMReX_ParallelDescriptor.H"
#include "AMReX_ParmParse.H"
#include "AMReX_Print.H"
#include "HydroSimulation.hpp"
#include "hydro_system.hpp"

auto main(int argc, char **argv) -> int
{
	// Initialization (copied from ExaWind)

	amrex::Initialize(argc, argv, true, MPI_COMM_WORLD, []() {
		amrex::ParmParse pp("amrex");
		// Set the defaults so that we throw an exception instead of attempting
		// to generate backtrace files. However, if the user has explicitly set
		// these options in their input files respect those settings.
		if (!pp.contains("throw_exception")) {
			pp.add("throw_exception", 1);
		}
		if (!pp.contains("signal_handling")) {
			pp.add("signal_handling", 0);
		}
	});

	int result = 0;

	{ // objects must be destroyed before amrex::finalize, so enter new
	  // scope here to do that automatically

		result = testproblem_hydro_blast();

	} // destructors must be called before amrex::Finalize()
	amrex::Finalize();

	return result;
}

struct BlastProblem {
};

template <> struct EOS_Traits<BlastProblem> {
	static constexpr double gamma = 5. / 3.;
};

template <> void HydroSimulation<BlastProblem>::setInitialConditions()
{
	amrex::GpuArray<Real, AMREX_SPACEDIM> dx = simGeometry_.CellSizeArray();
	amrex::GpuArray<Real, AMREX_SPACEDIM> prob_lo = simGeometry_.ProbLoArray();
	amrex::GpuArray<Real, AMREX_SPACEDIM> prob_hi = simGeometry_.ProbHiArray();

	amrex::Real const x0 = prob_lo[0] + 0.5 * (prob_hi[0] - prob_lo[0]);
	amrex::Real const y0 = prob_lo[1] + 0.5 * (prob_hi[1] - prob_lo[1]);

	for (amrex::MFIter iter(state_old_); iter.isValid(); ++iter) {
		const amrex::Box &indexRange = iter.validbox(); // excludes ghost zones
		auto const &state = state_new_.array(iter);

		amrex::ParallelFor(indexRange, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
			amrex::Real const x = prob_lo[0] + (i + Real(0.5)) * dx[0];
			amrex::Real const y = prob_lo[1] + (j + Real(0.5)) * dx[1];
			amrex::Real const R = std::sqrt(std::pow(x - x0, 2) + std::pow(y - y0, 2));

			double vx = 0.;
			double vy = 0.;
			double vz = 0.;
			double rho = 1.0;
			double P = NAN;

			if (R < 0.1) { // inside circle
				P = 10.;
			} else {
				P = 0.1;
			}

			AMREX_ASSERT(!std::isnan(vx));
			AMREX_ASSERT(!std::isnan(vy));
			AMREX_ASSERT(!std::isnan(vz));
			AMREX_ASSERT(!std::isnan(rho));
			AMREX_ASSERT(!std::isnan(P));

			const auto v_sq = vx * vx + vy * vy + vz * vz;
			const auto gamma = HydroSystem<BlastProblem>::gamma_;

			state(i, j, k, HydroSystem<BlastProblem>::density_index) = rho;
			state(i, j, k, HydroSystem<BlastProblem>::x1Momentum_index) = rho * vx;
			state(i, j, k, HydroSystem<BlastProblem>::x2Momentum_index) = rho * vy;
			state(i, j, k, HydroSystem<BlastProblem>::x3Momentum_index) = rho * vz;
			state(i, j, k, HydroSystem<BlastProblem>::energy_index) =
			    P / (gamma - 1.) + 0.5 * rho * v_sq;
		});
	}

	// set flag
	areInitialConditionsDefined_ = true;
}

auto testproblem_hydro_blast() -> int
{
	// Problem parameters
	// const int nx = 100;

	// Problem initialization
	HydroSimulation<BlastProblem> sim;
	sim.stopTime_ = 1.5;
	sim.cflNumber_ = 0.4;
	sim.maxTimesteps_ = 5000;
	sim.outputAtInterval_ = false;

	// initialize
	sim.setInitialConditions();

	// evolve
	sim.evolve();

	// Cleanup and exit
	amrex::Print() << "Finished." << std::endl;
	return 0;
}