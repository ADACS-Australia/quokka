#ifndef RADIATION_SYSTEM_HPP_ // NOLINT
#define RADIATION_SYSTEM_HPP_
//==============================================================================
// TwoMomentRad - a radiation transport library for patch-based AMR codes
// Copyright 2020 Benjamin Wibking.
// Released under the MIT license. See LICENSE file included in the GitHub repo.
//==============================================================================
/// \file radiation_system.hpp
/// \brief Defines a class for solving the (1d) radiation moment equations.
///

// c++ headers
#include <array>
#include <cmath>
#include <vector>

// library headers

// internal headers
#include "AMReX_Array.H"
#include "AMReX_BLassert.H"
#include "AMReX_GpuQualifiers.H"
#include "AMReX_REAL.H"
#include "ArrayView.hpp"
#include "hyperbolic_system.hpp"
#include "simulation.hpp"
#include "valarray.hpp"

// physical constants in CGS units
static constexpr double c_light_cgs_ = 2.99792458e10;		// cgs
static constexpr double radiation_constant_cgs_ = 7.5646e-15;	// cgs
static constexpr double hydrogen_mass_cgs_ = 1.6726231e-24;	// cgs
static constexpr double boltzmann_constant_cgs_ = 1.380658e-16; // cgs

// this struct is specialized by the user application code
//
template <typename problem_t> struct RadSystem_Traits {
	static constexpr double c_light = c_light_cgs_;
	static constexpr double c_hat = c_light_cgs_;
	static constexpr double radiation_constant = radiation_constant_cgs_;
	static constexpr double mean_molecular_mass = hydrogen_mass_cgs_;
	static constexpr double boltzmann_constant = boltzmann_constant_cgs_;
	static constexpr double gamma = 5. / 3.;
	static constexpr double Erad_floor = 0.;
};

/// Class for the radiation moment equations
///
template <typename problem_t> class RadSystem : public HyperbolicSystem<problem_t>
{
      public:
	enum consVarIndex {
		gasDensity_index = 0,
		x1GasMomentum_index = 1,
		x2GasMomentum_index = 2,
		x3GasMomentum_index = 3,
		gasEnergy_index = 4,
		radEnergy_index = 5,
		x1RadFlux_index = 6,
		x2RadFlux_index = 7,
		x3RadFlux_index = 8,
	};

	static constexpr int nvar_ = 9;
	static constexpr int nvarHyperbolic_ = 4;
	static constexpr int nstartHyperbolic_ = radEnergy_index;

	enum primVarIndex {
		primRadEnergy_index = 0,
		x1ReducedFlux_index = 1,
		x2ReducedFlux_index = 2,
		x3ReducedFlux_index = 3,
	};

	// C++ standard does not allow constexpr to be uninitialized, even in a templated class!
	static constexpr double c_light_ = RadSystem_Traits<problem_t>::c_light;
	static constexpr double c_hat_ = RadSystem_Traits<problem_t>::c_hat;
	static constexpr double radiation_constant_ =
	    RadSystem_Traits<problem_t>::radiation_constant;
	static constexpr double mean_molecular_mass_ =
	    RadSystem_Traits<problem_t>::mean_molecular_mass;
	static constexpr double boltzmann_constant_ =
	    RadSystem_Traits<problem_t>::boltzmann_constant;
	static constexpr double gamma_ = RadSystem_Traits<problem_t>::gamma;

	static constexpr double Erad_floor_ = RadSystem_Traits<problem_t>::Erad_floor;

	// static functions

	static void ComputeMaxSignalSpeed(amrex::Array4<const amrex::Real> const &cons,
					  array_t &maxSignal, amrex::Box const &indexRange);
	static void ConservedToPrimitive(amrex::Array4<const amrex::Real> const &cons,
					 array_t &primVar, amrex::Box const &indexRange);

	template <int nvars>
	static void PredictStep(arrayconst_t &consVarOld, array_t &consVarNew,
				amrex::GpuArray<arrayconst_t, AMREX_SPACEDIM> fluxArray,
				amrex::GpuArray<arrayconst_t, AMREX_SPACEDIM> fluxDiffusiveArray,
				double dt_in, amrex::GpuArray<amrex::Real, AMREX_SPACEDIM> dx_in,
				amrex::Box const &indexRange, int nstart);

	static void AddFluxesRK2(array_t &U_new, arrayconst_t &U0, arrayconst_t &U1,
				 amrex::GpuArray<arrayconst_t, AMREX_SPACEDIM> fluxArray,
				 amrex::GpuArray<arrayconst_t, AMREX_SPACEDIM> fluxDiffusiveArray,
				 double dt_in, amrex::GpuArray<amrex::Real, AMREX_SPACEDIM> dx_in,
				 amrex::Box const &indexRange, int nvars, int nstart);

	template <FluxDir DIR>
	static void ComputeFluxes(array_t &x1Flux_in, array_t &x1FluxDiffusive_in,
				  amrex::Array4<const amrex::Real> const &x1LeftState_in,
				  amrex::Array4<const amrex::Real> const &x1RightState_in,
				  amrex::Box const &indexRange, arrayconst_t &consVar_in,
				  amrex::GpuArray<amrex::Real, AMREX_SPACEDIM> dx);

	static void SetRadEnergySource(array_t &radEnergy, amrex::Box const &indexRange,
				       amrex::GpuArray<amrex::Real, AMREX_SPACEDIM> dx,
				       amrex::Real time);

	static void AddSourceTerms(array_t &consVar, arrayconst_t &radEnergySource,
				   arrayconst_t &advectionFluxes, amrex::Box const &indexRange,
				   amrex::Real dt);
	static void ComputeSourceTermsExplicit(arrayconst_t &consPrev,
					       arrayconst_t &radEnergySource, array_t &src,
					       amrex::Box const &indexRange, amrex::Real dt);

	AMREX_GPU_HOST_DEVICE static auto ComputeEddingtonFactor(double f) -> double;
	AMREX_GPU_HOST_DEVICE static auto ComputeOpacity(double rho, double Tgas) -> double;
	AMREX_GPU_HOST_DEVICE static auto ComputeOpacityTempDerivative(double rho, double Tgas)
	    -> double;
	AMREX_GPU_HOST_DEVICE static auto ComputeTgasFromEgas(double rho, double Egas) -> double;
	AMREX_GPU_HOST_DEVICE static auto ComputeEgasFromTgas(double rho, double Tgas) -> double;
	AMREX_GPU_HOST_DEVICE static auto ComputeEgasTempDerivative(double rho, double Tgas)
	    -> double;
	AMREX_GPU_HOST_DEVICE static auto ComputeEintFromEgas(double density, double X1GasMom,
							      double X2GasMom, double X3GasMom,
							      double Etot) -> double;
	AMREX_GPU_HOST_DEVICE static auto ComputeEgasFromEint(double density, double X1GasMom,
							      double X2GasMom, double X3GasMom,
							      double Eint) -> double;

	template <FluxDir DIR>
	AMREX_GPU_DEVICE static auto
	ComputeCellOpticalDepth(const quokka::Array4View<const amrex::Real, DIR> &consVar,
				amrex::GpuArray<amrex::Real, AMREX_SPACEDIM> dx, int i, int j,
				int k) -> double;

	AMREX_GPU_DEVICE static auto isStateValid(std::array<amrex::Real, nvarHyperbolic_> &cons)
	    -> bool;

	// requires GPU reductions
	// auto CheckStatesValid(array_t &cons, std::pair<int, int> range) -> bool;
};

template <typename problem_t>
void RadSystem<problem_t>::SetRadEnergySource(array_t &radEnergySource,
					      amrex::Box const &indexRange,
					      amrex::GpuArray<amrex::Real, AMREX_SPACEDIM> dx,
					      amrex::Real time)
{
	// do nothing -- user implemented
}

template <typename problem_t>
void RadSystem<problem_t>::ConservedToPrimitive(amrex::Array4<const amrex::Real> const &cons,
						array_t &primVar, amrex::Box const &indexRange)
{
	// keep radiation energy density as-is
	// convert (Fx,Fy,Fz) into reduced flux components (fx,fy,fx):
	//   F_x -> F_x / (c*E_r)

	// cell-centered kernel
	amrex::ParallelFor(indexRange, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
		const auto E_r = cons(i, j, k, radEnergy_index);
		const auto Fx = cons(i, j, k, x1RadFlux_index);
		const auto Fy = cons(i, j, k, x2RadFlux_index);
		const auto Fz = cons(i, j, k, x3RadFlux_index);
		const auto reducedFluxX1 = Fx / (c_light_ * E_r);
		const auto reducedFluxX2 = Fy / (c_light_ * E_r);
		const auto reducedFluxX3 = Fz / (c_light_ * E_r);

		// check admissibility of states
		AMREX_ASSERT(E_r > 0.0); // NOLINT

		primVar(i, j, k, primRadEnergy_index) = E_r;
		primVar(i, j, k, x1ReducedFlux_index) = reducedFluxX1;
		primVar(i, j, k, x2ReducedFlux_index) = reducedFluxX2;
		primVar(i, j, k, x3ReducedFlux_index) = reducedFluxX3;
	});
}

template <typename problem_t>
void RadSystem<problem_t>::ComputeMaxSignalSpeed(amrex::Array4<const amrex::Real> const & /*cons*/,
						 array_t &maxSignal, amrex::Box const &indexRange)
{
	// cell-centered kernel
	amrex::ParallelFor(indexRange, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
		const double signal_max = c_hat_;
		maxSignal(i, j, k) = signal_max;
	});
}

template <typename problem_t>
AMREX_GPU_DEVICE auto
RadSystem<problem_t>::isStateValid(std::array<amrex::Real, nvarHyperbolic_> &cons) -> bool
{
	// check if the state variable 'cons' is a valid state
	const auto E_r = cons[radEnergy_index];
	const auto Fx = cons[x1RadFlux_index];
	const auto Fy = cons[x2RadFlux_index];
	const auto Fz = cons[x3RadFlux_index];

	const auto Fnorm = std::sqrt(Fx * Fx + Fy * Fy + Fz * Fz);
	const auto f = Fnorm / (c_light_ * E_r);

	bool isNonNegative = (E_r > 0.);
	bool isFluxCausal = (f <= 1.);
	return (isNonNegative && isFluxCausal);
}

template <typename problem_t>
template <int nvars>
void RadSystem<problem_t>::PredictStep(
    arrayconst_t &consVarOld, array_t &consVarNew,
    amrex::GpuArray<arrayconst_t, AMREX_SPACEDIM> fluxArray,
    amrex::GpuArray<arrayconst_t, AMREX_SPACEDIM> fluxDiffusiveArray, const double dt_in,
    amrex::GpuArray<amrex::Real, AMREX_SPACEDIM> dx_in, amrex::Box const &indexRange,
    const int nstart)
{
	// By convention, the fluxes are defined on the left edge of each zone,
	// i.e. flux_(i) is the flux *into* zone i through the interface on the
	// left of zone i, and -1.0*flux(i+1) is the flux *into* zone i through
	// the interface on the right of zone i.

	const auto dt = dt_in;

	const auto dx = dx_in[0];
	const auto x1Flux = fluxArray[0];
	const auto x1FluxDiffusive = fluxDiffusiveArray[0];
#if (AMREX_SPACEDIM >= 2)
	const auto dy = dx_in[1];
	const auto x2Flux = fluxArray[1];
	const auto x2FluxDiffusive = fluxDiffusiveArray[1];
#endif

	amrex::ParallelFor(indexRange, [=] AMREX_GPU_DEVICE(int i, int j, int k) noexcept {
		std::array<amrex::Real, nvars> cons{}; // this *may* work on GPU

		for (int n = 0; n < nvars; ++n) {
			cons[n] =
			    consVarOld(i, j, k, nstart+n) +
			    ((dt / dx) * (x1Flux(i, j, k, n) - x1Flux(i + 1, j, k, n))
#if (AMREX_SPACEDIM >= 2)
			     + (dt / dy) * (x2Flux(i, j, k, n) - x2Flux(i, j + 1, k, n))
#endif
			    );
		}

		if (!isStateValid(cons)) {
			// use diffusive fluxes instead
			for (int n = 0; n < nvars; ++n) {
				cons[n] = consVarOld(i, j, k, nstart+n) +
						   ((dt / dx) * (x1FluxDiffusive(i, j, k, n) -
								 x1FluxDiffusive(i + 1, j, k, n))
#if (AMREX_SPACEDIM >= 2)
						    + (dt / dy) * (x2FluxDiffusive(i, j, k, n) -
								   x2FluxDiffusive(i, j + 1, k, n))
#endif
						   );
			}
		}

		for (int n = 0; n < nvars; ++n) {
			consVarNew(i, j, k, nstart+n) = cons[n];
		}
	});
}

template <typename problem_t>
void RadSystem<problem_t>::AddFluxesRK2(
    array_t &U_new, arrayconst_t &U0, arrayconst_t &U1,
    amrex::GpuArray<arrayconst_t, AMREX_SPACEDIM> fluxArray,
    amrex::GpuArray<arrayconst_t, AMREX_SPACEDIM> fluxDiffusiveArray, const double dt_in,
    amrex::GpuArray<amrex::Real, AMREX_SPACEDIM> dx_in, amrex::Box const &indexRange,
    const int nvars, const int nstart)
{
	// By convention, the fluxes are defined on the left edge of each zone,
	// i.e. flux_(i) is the flux *into* zone i through the interface on the
	// left of zone i, and -1.0*flux(i+1) is the flux *into* zone i through
	// the interface on the right of zone i.

	const auto dt = dt_in;

	const auto dx = dx_in[0];
	const auto x1Flux = fluxArray[0];
	const auto x1FluxDiffusive = fluxDiffusiveArray[0];
#if (AMREX_SPACEDIM >= 2)
	const auto dy = dx_in[1];
	const auto x2Flux = fluxArray[1];
	const auto x2FluxDiffusive = fluxDiffusiveArray[1];
#endif

	amrex::ParallelFor(indexRange, [=] AMREX_GPU_DEVICE(int i, int j, int k) noexcept {
		for (int n = 0; n < nvars; ++n) {
			// RK-SSP2 integrator
			const double U_0 = U0(i, j, k, nstart+n);
			const double U_1 = U1(i, j, k, nstart+n);

			const double FxU_1 =
			    (dt / dx) * (x1Flux(i, j, k, n) - x1Flux(i + 1, j, k, n));
#if (AMREX_SPACEDIM >= 2)
			const double FyU_1 =
			    (dt / dy) * (x2Flux(i, j, k, n) - x2Flux(i, j + 1, k, n));
#endif

			// save results in U_new
			U_new(i, j, k, nstart+n) = (0.5 * U_0 + 0.5 * U_1) + (0.5 * FxU_1
#if (AMREX_SPACEDIM >= 2)
								       + 0.5 * FyU_1
#endif
								      );
		}
	});
}

template <typename problem_t>
AMREX_GPU_DEVICE auto RadSystem<problem_t>::ComputeEddingtonFactor(double f_in) -> double
{
	// f is the reduced flux == |F|/cE.
	// compute Levermore (1984) closure [Eq. 25]
	// the is the M1 closure that is derived from Lorentz invariance
	const double f = clamp(f_in, 0., 1.); // restrict f to be within [0, 1]
	const double f_fac = std::sqrt(4.0 - 3.0 * (f * f));
	const double chi = (3.0 + 4.0 * (f * f)) / (5.0 + 2.0 * f_fac);

#if 0
	// compute Minerbo (1978) closure [piecewise approximation]
	// (For unknown reasons, this closure tends to work better
	// than the Levermore/Lorentz closure on the Su & Olson 1997 test.)
	const double chi = (f < 1. / 3.) ? (1. / 3.) : (0.5 - f + 1.5 * f*f);
#endif

	return chi;
}

template <typename problem_t>
template <FluxDir DIR>
AMREX_GPU_DEVICE auto RadSystem<problem_t>::ComputeCellOpticalDepth(
    const quokka::Array4View<const amrex::Real, DIR> &consVar,
    amrex::GpuArray<amrex::Real, AMREX_SPACEDIM> dx, int i, int j, int k) -> double
{
	// compute interface-averaged cell optical depth

	// [By convention, the interfaces are defined on the left edge of each
	// zone, i.e. xleft_(i) is the "left"-side of the interface at
	// the left edge of zone i, and xright_(i) is the "right"-side of the
	// interface at the *left* edge of zone i.]

	// piecewise-constant reconstruction
	const double rho_L = consVar(i - 1, j, k, gasDensity_index);
	const double rho_R = consVar(i, j, k, gasDensity_index);

	const double x1GasMom_L = consVar(i - 1, j, k, x1GasMomentum_index);
	const double x1GasMom_R = consVar(i, j, k, x1GasMomentum_index);

	const double x2GasMom_L = consVar(i - 1, j, k, x2GasMomentum_index);
	const double x2GasMom_R = consVar(i, j, k, x2GasMomentum_index);

	const double x3GasMom_L = consVar(i - 1, j, k, x3GasMomentum_index);
	const double x3GasMom_R = consVar(i, j, k, x3GasMomentum_index);

	const double Egas_L = consVar(i - 1, j, k, gasEnergy_index);
	const double Egas_R = consVar(i, j, k, gasEnergy_index);

	const double Eint_L = RadSystem<problem_t>::ComputeEintFromEgas(
	    rho_L, x1GasMom_L, x2GasMom_L, x3GasMom_L, Egas_L);
	const double Eint_R = RadSystem<problem_t>::ComputeEintFromEgas(
	    rho_R, x1GasMom_R, x2GasMom_R, x3GasMom_R, Egas_R);

	const double Tgas_L = RadSystem<problem_t>::ComputeTgasFromEgas(rho_L, Eint_L);
	const double Tgas_R = RadSystem<problem_t>::ComputeTgasFromEgas(rho_R, Eint_R);

	double dl = NAN;
	if constexpr (DIR == FluxDir::X1) {
		dl = dx[0];
	} else if constexpr (DIR == FluxDir::X2) {
		dl = dx[1];
	} else if constexpr (DIR == FluxDir::X3) {
		dl = dx[2];
	}
	const double tau_L = dl * rho_L * RadSystem<problem_t>::ComputeOpacity(rho_L, Tgas_L);
	const double tau_R = dl * rho_R * RadSystem<problem_t>::ComputeOpacity(rho_R, Tgas_R);

	return (2.0 * tau_L * tau_R) / (tau_L + tau_R); // harmonic mean
	// return 0.5*(tau_L + tau_R); // arithmetic mean
}

template <typename problem_t>
template <FluxDir DIR>
void RadSystem<problem_t>::ComputeFluxes(array_t &x1Flux_in, array_t &x1FluxDiffusive_in,
					 amrex::Array4<const amrex::Real> const &x1LeftState_in,
					 amrex::Array4<const amrex::Real> const &x1RightState_in,
					 amrex::Box const &indexRange, arrayconst_t &consVar_in,
					 amrex::GpuArray<amrex::Real, AMREX_SPACEDIM> dx)
{
	quokka::Array4View<const amrex::Real, DIR> x1LeftState(x1LeftState_in);
	quokka::Array4View<const amrex::Real, DIR> x1RightState(x1RightState_in);
	quokka::Array4View<amrex::Real, DIR> x1Flux(x1Flux_in);
	quokka::Array4View<amrex::Real, DIR> x1FluxDiffusive(x1FluxDiffusive_in);
	quokka::Array4View<const amrex::Real, DIR> consVar(consVar_in);

	// By convention, the interfaces are defined on the left edge of each
	// zone, i.e. xinterface_(i) is the solution to the Riemann problem at
	// the left edge of zone i.

	// Indexing note: There are (nx + 1) interfaces for nx zones.

	// interface-centered kernel
	amrex::ParallelFor(indexRange, [=] AMREX_GPU_DEVICE(int i_in, int j_in, int k_in) {
		auto [i, j, k] = quokka::reorderMultiIndex<DIR>(i_in, j_in, k_in);

		// HLL solver following Toro (1998) and Balsara (2017).
		// Radiation eigenvalues from Skinner & Ostriker (2013).

		// gather left- and right- state variables
		double erad_L = x1LeftState(i, j, k, primRadEnergy_index);
		double erad_R = x1RightState(i, j, k, primRadEnergy_index);

		double fx_L = x1LeftState(i, j, k, x1ReducedFlux_index);
		double fx_R = x1RightState(i, j, k, x1ReducedFlux_index);

		double fy_L = x1LeftState(i, j, k, x2ReducedFlux_index);
		double fy_R = x1RightState(i, j, k, x2ReducedFlux_index);

		double fz_L = x1LeftState(i, j, k, x3ReducedFlux_index);
		double fz_R = x1RightState(i, j, k, x3ReducedFlux_index);

		// compute scalar reduced flux f
		double f_L = std::sqrt(fx_L * fx_L + fy_L * fy_L + fz_L * fz_L);
		double f_R = std::sqrt(fx_R * fx_R + fy_R * fy_R + fz_R * fz_R);

		// Compute "un-reduced" Fx, Fy, Fz
		double Fx_L = fx_L * (c_light_ * erad_L);
		double Fx_R = fx_R * (c_light_ * erad_R);

		double Fy_L = fy_L * (c_light_ * erad_L);
		double Fy_R = fy_R * (c_light_ * erad_R);

		double Fz_L = fz_L * (c_light_ * erad_L);
		double Fz_R = fz_R * (c_light_ * erad_R);

		// check that states are physically admissible; if not, use first-order
		// reconstruction
		if (!((erad_L > 0.) && (erad_R > 0.) && (f_L < 1.) && (f_R < 1.))) {
			erad_L = consVar(i - 1, j, k, radEnergy_index);
			erad_R = consVar(i, j, k, radEnergy_index);

			Fx_L = consVar(i - 1, j, k, x1RadFlux_index);
			Fx_R = consVar(i, j, k, x1RadFlux_index);

			Fy_L = consVar(i - 1, j, k, x2RadFlux_index);
			Fy_R = consVar(i, j, k, x2RadFlux_index);

			Fz_L = consVar(i - 1, j, k, x3RadFlux_index);
			Fz_R = consVar(i, j, k, x3RadFlux_index);

			// compute primitive variables
			fx_L = Fx_L / (c_light_ * erad_L);
			fx_R = Fx_R / (c_light_ * erad_R);

			fy_L = Fy_L / (c_light_ * erad_L);
			fy_R = Fy_R / (c_light_ * erad_R);

			fz_L = Fz_L / (c_light_ * erad_L);
			fz_R = Fz_R / (c_light_ * erad_R);

			f_L = std::sqrt(fx_L * fx_L + fy_L * fy_L + fz_L * fz_L);
			f_R = std::sqrt(fx_R * fx_R + fy_R * fy_R + fz_R * fz_R);
		}

		// check that states are physically admissible
		AMREX_ASSERT(erad_L > 0.0);
		AMREX_ASSERT(erad_R > 0.0);
		// AMREX_ASSERT(f_L < 1.0); // there is sometimes a small (<1%) flux
		// limiting violation when using P1 AMREX_ASSERT(f_R < 1.0);

		std::array<amrex::Real, 3> fvec_L = {fx_L, fy_L, fz_L};
		std::array<amrex::Real, 3> fvec_R = {fx_R, fy_R, fz_R};

		// angle between interface and radiation flux \hat{n}
		// If direction is undefined, just drop direction-dependent
		// terms.
		std::array<amrex::Real, 3> n_L{};
		std::array<amrex::Real, 3> n_R{};

		for (int i = 0; i < 3; ++i) {
			n_L[i] = (f_L > 0.) ? (fvec_L[i] / f_L) : 0.;
			n_R[i] = (f_R > 0.) ? (fvec_R[i] / f_R) : 0.;
		}

		// compute radiation pressure tensors
		const double chi_L = RadSystem<problem_t>::ComputeEddingtonFactor(f_L);
		const double chi_R = RadSystem<problem_t>::ComputeEddingtonFactor(f_R);

		AMREX_ASSERT((chi_L >= 1. / 3.) && (chi_L <= 1.0)); // NOLINT
		AMREX_ASSERT((chi_R >= 1. / 3.) && (chi_R <= 1.0)); // NOLINT

		// diagonal term of Eddington tensor
		const double Tdiag_L = (1.0 - chi_L) / 2.0;
		const double Tdiag_R = (1.0 - chi_R) / 2.0;

		// anisotropic term of Eddington tensor (in the direction of the
		// rad. flux)
		const double Tf_L = (3.0 * chi_L - 1.0) / 2.0;
		const double Tf_R = (3.0 * chi_R - 1.0) / 2.0;

		// assemble Eddington tensor
		double T_L[3][3];
		double T_R[3][3];
		double P_L[3][3];
		double P_R[3][3];

		for (int i = 0; i < 3; ++i) {
			for (int j = 0; j < 3; ++j) {
				const double delta_ij = (i == j) ? 1 : 0;
				T_L[i][j] = Tdiag_L * delta_ij + Tf_L * (n_L[i] * n_L[j]);
				T_R[i][j] = Tdiag_R * delta_ij + Tf_R * (n_R[i] * n_R[j]);
				// compute the elements of the total radiation pressure
				// tensor
				P_L[i][j] = T_L[i][j] * erad_L;
				P_R[i][j] = T_R[i][j] * erad_R;
			}
		}

		// frozen Eddington tensor approximation, following Balsara
		// (1999) [JQSRT Vol. 61, No. 5, pp. 617–627, 1999], Eq. 46.
		double Tnormal_L = NAN;
		double Tnormal_R = NAN;
		if constexpr (DIR == FluxDir::X1) {
			Tnormal_L = T_L[0][0];
			Tnormal_R = T_R[0][0];
		} else if constexpr (DIR == FluxDir::X2) {
			Tnormal_L = T_L[1][1];
			Tnormal_R = T_R[1][1];
		} else if constexpr (DIR == FluxDir::X3) {
			Tnormal_L = T_L[2][2];
			Tnormal_R = T_R[2][2];
		}

		// compute fluxes F_L, F_R
		// P_nx, P_ny, P_nz indicate components where 'n' is the direction of the
		// face normal F_n is the radiation flux component in the direction of the
		// face normal
		double Fn_L = NAN;
		double Fn_R = NAN;
		double Pnx_L = NAN;
		double Pnx_R = NAN;
		double Pny_L = NAN;
		double Pny_R = NAN;
		double Pnz_L = NAN;
		double Pnz_R = NAN;

		if constexpr (DIR == FluxDir::X1) {
			Fn_L = Fx_L;
			Fn_R = Fx_R;

			Pnx_L = P_L[0][0];
			Pny_L = P_L[0][1];
			Pnz_L = P_L[0][2];

			Pnx_R = P_R[0][0];
			Pny_R = P_R[0][1];
			Pnz_R = P_R[0][2];
		} else if constexpr (DIR == FluxDir::X2) {
			Fn_L = Fy_L;
			Fn_R = Fy_R;

			Pnx_L = P_L[1][0];
			Pny_L = P_L[1][1];
			Pnz_L = P_L[1][2];

			Pnx_R = P_R[1][0];
			Pny_R = P_R[1][1];
			Pnz_R = P_R[1][2];
		} else if constexpr (DIR == FluxDir::X3) {
			Fn_L = Fz_L;
			Fn_R = Fz_R;

			Pnx_L = P_L[2][0];
			Pny_L = P_L[2][1];
			Pnz_L = P_L[2][2];

			Pnx_R = P_R[2][0];
			Pny_R = P_R[2][1];
			Pnz_R = P_R[2][2];
		}

		AMREX_ASSERT(Fn_L != NAN);
		AMREX_ASSERT(Fn_R != NAN);
		AMREX_ASSERT(Pnx_L != NAN);
		AMREX_ASSERT(Pnx_R != NAN);
		AMREX_ASSERT(Pny_L != NAN);
		AMREX_ASSERT(Pny_R != NAN);
		AMREX_ASSERT(Pnz_L != NAN);
		AMREX_ASSERT(Pnz_R != NAN);

		const quokka::valarray<double, nvarHyperbolic_> F_L = {
		    (c_hat_ / c_light_) * Fn_L, (c_hat_ * c_light_) * Pnx_L,
		    (c_hat_ * c_light_) * Pny_L, (c_hat_ * c_light_) * Pnz_L};

		const quokka::valarray<double, nvarHyperbolic_> F_R = {
		    (c_hat_ / c_light_) * Fn_R, (c_hat_ * c_light_) * Pnx_R,
		    (c_hat_ * c_light_) * Pny_R, (c_hat_ * c_light_) * Pnz_R};

		const quokka::valarray<double, nvarHyperbolic_> U_L = {erad_L, Fx_L, Fy_L, Fz_L};
		const quokka::valarray<double, nvarHyperbolic_> U_R = {erad_R, Fx_R, Fy_R, Fz_R};

		// asymptotic-preserving flux correction
		// [Similar to Skinner et al. (2019), but tau^-2 instead of tau^-1, which does not
		// appear to be asymptotic-preserving with PLM+SDC2.]
		const double tau_cell = ComputeCellOpticalDepth<DIR>(consVar, dx, i, j, k);

		// ensures that signal speed -> c \sqrt{f_xx} / tau_cell in the diffusion
		// limit [see Appendix of Jiang et al. ApJ 767:148 (2013)]
		// const double S_corr = std::sqrt(1.0 - std::exp(-tau_cell * tau_cell)) /
		//		      tau_cell; // Jiang et al. (2013)
		const double S_corr = std::min(1.0, 1.0 / tau_cell); // Skinner et al.

		// adjust the wavespeeds (cancels out except for the last term in the HLL flux)
		// const quokka::valarray<double, nvarHyperbolic_> epsilon =
		// {S_corr, 1.0, 1.0, 1.0}; // Skinner et al. (2019) const quokka::valarray<double,
		// nvarHyperbolic_> epsilon = {S_corr, S_corr, S_corr, S_corr}; // Jiang et al.
		// (2013)
		const quokka::valarray<double, nvarHyperbolic_> epsilon = {
		    S_corr * S_corr, S_corr, S_corr, S_corr}; // this code

		// compute the norm of the wavespeed vector
		const double S_L = std::min(-0.1 * c_hat_, -c_hat_ * std::sqrt(Tnormal_L));
		const double S_R = std::max(0.1 * c_hat_, c_hat_ * std::sqrt(Tnormal_R));

		AMREX_ASSERT(std::abs(S_L) <= c_hat_); // NOLINT
		AMREX_ASSERT(std::abs(S_R) <= c_hat_); // NOLINT

		// in the frozen Eddington tensor approximation, we are always
		// in the star region, so F = F_star
		const quokka::valarray<double, nvarHyperbolic_> F =
		    (S_R / (S_R - S_L)) * F_L - (S_L / (S_R - S_L)) * F_R +
		    epsilon * (S_R * S_L / (S_R - S_L)) * (U_R - U_L);

		// check states are valid
		AMREX_ASSERT(!std::isnan(F[0])); // NOLINT
		AMREX_ASSERT(!std::isnan(F[1])); // NOLINT
		AMREX_ASSERT(!std::isnan(F[2])); // NOLINT
		AMREX_ASSERT(!std::isnan(F[3])); // NOLINT

		x1Flux(i, j, k, radEnergy_index - nstartHyperbolic_) = F[0];
		x1Flux(i, j, k, x1RadFlux_index - nstartHyperbolic_) = F[1];
		x1Flux(i, j, k, x2RadFlux_index - nstartHyperbolic_) = F[2];
		x1Flux(i, j, k, x3RadFlux_index - nstartHyperbolic_) = F[3];

		const quokka::valarray<double, nvarHyperbolic_> diffusiveF =
		    (S_R / (S_R - S_L)) * F_L - (S_L / (S_R - S_L)) * F_R +
		    (S_R * S_L / (S_R - S_L)) * (U_R - U_L);

		x1FluxDiffusive(i, j, k, radEnergy_index - nstartHyperbolic_) = diffusiveF[0];
		x1FluxDiffusive(i, j, k, x1RadFlux_index - nstartHyperbolic_) = diffusiveF[1];
		x1FluxDiffusive(i, j, k, x2RadFlux_index - nstartHyperbolic_) = diffusiveF[2];
		x1FluxDiffusive(i, j, k, x3RadFlux_index - nstartHyperbolic_) = diffusiveF[3];
	});
}

template <typename problem_t>
AMREX_GPU_HOST_DEVICE auto RadSystem<problem_t>::ComputeOpacity(const double /*rho*/,
								const double /*Tgas*/) -> double
{
	return 1.0;
}

template <typename problem_t>
AMREX_GPU_HOST_DEVICE auto RadSystem<problem_t>::ComputeOpacityTempDerivative(const double /*rho*/,
									      const double /*Tgas*/)
    -> double
{
	return 0.0;
}

template <typename problem_t>
AMREX_GPU_HOST_DEVICE auto RadSystem<problem_t>::ComputeTgasFromEgas(const double rho,
								     const double Egas) -> double
{
	constexpr double c_v = boltzmann_constant_ / (mean_molecular_mass_ * (gamma_ - 1.0));
	return (Egas / (rho * c_v));
}

template <typename problem_t>
AMREX_GPU_HOST_DEVICE auto RadSystem<problem_t>::ComputeEgasFromTgas(const double rho,
								     const double Tgas) -> double
{
	constexpr double c_v = boltzmann_constant_ / (mean_molecular_mass_ * (gamma_ - 1.0));
	return (rho * c_v * Tgas);
}

template <typename problem_t>
AMREX_GPU_HOST_DEVICE auto RadSystem<problem_t>::ComputeEgasTempDerivative(const double rho,
									   const double /*Tgas*/)
    -> double
{
	constexpr double c_v = boltzmann_constant_ / (mean_molecular_mass_ * (gamma_ - 1.0));
	return (rho * c_v);
}

template <typename problem_t>
AMREX_GPU_HOST_DEVICE auto
RadSystem<problem_t>::ComputeEintFromEgas(const double density, const double X1GasMom,
					  const double X2GasMom, const double X3GasMom,
					  const double Etot) -> double
{
	const double p_sq = X1GasMom * X1GasMom + X2GasMom * X2GasMom + X3GasMom * X3GasMom;
	const double Ekin = p_sq / (2.0 * density);
	const double Eint = Etot - Ekin;
	return Eint;
}

template <typename problem_t>
AMREX_GPU_HOST_DEVICE auto
RadSystem<problem_t>::ComputeEgasFromEint(const double density, const double X1GasMom,
					  const double X2GasMom, const double X3GasMom,
					  const double Eint) -> double
{
	const double p_sq = X1GasMom * X1GasMom + X2GasMom * X2GasMom + X3GasMom * X3GasMom;
	const double Ekin = p_sq / (2.0 * density);
	const double Etot = Eint + Ekin;
	return Etot;
}

template <typename problem_t>
void RadSystem<problem_t>::AddSourceTerms(array_t &consVar, arrayconst_t &radEnergySource,
					  arrayconst_t &advectionFluxes,
					  amrex::Box const &indexRange, amrex::Real dt)
{
	arrayconst_t &consPrev = consVar; // make read-only
	array_t &consNew = consVar;

	// Lorentz transform the radiation variables into the comoving frame
	// TransformIntoComovingFrame(fluid_velocity);

	// Add source terms

	// 1. Compute gas energy and radiation energy update following Howell &
	// Greenough [Journal of Computational Physics 184 (2003) 53–78].

	// cell-centered kernel
	amrex::ParallelFor(indexRange, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
		const double c = c_light_;
		const double chat = c_hat_;
		const double a_rad = radiation_constant_;

		// load fluid properties
		const double rho = consPrev(i, j, k, gasDensity_index);
		const double Egastot0 = consPrev(i, j, k, gasEnergy_index);
		const double x1GasMom0 = consPrev(i, j, k, x1GasMomentum_index);
		const double x2GasMom0 = consPrev(i, j, k, x2GasMomentum_index);
		const double x3GasMom0 = consPrev(i, j, k, x3GasMomentum_index);
		amrex::GpuArray<const amrex::Real, 3> vel0 = {
		    x1GasMom0 / rho, x2GasMom0 / rho,
		    x3GasMom0 / rho}; // needed to update kinetic energy
		const double Egas0 =
		    ComputeEintFromEgas(rho, x1GasMom0, x2GasMom0, x3GasMom0, Egastot0);

		// load radiation energy
		const double Erad0 = consPrev(i, j, k, radEnergy_index);

		AMREX_ASSERT(Egas0 > 0.0); // NOLINT
		AMREX_ASSERT(Erad0 > 0.0); // NOLINT

		const double Etot0 = Egas0 + (c / chat) * Erad0;

		// BEGIN NEWTON-RAPHSON LOOP
		double F_G = NAN;
		double F_R = NAN;
		double rhs = NAN;
		double T_gas = NAN;
		double kappa = NAN;
		double fourPiB = NAN;
		double drhs_dEgas = NAN;
		double dFG_dEgas = NAN;
		double dFG_dErad = NAN;
		double dFR_dEgas = NAN;
		double dFR_dErad = NAN;
		double Src = NAN;
		double eta = NAN;
		double deltaErad = NAN;
		double deltaEgas = NAN;

		double Egas_guess = Egas0;
		double Erad_guess = Erad0;
		// const double T_floor = 1e-10;
		const double resid_tol = 1e-10;
		const int maxIter = 200;
		int n = 0;
		for (n = 0; n < maxIter; ++n) {

			// compute material temperature
			T_gas = RadSystem<problem_t>::ComputeTgasFromEgas(rho, Egas_guess);
			AMREX_ASSERT(T_gas >= 0.);

			// compute opacity, emissivity
			kappa = RadSystem<problem_t>::ComputeOpacity(rho, T_gas);
			AMREX_ASSERT(kappa >= 0.);
			fourPiB = chat * a_rad * std::pow(T_gas, 4);

			// constant radiation energy source term
			// plus advection source term (for well-balanced/SDC integrators)
			Src = dt * ((chat * radEnergySource(i, j, k)) + advectionFluxes(i, j, k));
			AMREX_ASSERT(!std::isnan(Src));

			// compute derivatives w/r/t T_gas
			const double dB_dTgas = (4.0 * fourPiB) / T_gas;
			const double dkappa_dTgas =
			    RadSystem<problem_t>::ComputeOpacityTempDerivative(rho, T_gas);

			// compute residuals
			rhs = dt * (rho * kappa) * (fourPiB - chat * Erad_guess);
			F_G = (Egas_guess - Egas0) + ((c / chat) * rhs);
			F_R = (Erad_guess - Erad0) - (rhs + Src);

			// check if converged
			if ((std::abs(F_G / Etot0) < resid_tol) &&
			    (std::abs(F_R / Etot0) < resid_tol)) {
				break;
			}

			// compute Jacobian elements
			const double c_v =
			    RadSystem<problem_t>::ComputeEgasTempDerivative(rho, T_gas);

			drhs_dEgas =
			    (rho * dt / c_v) *
			    (kappa * dB_dTgas + dkappa_dTgas * (fourPiB - chat * Erad_guess));

			// Update variables
			dFG_dEgas = 1.0 + (c / chat) * drhs_dEgas;     // 1.0 + epsilon;
			dFG_dErad = dt * (-(rho * kappa) * c);	       // -R*alpha
			dFR_dEgas = -drhs_dEgas;		       		   // -Rinv * epsilon;
			dFR_dErad = 1.0 + dt * ((rho * kappa) * chat); // 1.0 + alpha;
			eta = -dFR_dEgas / dFG_dEgas;
			// eta = (eta > 0.0) ? eta : 0.0;
			deltaErad = -(F_R + eta * F_G) / (dFR_dErad + eta * dFG_dErad);
			deltaEgas = -(F_G + dFG_dErad * deltaErad) / dFG_dEgas;

			AMREX_ASSERT(!std::isnan(deltaErad));
			AMREX_ASSERT(!std::isnan(deltaEgas));

			Egas_guess += deltaEgas;
			Erad_guess += deltaErad;

		} // END NEWTON-RAPHSON LOOP

		AMREX_ALWAYS_ASSERT(std::abs(F_G / Etot0) < resid_tol);
		AMREX_ALWAYS_ASSERT(std::abs(F_R / Etot0) < resid_tol);

		AMREX_ALWAYS_ASSERT(Erad_guess > 0.0);
		AMREX_ALWAYS_ASSERT(Egas_guess > 0.0);

		// store new radiation energy, gas energy
		consNew(i, j, k, radEnergy_index) = Erad_guess;
		consNew(i, j, k, gasEnergy_index) =
		    ComputeEgasFromEint(rho, x1GasMom0, x2GasMom0, x3GasMom0, Egas_guess);

		// 2. Compute radiation flux update
		amrex::GpuArray<amrex::Real, 3> Frad_t0{};
		amrex::GpuArray<amrex::Real, 3> Frad_t1{};

		Frad_t0[0] = consPrev(i, j, k, x1RadFlux_index);
		Frad_t0[1] = consPrev(i, j, k, x2RadFlux_index);
		Frad_t0[2] = consPrev(i, j, k, x3RadFlux_index);

		for (int n = 0; n < 3; ++n) {
			Frad_t1[n] = (Frad_t0[n] + (dt * advectionFluxes(i, j, k, n))) /
				     (1.0 + (rho * kappa) * chat * dt);
		}
		consNew(i, j, k, x1RadFlux_index) = Frad_t1[0];
		consNew(i, j, k, x2RadFlux_index) = Frad_t1[1];
		consNew(i, j, k, x3RadFlux_index) = Frad_t1[2];

		// 3. Compute conservative gas momentum update -- modify in 3d
		//	[N.B. should this step happen after the Lorentz	transform?]
		amrex::GpuArray<amrex::Real, 3> dF{};
		amrex::GpuArray<amrex::Real, 3> dMomentum{};

		for (int n = 0; n < 3; ++n) {
			dF[n] = Frad_t1[n] - Frad_t0[n];
			dMomentum[n] = -dF[n] / (c * chat);
		}

		consNew(i, j, k, x1GasMomentum_index) += dMomentum[0];
		consNew(i, j, k, x2GasMomentum_index) += dMomentum[1];
		consNew(i, j, k, x3GasMomentum_index) += dMomentum[2];

		// 4. Update kinetic energy of gas
		double dEkin = 0.;
		for (int n = 0; n < 3; ++n) {
			dEkin += (vel0[n] * dMomentum[n]);
		}
		consNew(i, j, k, gasEnergy_index) += dEkin;

		// Lorentz transform back to 'laboratory' frame
		// TransformIntoComovingFrame(-fluid_velocity);
	});
}

template <typename problem_t>
void RadSystem<problem_t>::ComputeSourceTermsExplicit(arrayconst_t &consPrev,
						      arrayconst_t &radEnergySource, array_t &src,
						      amrex::Box const &indexRange, amrex::Real dt)
{
	const double chat = c_hat_;
	const double a_rad = radiation_constant_;

	// cell-centered kernel
	amrex::ParallelFor(indexRange, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
		// load gas energy
		const auto rho = consPrev(i, j, k, gasDensity_index);
		const auto Egastot0 = consPrev(i, j, k, gasEnergy_index);
		const auto x1GasMom0 = consPrev(i, j, k, x1GasMomentum_index);
		const double x2GasMom0 = consPrev(i, j, k, x2GasMomentum_index);
		const double x3GasMom0 = consPrev(i, j, k, x3GasMomentum_index);
		const auto Egas0 =
		    ComputeEintFromEgas(rho, x1GasMom0, x2GasMom0, x3GasMom0, Egastot0);

		// load radiation energy, momentum
		const auto Erad0 = consPrev(i, j, k, radEnergy_index);
		const auto Frad0_x = consPrev(i, j, k, x1RadFlux_index);

		// compute material temperature
		const auto T_gas = RadSystem<problem_t>::ComputeTgasFromEgas(rho, Egas0);

		// compute opacity, emissivity
		const auto kappa = RadSystem<problem_t>::ComputeOpacity(rho, T_gas);
		const auto fourPiB = chat * a_rad * std::pow(T_gas, 4);

		// constant radiation energy source term
		const auto Src = dt * (chat * radEnergySource(i, j, k));

		// compute reaction term
		const auto rhs = dt * (rho * kappa) * (fourPiB - chat * Erad0);
		const auto Fx_rhs = -dt * chat * (rho * kappa) * Frad0_x;

		src(radEnergy_index, i) = rhs;
		src(x1RadFlux_index, i) = Fx_rhs;
	});
}

#endif // RADIATION_SYSTEM_HPP_
