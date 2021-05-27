#ifndef SIMULATION_HPP_ // NOLINT
#define SIMULATION_HPP_
//==============================================================================
// TwoMomentRad - a radiation transport library for patch-based AMR codes
// Copyright 2020 Benjamin Wibking.
// Released under the MIT license. See LICENSE file included in the GitHub repo.
//==============================================================================
/// \file simulation.cpp
/// \brief Implements classes and functions to organise the overall setup,
/// timestepping, solving, and I/O of a simulation.

// c++ headers

// library headers
#include "AMReX_BCRec.H"
#include "AMReX_BC_TYPES.H"
#include "AMReX_BLassert.H"
#include "AMReX_Config.H"
#include "AMReX_DistributionMapping.H"
#include "AMReX_INT.H"
#include "AMReX_ParallelDescriptor.H"
#include "AMReX_VisMF.H"
#include <AMReX_Geometry.H>
#include <AMReX_Gpu.H>
#include <AMReX_MultiFab.H>
#include <AMReX_ParmParse.H>
#include <AMReX_PlotFileUtil.H>
#include <AMReX_Print.H>
#include <AMReX_Utility.H>

// internal headers

using Real = amrex::Real;

AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE auto clamp(double v, double lo, double hi) -> double
{
	return (v < lo) ? lo : (hi < v) ? hi : v;
}

AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE void
CheckNaN(amrex::FArrayBox const &arr, amrex::Box const &indexRange, const int ncomp)
{
	// need to rewrite for GPU
	AMREX_ASSERT(!arr.contains_nan(indexRange, 0, ncomp));
}

// Simulation class should be initialized only once per program (i.e., is a singleton)
template <typename problem_t> class SingleLevelSimulation
{
      public:
	int nx_{400};
	int ny_{40};
	int nz_{1};
	int max_grid_size_{32};
	int maxTimesteps_;

	amrex::BoxArray simBoxArray_;
	amrex::Geometry simGeometry_;
	amrex::IntVect const domain_lo_{AMREX_D_DECL(0, 0, 0)};
	amrex::IntVect domain_hi_{AMREX_D_DECL(nx_ - 1, ny_ - 1, nz_ - 1)};
	amrex::Box domain_{domain_lo_, domain_hi_};

	// This defines the physical box, [-1,1] in each direction.
	amrex::RealBox real_box_{
	    {AMREX_D_DECL(amrex::Real(0.0), amrex::Real(0.0), amrex::Real(0.0))},
	    {AMREX_D_DECL(amrex::Real(1.0), amrex::Real(0.1), amrex::Real(1.0))}};

	// periodic in all directions
	amrex::Array<int, AMREX_SPACEDIM> is_periodic_{AMREX_D_DECL(1, 1, 1)};

	// boundary conditions object
	amrex::Vector<amrex::BCRec> boundaryConditions_;

	// How boxes are distributed among MPI processes
	amrex::DistributionMapping simDistributionMapping_;

	// we allocate two multifabs; one will store the old state, the other the new.
	amrex::MultiFab state_old_;
	amrex::MultiFab state_new_;
	amrex::MultiFab max_signal_speed_; // needed to compute CFL timestep

	// Nghost = number of ghost cells for each array
	int nghost_ = 4; // PPM needs nghost >= 3, PPM+flattening needs nghost >= 4
	// Ncomp = number of components for each array
	int ncomp_ = NAN; // == 5 for 3d Euler equations
	int ncompPrimitive_ = NAN; // for radiation, fewer primitive variables than conserved variables
	amrex::Vector<std::string> componentNames_;

	int plotfileInterval_ = 100; // write plotfile every 100 cycles
	bool outputAtInterval_ = false;

	// dx = cell size
	amrex::GpuArray<amrex::Real, AMREX_SPACEDIM> dx_{};

	amrex::Real dt_ = NAN;
	amrex::Real maxDt_ = std::numeric_limits<double>::max(); // default (no limit)
	amrex::Real tNow_ = 0.0;
	amrex::Real stopTime_ = 1.0;  // default
	amrex::Real cflNumber_ = 0.3; // default
	amrex::Long cycleCount_ = 0;
	bool areInitialConditionsDefined_ = false;

	SingleLevelSimulation(amrex::IntVect &gridDims, amrex::RealBox &boxSize,
			      amrex::Vector<amrex::BCRec> &boundaryConditions, const int ncomp)
	    : ncomp_(ncomp), ncompPrimitive_(ncomp)
	{
		initialize(gridDims, boxSize, boundaryConditions);
	}

	SingleLevelSimulation(amrex::IntVect &gridDims, amrex::RealBox &boxSize,
			      amrex::Vector<amrex::BCRec> &boundaryConditions, const int ncomp, const int ncompPrimitive)
	    : ncomp_(ncomp), ncompPrimitive_(ncompPrimitive)
	{
		initialize(gridDims, boxSize, boundaryConditions);
	}
	
	void initialize(amrex::IntVect &gridDims, amrex::RealBox &boxSize,
			      amrex::Vector<amrex::BCRec> &boundaryConditions) {
		// readParameters();

		// set grid dimension variables
		domain_hi_ = {AMREX_D_DECL(gridDims[0] - 1, gridDims[1] - 1, gridDims[2] - 1)};
		domain_ = {domain_lo_, domain_hi_};
		simBoxArray_.define(domain_);
		simBoxArray_.maxSize(max_grid_size_);

		// This defines a Geometry object
		real_box_ = boxSize;
		boundaryConditions_ = boundaryConditions;
		for (int i = 0; i < AMREX_SPACEDIM; ++i) {
			bool is_periodic_this_dim = true;
			// check whether each component has periodic boundary conditions
			for (int n = 0; n < ncomp_; ++n) {
				is_periodic_this_dim =
				    (is_periodic_this_dim &&
				     (boundaryConditions_[n].lo(i) == amrex::BCType::int_dir) &&
				     (boundaryConditions_[n].hi(i) == amrex::BCType::int_dir));
			}
			is_periodic_[i] = static_cast<int>(is_periodic_this_dim);
		}
		amrex::Print() << "periodicity: " << is_periodic_ << "\n";

		simGeometry_.define(domain_, real_box_, amrex::CoordSys::cartesian, is_periodic_);
		dx_ = simGeometry_.CellSizeArray();

		amrex::Print() << "isAllPeriodic() = " << simGeometry_.isAllPeriodic() << "\n";

		// initial DistributionMapping with boxarray
		simDistributionMapping_ = amrex::DistributionMapping(simBoxArray_);

		// initialize MultiFabs
		state_old_ =
		    amrex::MultiFab(simBoxArray_, simDistributionMapping_, ncomp_, nghost_);
		state_new_ =
		    amrex::MultiFab(simBoxArray_, simDistributionMapping_, ncomp_, nghost_);
		max_signal_speed_ =
		    amrex::MultiFab(simBoxArray_, simDistributionMapping_, 1, nghost_);
	}

	void readParameters();
	void evolve();
	void computeTimestep();
	// virtual auto computeTimestepLocal() -> amrex::Real = 0;
	virtual void computeMaxSignalLocal() = 0;
	virtual void setInitialConditions() = 0;
	virtual void advanceSingleTimestep() = 0;
};

template <typename problem_t> void SingleLevelSimulation<problem_t>::readParameters()
{
	// ParmParse is way of reading inputs from the inputs file
	amrex::ParmParse pp;

	// We need to get Nx, Ny, Nz (grid dimensions)
	pp.get("nx", nx_);
	pp.get("ny", ny_);
	pp.get("nz", nz_);

	// The domain is broken into boxes of size max_grid_size
	pp.get("max_grid_size", max_grid_size_);

	// Default nsteps to 10, allow us to set it to something else in the inputs file
	pp.query("max_timesteps", maxTimesteps_);

	// Default CFL number == 1.0, set to whatever is in the file
	pp.query("cfl", cflNumber_);
}

template <typename problem_t> void SingleLevelSimulation<problem_t>::computeTimestep()
{
	computeMaxSignalLocal();
	amrex::Real domain_signal_max = max_signal_speed_.norminf();
	amrex::Real dt_tmp = cflNumber_ * (dx_[0] / domain_signal_max);

	constexpr amrex::Real change_max = 1.1;
	amrex::Real dt_0 = dt_tmp;

	dt_tmp = std::min(dt_tmp, change_max * dt_);
	dt_0 = std::min(dt_0, dt_tmp);
	dt_0 = std::min(dt_0, maxDt_); // limit to maxDt_

	// Limit dt to avoid overshooting stop_time
	const amrex::Real eps = 1.e-3 * dt_0;

	if (tNow_ + dt_0 > stopTime_ - eps) {
		dt_0 = stopTime_ - tNow_;
	}

	dt_ = dt_0;
}

template <typename problem_t> void SingleLevelSimulation<problem_t>::evolve()
{
	// Main time loop
	AMREX_ASSERT(areInitialConditionsDefined_);
	amrex::Real start_time = amrex::ParallelDescriptor::second();

	for (int j = 0; j < maxTimesteps_; ++j) {
		if (tNow_ >= stopTime_) {
			break;
		}

		amrex::MultiFab::Copy(state_old_, state_new_, 0, 0, ncomp_, 0);

		computeTimestep();
		advanceSingleTimestep();
		tNow_ += dt_;
		++cycleCount_;

		if (outputAtInterval_ && ((cycleCount_ % plotfileInterval_) == 0)) {
			// output plotfile
			const std::string &pltfile = amrex::Concatenate("plt", cycleCount_, 5);
			amrex::WriteSingleLevelPlotfile(pltfile, state_new_, componentNames_,
							simGeometry_, tNow_, cycleCount_);
		}

		// print timestep information on I/O processor
		if (amrex::ParallelDescriptor::IOProcessor()) {
			amrex::Print()
			    << "Cycle " << j << "; t = " << tNow_ << "; dt = " << dt_ << "\n";
		}
	}

	// compute performance metric (microseconds/zone-update)
	amrex::Real elapsed_sec = amrex::ParallelDescriptor::second() - start_time;
	const int IOProc = amrex::ParallelDescriptor::IOProcessorNumber();
	amrex::ParallelDescriptor::ReduceRealMax(elapsed_sec, IOProc);

	if (amrex::ParallelDescriptor::IOProcessor()) {
		const double zone_cycles = cycleCount_ * (nx_ * ny_ * nz_);
		const double microseconds_per_update = 1.0e6 * elapsed_sec / zone_cycles;
		amrex::Print() << "Performance figure-of-merit: " << microseconds_per_update
			       << " μs/zone-update\n";
	}

	// output plotfile
	const std::string &pltfile = amrex::Concatenate("plt", cycleCount_, 5);
	amrex::WriteSingleLevelPlotfile(pltfile, state_new_, componentNames_, simGeometry_, tNow_,
					cycleCount_);
}

#endif // SIMULATION_HPP_