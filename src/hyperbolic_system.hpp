#ifndef HYPERBOLIC_SYSTEM_HPP_ // NOLINT
#define HYPERBOLIC_SYSTEM_HPP_
//==============================================================================
// TwoMomentRad - a radiation transport library for patch-based AMR codes
// Copyright 2020 Benjamin Wibking.
// Released under the MIT license. See LICENSE file included in the GitHub repo.
//==============================================================================
/// \file hyperbolic_system.hpp
/// \brief Defines classes and functions for use with hyperbolic systems of
/// conservation laws.
///
/// This file provides classes, data structures and functions for hyperbolic
/// systems of conservation laws.
///

// c++ headers
#include <cassert>
#include <cmath>

// library headers
#include "AMReX_Array4.H"
#include "AMReX_FArrayBox.H"

// internal headers

/// Provide type-safe global sign ('sgn') function.
template <typename T> auto sgn(T val) -> int { return (T(0) < val) - (val < T(0)); }

using array_t = amrex::Array4<amrex::Real> const;
using arrayconst_t = amrex::Array4<const amrex::Real> const;

/// Class for a hyperbolic system of conservation laws (Cannot be instantiated,
/// must be subclassed.)
template <typename problem_t> class HyperbolicSystem
{
      public:
	/// Computes timestep and advances system
	// void AdvanceTimestep();
	// virtual void AdvanceTimestep(double dt_max);
	// void AdvanceTimestepSDC2(double dt);

	// setter functions:

	void set_cflNumber(double cflNumber);

	// accessor functions:

	[[nodiscard]] auto nvars() const -> int;
	[[nodiscard]] auto nghost() const -> int;
	[[nodiscard]] auto nx() const -> int;
	[[nodiscard]] auto dim1() const -> int;
	[[nodiscard]] auto time() const -> double;
	[[nodiscard]] auto dt() const -> double;

	// static member functions

	[[nodiscard]] AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE static auto MC(double a, double b)
	    -> double
	{
		return 0.5 * (sgn(a) + sgn(b)) *
		       std::min(0.5 * std::abs(a + b),
				std::min(2.0 * std::abs(a), 2.0 * std::abs(b)));
	}

	static void ReconstructStatesConstant(arrayconst_t &q, array_t &leftState, array_t &rightState,
					      std::pair<int, int> range, int nvars);
	static void ReconstructStatesPLM(arrayconst_t &q, array_t &leftState, array_t &rightState,
					 std::pair<int, int> range, int nvars);
	static void ReconstructStatesPPM(arrayconst_t &q, array_t &leftState, array_t &rightState,
					 std::pair<int, int> range, int nvars);

	static void CopyVars(arrayconst_t &src, array_t &dest, std::pair<int, int> range, int nvars);
	static auto ComputeResidual(arrayconst_t &cur, arrayconst_t &prev, std::pair<int, int> range,
				    int nvars) -> double;
	static auto ComputeNorm(arrayconst_t &arr, std::pair<int, int> range, int nvars) -> double;

	static void AddFluxesRK2(array_t &U_new, arrayconst_t &U0, arrayconst_t &U1, arrayconst_t &x1Flux,
				 double dt, double dx, std::pair<int, int> range, int nvars);
	static void PredictStep(arrayconst_t &consVarOld, array_t &consVarNew, arrayconst_t &x1Flux,
				double dt, double dx, std::pair<int, int> range, int nvars);
	static void SaveFluxes(array_t &advectionFluxes, arrayconst_t &x1Flux, double dx, std::pair<int, int> range, int nvars);

	// static void AdvanceTimestepRK2(const double dt, array_t &consVar, std::pair<int,int>
	// cell_range, const int nvars);

	// non-static member functions

	virtual void FillGhostZones(array_t &cons);
	virtual void AddSourceTerms(array_t &U_prev, array_t &U_new, std::pair<int, int> range);
	virtual void ComputeSourceTermsExplicit(array_t &U_prev, array_t &src,
						std::pair<int, int> range);
	virtual auto CheckStatesValid(arrayconst_t &cons, std::pair<int, int> range) -> bool;
	virtual void ComputeFlatteningCoefficients(std::pair<int, int> range);
	virtual void FlattenShocks(array_t &q, std::pair<int, int> range);

      protected:

	double cflNumber_ = 1.0;
	double dt_ = 0;
	const double dtExpandFactor_ = 1.2;
	double dtPrev_ = std::numeric_limits<double>::max();
	double time_ = 0.;
	double lx_;
	double dx_;
	int nx_;
	int dim1_;
	int nvars_;
	const int nghost_ = 4; // 4 ghost cells required for PPM

	HyperbolicSystem(int nx, double lx, double cflNumber, int nvars)
	    : cflNumber_(cflNumber), lx_(lx), dx_(lx / static_cast<double>(nx)), nx_(nx),
	      nvars_(nvars)
	{
		assert(lx_ > 0.0);				   // NOLINT
		assert(nx_ > 2);				   // NOLINT
		assert(nghost_ > 1);				   // NOLINT
		assert((cflNumber_ > 0.0) && (cflNumber_ <= 1.0)); // NOLINT

		dim1_ = nx_ + 2 * nghost_;
	}

	// virtual auto ComputeTimestep(double dt_max) -> double = 0;
	// virtual void ComputeFluxes(std::pair<int, int> range) = 0;
	// virtual void ComputeFirstOrderFluxes(std::pair<int, int> range) = 0;
};

template <typename problem_t> auto HyperbolicSystem<problem_t>::time() const -> double
{
	return time_;
}

template <typename problem_t> auto HyperbolicSystem<problem_t>::dt() const -> double { return dt_; }

template <typename problem_t> auto HyperbolicSystem<problem_t>::nx() const -> int { return nx_; }

template <typename problem_t> auto HyperbolicSystem<problem_t>::dim1() const -> int
{
	return dim1_;
}

template <typename problem_t> auto HyperbolicSystem<problem_t>::nghost() const -> int
{
	return nghost_;
}

template <typename problem_t> auto HyperbolicSystem<problem_t>::nvars() const -> int
{
	return nvars_;
}

template <typename problem_t> void HyperbolicSystem<problem_t>::set_cflNumber(double cflNumber)
{
	assert((cflNumber > 0.0) && (cflNumber <= 1.0)); // NOLINT
	cflNumber_ = cflNumber;
}

template <typename problem_t>
void HyperbolicSystem<problem_t>::AddSourceTerms(array_t &U_prev, array_t &U_new,
						 std::pair<int, int> range)
{
	// intentionally zero source terms by default
}

template <typename problem_t>
void HyperbolicSystem<problem_t>::ComputeSourceTermsExplicit(array_t &U_prev, array_t &src,
							     std::pair<int, int> range)
{
	// intentionally zero source terms by default
}

template <typename problem_t>
void HyperbolicSystem<problem_t>::ComputeFlatteningCoefficients(std::pair<int, int> range)
{
	// intentionally no flattening by default
}

template <typename problem_t>
void HyperbolicSystem<problem_t>::FlattenShocks(array_t &q, std::pair<int, int> range)
{
	// intentionally no flattening by default
}

template <typename problem_t> void HyperbolicSystem<problem_t>::FillGhostZones(array_t &cons)
{
	// In general, this step will require MPI communication, and interaction
	// with the main AMR code.

	// periodic boundary conditions
#if 0
	// x1 right side boundary
	for (int n = 0; n < nvars_; ++n) {
		for (int i = nghost_ + nx_; i < nghost_ + nx_ + nghost_; ++i) {
			cons(i, j, k, n) = cons(n, i - nx_);
		}
	}

	// x1 left side boundary
	for (int n = 0; n < nvars_; ++n) {
		for (int i = 0; i < nghost_; ++i) {
			cons(i, j, k, n) = cons(n, i + nx_);
		}
	}
#endif

	const int j = 1;
	const int k = 1;

	// extrapolate boundary conditions
	// x1 right side boundary
	for (int n = 0; n < nvars_; ++n) {
		for (int i = nghost_ + nx_; i < nghost_ + nx_ + nghost_; ++i) {
			cons(i, j, k, n) = cons(nghost_ + nx_ - 1, j, k, n);
		}
	}

	// x1 left side boundary
	for (int n = 0; n < nvars_; ++n) {
		for (int i = 0; i < nghost_; ++i) {
			cons(i, j, k, n) = cons(nghost_ + 0, j, k, n);
		}
	}
}

template <typename problem_t>
void HyperbolicSystem<problem_t>::ReconstructStatesConstant(arrayconst_t &q, array_t &leftState,
							    array_t &rightState,
							    const std::pair<int, int> range,
							    const int nvars)
{
	// By convention, the interfaces are defined on the left edge of each
	// zone, i.e. xleft_(i) is the "left"-side of the interface at
	// the left edge of zone i, and xright_(i) is the "right"-side of the
	// interface at the *left* edge of zone i.

	const int j = 1;
	const int k = 1;

	// Indexing note: There are (nx + 1) interfaces for nx zones.
	for (int n = 0; n < nvars; ++n) {
		for (int i = range.first; i < (range.second + 1); ++i) {

			// Use piecewise-constant reconstruction
			// (This converges at first order in spatial
			// resolution.)

			leftState(i, j, k, n) = q(i - 1, j, k, n);
			rightState(i, j, k, n) = q(i, j, k, n);
		}
	}
}

template <typename problem_t>
void HyperbolicSystem<problem_t>::ReconstructStatesPLM(arrayconst_t &q, array_t &leftState,
						       array_t &rightState,
						       const std::pair<int, int> range,
						       const int nvars)
{
	// Unlike PPM, PLM with the MC limiter is TVD.
	// (There are no spurious oscillations, *except* in the slow-moving shock problem,
	// which can produce unphysical oscillations even when using upwind Godunov fluxes.)
	// However, most tests fail when using PLM reconstruction because
	// the accuracy tolerances are very strict, and the L1 error is significantly
	// worse compared to PPM for a fixed number of mesh elements.

	// By convention, the interfaces are defined on the left edge of each
	// zone, i.e. xleft_(i) is the "left"-side of the interface at
	// the left edge of zone i, and xright_(i) is the "right"-side of the
	// interface at the *left* edge of zone i.

	// Indexing note: There are (nx + 1) interfaces for nx zones.
	const int j = 1;
	const int k = 1;

	for (int n = 0; n < nvars; ++n) {
		for (int i = range.first; i < (range.second + 1); ++i) {

			// Use piecewise-linear reconstruction
			// (This converges at second order in spatial resolution.)

			const auto lslope = MC(q(i, j, k, n) - q(i - 1, j, k, n), q(i - 1, j, k, n) - q(i - 2, j, k, n));
			const auto rslope = MC(q(i + 1, j, k, n) - q(i, j, k, n), q(i, j, k, n) - q(i - 1, j, k, n));

			leftState(i, j, k, n) = q(i - 1, j, k, n) + 0.25 * lslope; // NOLINT
			rightState(i, j, k, n) = q(i, j, k, n) - 0.25 * rslope;    // NOLINT
		}
	}

	// Important final step: ensure that velocity does not exceed c
	// in any cell where v^2 > c, reconstruct using first-order method for all velocity
	// components (must be done by user)
	// ComputeFlatteningCoefficients(std::make_pair(-2 + nghost_, nghost_ + nx_ + 2));

	// Apply shock flattening
	// FlattenShocks(q, range);
}

template <typename problem_t>
void HyperbolicSystem<problem_t>::ReconstructStatesPPM(arrayconst_t &q, array_t &leftState,
						       array_t &rightState,
						       const std::pair<int, int> range,
						       const int nvars)
{
	// By convention, the interfaces are defined on the left edge of each
	// zone, i.e. xleft_(i) is the "left"-side of the interface at the left
	// edge of zone i, and xright_(i) is the "right"-side of the interface
	// at the *left* edge of zone i.

	// Indexing note: There are (nx + 1) interfaces for nx zones.
	const int j = 1;
	const int k = 1;

	for (int n = 0; n < nvars; ++n) {
		for (int i = range.first; i < (range.second + 1); ++i) {
			// PPM reconstruction following Colella & Woodward (1984), with some
			// modifications following Mignone (2014), as implemented in Athena++.

			// (1.) Estimate the interface a_{i - 1/2}. Equivalent to step 1 in Athena++
			// [ppm_simple.cpp].

			// C&W Eq. (1.9) [parabola midpoint for the case of equally-spaced zones]:
			// a_{j+1/2} = (7/12)(a_j + a_{j+1}) - (1/12)(a_{j+2} + a_{j-1}). Terms are
			// grouped to preserve exact symmetry in floating-pointn arithmetic,
			// following Athena++.

			const double coef_1 = (7. / 12.);
			const double coef_2 = (-1. / 12.);
			const double interface = (coef_1 * q(i, j, k, n) + coef_2 * q(i + 1, j, k, n)) +
						 (coef_1 * q(i - 1, j, k, n) + coef_2 * q(i - 2, j, k, n));

			// a_R,(i-1) in C&W
			leftState(i, j, k, n) = interface;

			// a_L,i in C&W
			rightState(i, j, k, n) = interface;
		}
	}

	for (int n = 0; n < nvars; ++n) {
		for (int i = range.first; i < range.second; ++i) {
			// (2.) Constrain interface value to lie between adjacent cell-averaged
			// values (equivalent to step 2b in Athena++ [ppm_simple.cpp]). [See Eq. B8
			// of Mignone+ 2005]

			// compute bounds from surrounding cells
			const std::pair<double, double> bounds =
			    std::minmax({q(i - 1, j, k, n), q(i, j, k, n), q(i + 1, j, k, n)}); // modify in 3d !!

			// get interfaces
			const double a_minus = rightState(i, j, k, n);
			const double a_plus = leftState(i + 1, j, k, n);

			// left side of zone i
			const double new_a_minus = std::clamp(a_minus, bounds.first, bounds.second);

			// right side of zone i
			const double new_a_plus = std::clamp(a_plus, bounds.first, bounds.second);

			rightState(i, j, k, n) = new_a_minus;
			leftState(i + 1, j, k, n) = new_a_plus;
		}
	}

	for (int n = 0; n < nvars; ++n) {
		for (int i = range.first; i < range.second; ++i) {

			const double a_minus = rightState(i, j, k, n);   // a_L,i in C&W
			const double a_plus = leftState(i + 1, j, k, n); // a_R,i in C&W
			const double a = q(i, j, k, n);		   // a_i in C&W

			const double dq_minus = (a - a_minus);
			const double dq_plus = (a_plus - a);

			double new_a_minus = a_minus;
			double new_a_plus = a_plus;

			// (3.) Monotonicity correction, using Eq. (1.10) in PPM paper. Equivalent
			// to step 4b in Athena++ [ppm_simple.cpp].

			const double qa = dq_plus * dq_minus; // interface extrema

			if ((qa <= 0.0)) { // local extremum

				// Causes subtle, but very weird, oscillations in the Shu-Osher test
				// problem. However, it is necessary to get a reasonable solution
				// for the sawtooth advection problem.
				const double dq0 = MC(q(i + 1, j, k, n) - q(i, j, k, n), q(i, j, k, n) - q(i - 1, j, k, n));

				// use linear reconstruction, following Balsara (2017) [Living Rev
				// Comput Astrophys (2017) 3:2]
				new_a_minus = a - 0.5 * dq0;
				new_a_plus = a + 0.5 * dq0;

				// original C&W method for this case
				// new_a_minus = a;
				// new_a_plus = a;

			} else { // no local extrema

				// parabola overshoots near a_plus -> reset a_minus
				if (std::abs(dq_minus) >= 2.0 * std::abs(dq_plus)) {
					new_a_minus = a - 2.0 * dq_plus;
				}

				// parabola overshoots near a_minus -> reset a_plus
				if (std::abs(dq_plus) >= 2.0 * std::abs(dq_minus)) {
					new_a_plus = a + 2.0 * dq_minus;
				}
			}

			rightState(i, j, k, n) = new_a_minus;
			leftState(i + 1, j, k, n) = new_a_plus;
		}
	}

	// Important final step: ensure that velocity does not exceed c
	// in any cell where v^2 > c, reconstruct using first-order method for all velocity
	// components (must be done by user)
	// ComputeFlatteningCoefficients(std::make_pair(-2 + nghost_, nghost_ + nx_ + 2));

	// Apply shock flattening
	// FlattenShocks(q, range);
}

template <typename problem_t>
void HyperbolicSystem<problem_t>::SaveFluxes(array_t &advectionFluxes, arrayconst_t &x1Flux, const double dx, const std::pair<int, int> range, const int nvars)
{
	// By convention, the fluxes are defined on the left edge of each zone,
	// i.e. flux_(i) is the flux *into* zone i through the interface on the
	// left of zone i, and -1.0*flux(i+1) is the flux *into* zone i through
	// the interface on the right of zone i.
	const int j = 1;
	const int k = 1;

	for (int n = 0; n < nvars; ++n) {
		for (int i = range.first; i < range.second; ++i) {
			advectionFluxes(i, j, k, n) = (-1.0 / dx) * (x1Flux(i + 1, j, k, n) - x1Flux(i, j, k, n));
		}
	}
}

template <typename problem_t>
void HyperbolicSystem<problem_t>::PredictStep(arrayconst_t &consVarOld, array_t &consVarNew,
					      arrayconst_t &x1Flux, const double dt, const double dx,
					      const std::pair<int, int> range, const int nvars)
{
	// By convention, the fluxes are defined on the left edge of each zone,
	// i.e. flux_(i) is the flux *into* zone i through the interface on the
	// left of zone i, and -1.0*flux(i+1) is the flux *into* zone i through
	// the interface on the right of zone i.
	const int j = 1;
	const int k = 1;

	for (int n = 0; n < nvars; ++n) {
		for (int i = range.first; i < range.second; ++i) {
			consVarNew(i, j, k, n) =
			    consVarOld(i, j, k, n) - (dt / dx) * (x1Flux(i + 1, j, k, n) - x1Flux(i, j, k, n));
		}
	}
}

template <typename problem_t>
void HyperbolicSystem<problem_t>::AddFluxesRK2(array_t &U_new, arrayconst_t &U0, arrayconst_t &U1,
					       arrayconst_t &x1Flux, const double dt, const double dx,
					       const std::pair<int, int> range, const int nvars)
{
	// By convention, the fluxes are defined on the left edge of each zone,
	// i.e. flux_(i) is the flux *into* zone i through the interface on the
	// left of zone i, and -1.0*flux(i+1) is the flux *into* zone i through
	// the interface on the right of zone i.
	const int j = 1;
	const int k = 1;

	for (int n = 0; n < nvars; ++n) {
		for (int i = range.first; i < range.second; ++i) {
			// RK-SSP2 integrator
			const double U_0 = U0(i, j, k, n);
			const double U_1 = U1(i, j, k, n);
			const double FU_1 = -1.0 * (dt / dx) * (x1Flux(i + 1, j, k, n) - x1Flux(i, j, k, n));

			// save results in U_new
			U_new(i, j, k, n) = 0.5 * U_0 + 0.5 * U_1 + 0.5 * FU_1;
		}
	}
}

template <typename problem_t>
auto HyperbolicSystem<problem_t>::CheckStatesValid(arrayconst_t & /*cons*/,
						   const std::pair<int, int> /*range*/) -> bool
{
	return true;
}

template <typename problem_t>
void HyperbolicSystem<problem_t>::CopyVars(arrayconst_t &src, array_t &dest,
					   const std::pair<int, int> range, const int nvars)
{
	const int j = 1;
	const int k = 1;

	for (int n = 0; n < nvars; ++n) {
		for (int i = range.first; i < range.second; ++i) {
			dest(i, j, k, n) = src(i, j, k, n);
		}
	}
}

template <typename problem_t>
auto HyperbolicSystem<problem_t>::ComputeResidual(arrayconst_t &cur, arrayconst_t &prev,
						  const std::pair<int, int> range, const int nvars)
    -> double
{
	const int j = 1;
	const int k = 1;

	double norm = 0.;
	for (int n = 0; n < nvars; ++n) {
		double comp = 0.;
		for (int i = range.first; i < range.second; ++i) {
			comp += std::abs(cur(i, j, k, n) - prev(i, j, k, n));
		}
		comp *= 1.0 / (range.second - range.first);
		norm += comp * comp;
	}
	return std::sqrt(norm);
}

template <typename problem_t>
auto HyperbolicSystem<problem_t>::ComputeNorm(arrayconst_t &arr, const std::pair<int, int> range,
					      const int nvars) -> double
{
	const int j = 1;
	const int k = 1;

	double norm = 0.;
	for (int n = 0; n < nvars; ++n) {
		double comp = 0.;
		for (int i = range.first; i < range.second; ++i) {
			comp += std::abs(arr(i, j, k, n));
		}
		comp *= 1.0 / (range.second - range.first);
		norm += comp * comp;
	}
	return std::sqrt(norm);
}

#if 0
template <typename problem_t> void HyperbolicSystem<problem_t>::AdvanceTimestepRK2(const double dt,
	array_t &consVar, std::pair<int,int> cell_range, const int nvars)
{
	const auto ppm_range = std::make_pair(-1 + cell_range.first, 1 + cell_range.second);

	// Allocate temporary arrays for intermediate stages
	// primVar, x1LeftState, x1RightState, x1Fluxes
	// consVarPredictStep

	const auto [dim1, dim2, dim3] = amrex::length(consVar.arr_);
	array_t primVar(nvars, dim1);
	array_t x1LeftState(nvars, dim1);
	array_t x1RightState(nvars, dim1);
	array_t x1Fluxes(nvars, dim1);
	array_t consVarPredictStep(nvars, dim1);

	// Initialize data
	FillGhostZones(consVar);
	ConservedToPrimitive(consVar, std::make_pair(0, dim1));

	// Stage 1 of RK2-SSP
	FillGhostZones(consVar);
	ConservedToPrimitive(consVar, std::make_pair(0, dim1));

	ReconstructStatesPPM(primVar, x1LeftState, x1RightState, ppm_range, nvars);
	ComputeFlatteningCoefficients(std::make_pair(-2 + cell_range.first, 2 + cell_range.second));
	FlattenShocks(primVar, ppm_range);

	ComputeFluxes(cell_range);
	ComputeFirstOrderFluxes(cell_range);
	PredictStep(cell_range);

	AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
	    CheckStatesValid(consVarPredictStep_, cell_range),
	    "[stage 1] Non-realizable states produced. This should not happen!");

	// Stage 2 of RK2-SSP
	FillGhostZones(consVarPredictStep);
	ConservedToPrimitive(consVarPredictStep, std::make_pair(0, dim1_));

	ReconstructStatesPPM(primVar, x1LeftState, x1RightState, ppm_range, nvars);
	ComputeFlatteningCoefficients(std::make_pair(-2 + cell_range.first, 2 + cell_range.second));
	FlattenShocks(primVar, ppm_range);

	ComputeFluxes(cell_range);
	ComputeFirstOrderFluxes(cell_range);
	AddFluxesRK2(consVar, consVarPredictStep);

	AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
	    CheckStatesValid(consVar, cell_range),
	    "[stage 2] Non-realizable states produced. This should not happen!");

	// Add source terms via operator splitting
	AddSourceTerms(consVar, consVar, cell_range);
}
#endif

#if 0
template <typename problem_t> void HyperbolicSystem<problem_t>::AdvanceTimestepSDC2(const double dt)
{
	// Use a second-order SDC method to advance the radiation subsystem,
	// continuing iterations until the nonlinear residual is below a given tolerance.

	const auto ppm_range = std::make_pair(-1 + nghost_, nx_ + 1 + nghost_);
	const auto cell_range = std::make_pair(nghost_, nx_ + nghost_);

	// Initialize data
	dt_ = dt;
	// ensure that consVarPredictStep_ is initialized
	CopyVars(consVar_, consVarPredictStep_, cell_range, nvars_);
	CopyVars(consVar_, consVarPredictStepPrev_, cell_range, nvars_);

	// begin SDC loop
	const double rtol = 1.0e-10; // relative tolerance for L1 residual
	const double normPrev = ComputeNorm(consVar_, cell_range, nvars_);
	const int maxIterationCount = 50;
	double res = NAN;
	int j = 0;
	for (; j < maxIterationCount; ++j) {

		// Step 0: Fill ghost zones and convert to primitive variables
		FillGhostZones(consVarPredictStepPrev_); // consVarPredictStepPrev_ == U^{t+1,k}
		ConservedToPrimitive(consVarPredictStepPrev_, std::make_pair(0, dim1_));

		// Step 1a: Compute transport terms using state U^{t+1,k}
		ReconstructStatesPLM(primVar_, x1LeftState_, x1RightState_, ppm_range, nvars_); // PPM is unstable for SDC2!
		ComputeFluxes(cell_range);
		ComputeFirstOrderFluxes(cell_range);

		// update advectionFluxes_ <- F(U^{t+1, k})
		SaveFluxes(cell_range);
		// update reactionTerms_ <- S(U^{t+1, k})
		ComputeSourceTermsExplicit(consVarPredictStepPrev_, reactionTerms_, cell_range);

		if (j == 0) {
			// these terms correspond to the previous timestep
			CopyVars(advectionFluxes_, advectionFluxesU0_, cell_range, nvars_);
			CopyVars(reactionTerms_, reactionTermsU0_, cell_range, nvars_);
		}

		// Add SDC source terms to advectionFluxes_ following Zingale et al., ApJ 886:105
		//  (2019).
		for (int n = 0; n < nvars_; ++n) {
			for (int i = cell_range.first; i < cell_range.second; ++i) {
				// advectionFluxes_ == F(U^{t+1,k})
				// advectionFluxesU0_ == F(U^{t})
				// reactionTerms_ == S(U^{t+1,k})
				// reactionTermsU0_ == S(U^{t})
				advectionFluxes_(i, j, k, n) =
				    0.5 * (advectionFluxesU0_(i, j, k, n) + advectionFluxes_(i, j, k, n) +
					   reactionTermsU0_(i, j, k, n) + reactionTerms_(i, j, k, n)) -
				    reactionTerms_(i, j, k, n);
			}
		}

		// Step 1b: Compute reaction terms with advectionFluxes_ as a source term.
		AddSourceTerms(consVar_, consVarPredictStep_, cell_range);

		// Step 2: Check for convergence of |U^{t+1, k+1} - U^{t+1, k}|
		res = ComputeResidual(consVarPredictStep_, consVarPredictStepPrev_, cell_range, nvars_) /
		      normPrev;

		if (res <= rtol) {
			break;
		}

		// Step 3: Save current iteration
		CopyVars(consVarPredictStep_, consVarPredictStepPrev_, cell_range, nvars_);
	}

	// AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
	//    CheckStatesValid(consVarPredictStep_, cell_range),
	//    "[step 1b] Non-realizable states produced. This should not happen!");

	amrex::Print() << "\tSDC2 iteration converged with residual " << res << " after " << j + 1
		       << " iterations.\n";
	AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
	    res <= rtol, "SDC2 iteration exceeded maximum iteration count, but did not converge.");

	// If converged, copy final solution to consVar_
	CopyVars(consVarPredictStep_, consVar_, cell_range, nvars_);

	// Adjust our clock
	time_ += dt_;
	dtPrev_ = dt_;
}
#endif

#endif // HYPERBOLIC_SYSTEM_HPP_
