//==============================================================================
// TwoMomentRad - a radiation transport library for patch-based AMR codes
// Copyright 2020 Benjamin Wibking.
// Released under the MIT license. See LICENSE file included in the GitHub repo.
//==============================================================================
/// \file test_advection.cpp
/// \brief Defines a test problem for linear advection.
///

#include "test_advection.hpp"
#include "AMReX_BoxArray.H"
#include "AMReX_DistributionMapping.H"
#include "AMReX_FArrayBox.H"
#include "AMReX_MultiFab.H"
#include "AdvectionSimulation.hpp"

auto main(int argc, char **argv) -> int
{
	// Initialization
	// (copied from ExaWind)

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

		result = testproblem_advection();

	} // destructors must be called before amrex::Finalize()
	amrex::Finalize();

	return result;
}

struct SawtoothProblem {
};

template <> void AdvectionSimulation<SawtoothProblem>::setInitialConditions()
{
	for (amrex::MFIter iter(state_old_); iter.isValid(); ++iter) {
		const amrex::Box &indexRange = iter.validbox(); // excludes ghost zones
		auto const &state = state_new_.array(iter);

		amrex::ParallelFor(indexRange, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
			auto x = (0.5 + static_cast<double>(i)) / nx_;
			auto value = static_cast<double>((i + nx_ / 2) % nx_) / nx_;
			state(i, j, k, 0) = value;
		});
	}

	// set flag
	areInitialConditionsDefined_ = true;
}

void ComputeExactSolution(amrex::Array4<amrex::Real> const &exact_arr, amrex::Box const &indexRange,
			  const int nx)
{
	amrex::ParallelFor(indexRange, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
		auto x = (0.5 + static_cast<double>(i)) / nx;
		auto value = static_cast<double>((i + nx / 2) % nx) / nx;
		exact_arr(i, j, k, 0) = value;
	});
}

auto testproblem_advection() -> int
{
	// Problem parameters
	const int nx = 400;
	const double Lx = 1.0;
	const double advection_velocity = 1.0;
	const double CFL_number = 0.3;
	const double max_time = 1.0;
	const double max_dt = 1e-4;
	const int max_timesteps = 1e4;
	const int nvars = 1; // only density

	// Problem initialization
	AdvectionSimulation<SawtoothProblem> sim;

	// set initial conditions
	sim.setInitialConditions();

	// run simulation
	sim.evolve();

	// Compute reference solution
	amrex::MultiFab state_exact(sim.simBoxArray_, sim.simDistributionMapping_, sim.ncomp_,
				    sim.nghost_);

	for (amrex::MFIter iter(sim.state_new_); iter.isValid(); ++iter) {
		const amrex::Box &indexRange = iter.validbox();
		auto const &stateExact = state_exact.array(iter);
		auto const &stateNew = sim.state_new_.const_array(iter);
		ComputeExactSolution(stateExact, indexRange, sim.nx_);
	}

	// Compute error norm
	const int this_comp = 0;
	const auto sol_norm = state_exact.norm1(this_comp);
	amrex::MultiFab residual(sim.simBoxArray_, sim.simDistributionMapping_, sim.ncomp_,
				 sim.nghost_);
	amrex::MultiFab::Copy(residual, state_exact, 0, 0, sim.ncomp_, sim.nghost_);
	amrex::MultiFab::Saxpy(residual, -1., sim.state_new_, this_comp, this_comp, sim.ncomp_,
			       sim.nghost_);
	const auto err_norm = residual.norm1(this_comp);
	const double rel_error = err_norm / sol_norm;
	amrex::Print() << "Relative L1 error norm = " << rel_error << std::endl;

	const double err_tol = 0.015;
	int status = 0;
	if (rel_error > err_tol) {
		status = 1;
	}

	// copy all FABs to a local FAB across the entire domain
	amrex::BoxArray localBoxes(sim.domain_);
	amrex::DistributionMapping localDistribution(localBoxes, 1);
	amrex::MultiFab state_final(localBoxes, localDistribution, sim.ncomp_, 0);
	amrex::MultiFab state_exact_local(localBoxes, localDistribution, sim.ncomp_, 0);
	state_final.ParallelCopy(sim.state_new_);
	state_exact_local.ParallelCopy(state_exact);
	auto const &state_final_array = state_final.array(0);
	auto const &state_exact_array = state_exact_local.array(0);

	std::vector<double> d_final(sim.nx_);
	std::vector<double> d_initial(sim.nx_);
	std::vector<double> x(sim.nx_);

	for (int i = 0; i < sim.nx_; ++i) {
		x.at(i) = (static_cast<double>(i) + 0.5) / sim.nx_;
		d_final.at(i) = state_final_array(i, 0, 0);
		d_initial.at(i) = state_exact_array(i, 0, 0);
	}

	// Plot results
	std::map<std::string, std::string> d_initial_args;
	std::map<std::string, std::string> d_final_args;
	d_initial_args["label"] = "density (initial)";
	d_final_args["label"] = "density (final)";

	matplotlibcpp::clf();
	matplotlibcpp::plot(x, d_initial, d_initial_args);
	matplotlibcpp::plot(x, d_final, d_final_args);
	matplotlibcpp::legend();
	matplotlibcpp::save(std::string("./advection.pdf"));

	return status;
}
