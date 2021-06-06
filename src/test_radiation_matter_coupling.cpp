//==============================================================================
// TwoMomentRad - a radiation transport library for patch-based AMR codes
// Copyright 2020 Benjamin Wibking.
// Released under the MIT license. See LICENSE file included in the GitHub repo.
//==============================================================================
/// \file test_radiation_matter_coupling.cpp
/// \brief Defines a test problem for radiation-matter coupling.
///

#include "test_radiation_matter_coupling.hpp"
#include "AMReX_BC_TYPES.H"
#include "RadiationSimulation.hpp"
#include "radiation_system.hpp"
#include <vector>

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

		result = testproblem_radiation_matter_coupling();

	} // destructors must be called before amrex::Finalize()
	amrex::Finalize();

	return result;
}

struct CouplingProblem {
}; // dummy type to allow compile-type polymorphism via template specialization

// Su & Olson (1997) test problem
constexpr double eps_SuOlson = 1.0;
constexpr double a_rad = 7.5646e-15; // cgs
constexpr double alpha_SuOlson = 4.0 * a_rad / eps_SuOlson;

template <> struct RadSystem_Traits<CouplingProblem> {
	static constexpr double c_light = c_light_cgs_;
	static constexpr double c_hat = c_light_cgs_;
	static constexpr double radiation_constant = radiation_constant_cgs_;
	static constexpr double mean_molecular_mass = hydrogen_mass_cgs_;
	static constexpr double boltzmann_constant = boltzmann_constant_cgs_;
	static constexpr double gamma = 5. / 3.;
	static constexpr double Erad_floor = 0.;
	static constexpr bool do_marshak_left_boundary = false;
	static constexpr double T_marshak_left = 0.;
};

template <>
constexpr auto RadSystem<CouplingProblem>::ComputeTgasFromEgas(const double rho, const double Egas) -> double
{
	return std::pow(4.0 * Egas / alpha_SuOlson, 1. / 4.);
}

template <>
constexpr auto RadSystem<CouplingProblem>::ComputeEgasFromTgas(const double rho, const double Tgas) -> double
{
	return (alpha_SuOlson / 4.0) * std::pow(Tgas, 4);
}

template <>
constexpr auto RadSystem<CouplingProblem>::ComputeEgasTempDerivative(const double rho, const double Tgas)
    -> double
{
	// This is also known as the heat capacity, i.e.
	// 		\del E_g / \del T = \rho c_v,
	// for normal materials.

	// However, for this problem, this must be of the form \alpha T^3
	// in order to obtain an exact solution to the problem.
	// The input parameter is the *temperature*, not Egas itself.

	return alpha_SuOlson * std::pow(Tgas, 3);
}

constexpr double Erad = 1.0e12; // erg cm^-3
constexpr double Egas = 1.0e2;	// erg cm^-3
constexpr double rho = 1.0e-7;	// g cm^-3

template <> void RadiationSimulation<CouplingProblem>::setInitialConditions()
{
	for (amrex::MFIter iter(state_old_); iter.isValid(); ++iter) {
		const amrex::Box &indexRange = iter.validbox(); // excludes ghost zones
		auto const &state = state_new_.array(iter);

		amrex::ParallelFor(indexRange, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
			state(i, j, k, RadSystem<CouplingProblem>::radEnergy_index) = Erad;
			state(i, j, k, RadSystem<CouplingProblem>::x1RadFlux_index) = 0;
			state(i, j, k, RadSystem<CouplingProblem>::x2RadFlux_index) = 0;
			state(i, j, k, RadSystem<CouplingProblem>::x3RadFlux_index) = 0;

			state(i, j, k, RadSystem<CouplingProblem>::gasEnergy_index) = Egas;
			state(i, j, k, RadSystem<CouplingProblem>::gasDensity_index) = rho;
			state(i, j, k, RadSystem<CouplingProblem>::x1GasMomentum_index) = 0.;
			state(i, j, k, RadSystem<CouplingProblem>::x2GasMomentum_index) = 0.;
			state(i, j, k, RadSystem<CouplingProblem>::x3GasMomentum_index) = 0.;
		});
	}

	// set flag
	areInitialConditionsDefined_ = true;
}

template <> void RadiationSimulation<CouplingProblem>::computeAfterTimestep()
{
	if (amrex::ParallelDescriptor::IOProcessor()) {
		// copy all FABs to a local FAB across the entire domain
		amrex::BoxArray localBoxes(domain_);
		amrex::DistributionMapping localDistribution(localBoxes, 1);
		amrex::MultiFab state_final(localBoxes, localDistribution, ncomp_, 0);
		amrex::MultiFab state_exact_local(localBoxes, localDistribution, ncomp_, 0);
		state_final.ParallelCopy(state_new_);
		auto const &state_final_array = state_final.array(0);

		t_vec_.push_back(tNow_);
		const amrex::Real Erad_i =
		    state_final_array(0, 0, 0, RadSystem<CouplingProblem>::radEnergy_index);
		const amrex::Real Egas_i =
		    state_final_array(0, 0, 0, RadSystem<CouplingProblem>::gasEnergy_index);
		Trad_vec_.push_back(std::pow(Erad_i / a_rad, 1. / 4.));
		Tgas_vec_.push_back(RadSystem<CouplingProblem>::ComputeTgasFromEgas(rho, Egas_i));
	}
}

auto testproblem_radiation_matter_coupling() -> int
{
	// Problem parameters

	const int nx = 4;
	const double Lx = 1e5; // cm
	const double CFL_number = 1.0;
	const double max_time = 1.0e-2; // s
	const int max_timesteps = 1e6;
	// const double constant_dt = 1.0e-8; // s

	// Problem initialization
	amrex::IntVect gridDims{AMREX_D_DECL(nx, 4, 4)};
	amrex::RealBox boxSize{
	    {AMREX_D_DECL(amrex::Real(0.0), amrex::Real(0.0), amrex::Real(0.0))},	// NOLINT
	    {AMREX_D_DECL(amrex::Real(Lx), amrex::Real(1.0), amrex::Real(1.0))}}; // NOLINT

	constexpr int nvars = 9;
	amrex::Vector<amrex::BCRec> boundaryConditions(nvars);
	for (int n = 0; n < nvars; ++n) {
		for (int i = 0; i < AMREX_SPACEDIM; ++i) {
			boundaryConditions[n].setLo(i, amrex::BCType::foextrap); // extrapolate
			boundaryConditions[n].setHi(i, amrex::BCType::foextrap);
		}
	}

	RadiationSimulation<CouplingProblem> sim(gridDims, boxSize, boundaryConditions, nvars);
	sim.stopTime_ = max_time;
	sim.cflNumber_ = CFL_number;
	sim.maxTimesteps_ = max_timesteps;
	sim.outputAtInterval_ = false;
	sim.plotfileInterval_ = 100; // for debugging

	// initialize
	sim.setInitialConditions();

	// evolve
	sim.evolve();

	// copy solution slice to vector
	int status = 0;

	if (amrex::ParallelDescriptor::IOProcessor()) {
		// Solve for asymptotically-exact solution (Gonzalez et al. 2007)
		const int nmax = sim.t_vec_.size();
		std::vector<double> t_exact(nmax);
		std::vector<double> Tgas_exact(nmax);
		const double initial_Tgas =
		    RadSystem<CouplingProblem>::ComputeTgasFromEgas(rho, Egas);
		const auto kappa = RadSystem<CouplingProblem>::ComputeOpacity(rho, initial_Tgas);

		for (int n = 0; n < nmax; ++n) {
			const double time_t = sim.t_vec_.at(n);
			const double arad = RadSystem<CouplingProblem>::radiation_constant_;
			const double c = RadSystem<CouplingProblem>::c_light_;
			const double E0 = (Erad + Egas) / (arad + alpha_SuOlson / 4.0);
			const double T0_4 = std::pow(initial_Tgas, 4);

			const double T4 = (T0_4 - E0) * std::exp(-(4. / alpha_SuOlson) *
								 (arad + alpha_SuOlson / 4.0) *
								 kappa * rho * c * time_t) +
					  E0;

			const double T_gas = std::pow(T4, 1. / 4.);

			t_exact.at(n) = (time_t);
			Tgas_exact.at(n) = (T_gas);
		}

		// interpolate exact solution onto output timesteps
		std::vector<double> Tgas_exact_interp(sim.t_vec_.size());
		interpolate_arrays(sim.t_vec_.data(), Tgas_exact_interp.data(), sim.t_vec_.size(),
				   t_exact.data(), Tgas_exact.data(), t_exact.size());

		// compute L2 error norm
		double err_norm = 0.;
		double sol_norm = 0.;
		for (int i = 0; i < sim.t_vec_.size(); ++i) {
			err_norm += std::abs(sim.Tgas_vec_[i] - Tgas_exact_interp[i]);
			sol_norm += std::abs(Tgas_exact_interp[i]);
		}
		const double rel_error = err_norm / sol_norm;
		const double error_tol = 2e-5;
		amrex::Print() << "relative L1 error norm = " << rel_error << std::endl;
		if (rel_error > error_tol) {
			status = 1;
		}

		// Plot results
		std::vector<double> &Tgas = sim.Tgas_vec_;
		std::vector<double> &Trad = sim.Trad_vec_;
		std::vector<double> &t = sim.t_vec_;

		matplotlibcpp::clf();
		matplotlibcpp::yscale("log");
		matplotlibcpp::xscale("log");
		matplotlibcpp::ylim(0.1 * std::min(Tgas.front(), Trad.front()),
				    10.0 * std::max(Trad.back(), Tgas.back()));

		std::map<std::string, std::string> Trad_args;
		Trad_args["label"] = "radiation temperature (numerical)";
		matplotlibcpp::plot(t, Trad, Trad_args);

		std::map<std::string, std::string> Tgas_args;
		Tgas_args["label"] = "gas temperature (numerical)";
		matplotlibcpp::plot(t, Tgas, Tgas_args);

		std::map<std::string, std::string> exactsol_args;
		exactsol_args["label"] = "gas temperature (exact)";
		exactsol_args["linestyle"] = "--";
		exactsol_args["color"] = "black";
		matplotlibcpp::plot(t, Tgas_exact_interp, exactsol_args);

		matplotlibcpp::legend();
		matplotlibcpp::xlabel("time t (s)");
		matplotlibcpp::ylabel("temperature T (K)");
		// matplotlibcpp::title(
		//    fmt::format("dt = {:.4g}\nt = {:.4g}", constant_dt, sim.tNow_));
		matplotlibcpp::save(fmt::format("./radcoupling.pdf"));

		matplotlibcpp::clf();

		std::vector<double> frac_err(t.size());
		for (int i = 0; i < t.size(); ++i) {
			frac_err.at(i) = Tgas_exact_interp.at(i) / Tgas.at(i) - 1.0;
		}
		matplotlibcpp::plot(t, frac_err);
		matplotlibcpp::xlabel("time t (s)");
		matplotlibcpp::ylabel("fractional error in material temperature");
		matplotlibcpp::save(fmt::format("./radcoupling_fractional_error.pdf"));
	}

	// Cleanup and exit
	amrex::Print() << "Finished." << std::endl;
	return status;
}
