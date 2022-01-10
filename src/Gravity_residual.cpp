//==============================================================================
// Poisson gravity solver, adapted from Castro's gravity module:
//   Commit history:
//   https://github.com/AMReX-Astro/Castro/commits/main/Source/gravity/Gravity.cpp
// Used under the terms of the open-source license (BSD 3-clause) given here:
//   https://github.com/AMReX-Astro/Castro/blob/main/license.txt
//==============================================================================
/// \file gravity.cpp
/// \brief Implements a class for solving the Poisson equation.
///

#include <cmath>
#include <limits>
#include <memory>

#include <AMReX_FillPatchUtil.H>
#include <AMReX_MLMG.H>
#include <AMReX_MLPoisson.H>
#include <AMReX_ParmParse.H>

#include <Gravity.H>
#include "hydro_system.hpp"

using namespace amrex;

#ifdef AMREX_DEBUG
int Gravity::test_solves = 1;
#else
int Gravity::test_solves = 0;
#endif

void Gravity::test_residual(const Box &bx, Array4<Real> const &rhs,
                            Array4<Real> const &ecx, Array4<Real> const &ecy,
                            Array4<Real> const &ecz,
                            GpuArray<Real, AMREX_SPACEDIM> dx,
                            GpuArray<Real, AMREX_SPACEDIM> /*problo*/,
                            int coord_type) {
  // Test whether using the edge-based gradients
  // to compute Div(Grad(Phi)) satisfies Lap(phi) = RHS
  // Fill the RHS array with the residual

  AMREX_ALWAYS_ASSERT(coord_type == 0);

  amrex::ParallelFor(bx, [=] AMREX_GPU_HOST_DEVICE(int i, int j, int k) {
    Real lapphi = (ecx(i + 1, j, k) - ecx(i, j, k)) / dx[0];
    lapphi += (ecy(i, j + 1, k) - ecy(i, j, k)) / dx[1];
    lapphi += (ecz(i, j, k + 1) - ecz(i, j, k)) / dx[2];
    rhs(i, j, k) -= lapphi;
  });
}

void Gravity::test_level_grad_phi_prev(int level) {
  BL_PROFILE("Gravity::test_level_grad_phi_prev()");

  // Fill the RHS for the solve
  MultiFab &S_old = LevelData[level]->get_old_data(State_Type);
  MultiFab Rhs(grids[level], dmap[level], 1, 0);
  MultiFab::Copy(Rhs, S_old, density_index, 0, 1, 0);

  const Geometry &geom = parent->Geom(level);

  // This is a correction for fully periodic domains only
  if (geom.isAllPeriodic()) {
    if (gravity::verbose > 1 && ParallelDescriptor::IOProcessor() &&
        mass_offset != 0.0) {
      std::cout << " ... subtracting average density from RHS at level ... "
                << level << " " << mass_offset << std::endl;
    }
    Rhs.plus(-mass_offset, 0, 1, 0);
  }

  Rhs.mult(Ggravity);

  if (gravity::verbose > 1) {
    Real rhsnorm = Rhs.norm0();
    amrex::Print() << "... test_level_grad_phi_prev at level " << level
                   << std::endl;
    amrex::Print() << "       norm of RHS             " << rhsnorm << std::endl;
  }

  auto dx = parent->Geom(level).CellSizeArray();
  auto problo = parent->Geom(level).ProbLoArray();
  const int coord_type = geom.Coord();

  for (MFIter mfi(Rhs, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
    const Box &bx = mfi.tilebox();

    test_residual(bx, Rhs.array(mfi), (*grad_phi_prev[level][0]).array(mfi),
                  (*grad_phi_prev[level][1]).array(mfi),
                  (*grad_phi_prev[level][2]).array(mfi), dx, problo,
                  coord_type);
  }

  if (gravity::verbose > 1) {
    Real resnorm = Rhs.norm0();
    amrex::Print() << "       norm of residual        " << resnorm << std::endl;
  }
}

void Gravity::test_level_grad_phi_curr(int level) {
  BL_PROFILE("Gravity::test_level_grad_phi_curr()");

  // Fill the RHS for the solve
  MultiFab &S_new = LevelData[level]->get_new_data(State_Type);
  MultiFab Rhs(grids[level], dmap[level], 1, 0);
  MultiFab::Copy(Rhs, S_new, density_index, 0, 1, 0);

  const Geometry &geom = parent->Geom(level);

  // This is a correction for fully periodic domains only
  if (geom.isAllPeriodic()) {
    if (gravity::verbose > 1 && ParallelDescriptor::IOProcessor() &&
        mass_offset != 0.0) {
      std::cout << " ... subtracting average density from RHS in solve ... "
                << mass_offset << std::endl;
    }
    Rhs.plus(-mass_offset, 0, 1, 0);
  }

  Rhs.mult(Ggravity);

  if (gravity::verbose > 1) {
    Real rhsnorm = Rhs.norm0();
    if (ParallelDescriptor::IOProcessor()) {
      std::cout << "... test_level_grad_phi_curr at level " << level
                << std::endl;
      std::cout << "       norm of RHS             " << rhsnorm << std::endl;
    }
  }

  auto dx = geom.CellSizeArray();
  auto problo = geom.ProbLoArray();
  const int coord_type = geom.Coord();

  for (MFIter mfi(Rhs, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
    const Box &bx = mfi.tilebox();

    test_residual(bx, Rhs.array(mfi), (*grad_phi_curr[level][0]).array(mfi),
#if AMREX_SPACEDIM >= 2
                  (*grad_phi_curr[level][1]).array(mfi),
#endif
#if AMREX_SPACEDIM == 3
                  (*grad_phi_curr[level][2]).array(mfi),
#endif
                  dx, problo, coord_type);
  }

  if (gravity::verbose > 1) {
    Real resnorm = Rhs.norm0();
    amrex::Print() << "       norm of residual        " << resnorm << std::endl;
  }
}

void Gravity::test_composite_phi(int crse_level) {
  BL_PROFILE("Gravity::test_composite_phi()");

  if (gravity::verbose > 1 && ParallelDescriptor::IOProcessor()) {
    std::cout << "   " << '\n';
    std::cout << "... test_composite_phi at base level " << crse_level << '\n';
  }

  int finest_level_local = parent->finestLevel();
  int nlevels = finest_level_local - crse_level + 1;

  Vector<std::unique_ptr<MultiFab>> phi(nlevels);
  Vector<std::unique_ptr<MultiFab>> rhs(nlevels);
  Vector<std::unique_ptr<MultiFab>> res(nlevels);
  for (int ilev = 0; ilev < nlevels; ++ilev) {
    int amr_lev = crse_level + ilev;

    phi[ilev] = std::make_unique<MultiFab>(grids[amr_lev], dmap[amr_lev], 1, 1);
    MultiFab::Copy(*phi[ilev], LevelData[amr_lev]->get_new_data(PhiGrav_Type),
                   0, 0, 1, 1);

    rhs[ilev] = std::make_unique<MultiFab>(grids[amr_lev], dmap[amr_lev], 1, 1);
    MultiFab::Copy(*rhs[ilev], LevelData[amr_lev]->get_new_data(State_Type),
                   density_index, 0, 1, 0);

    res[ilev] = std::make_unique<MultiFab>(grids[amr_lev], dmap[amr_lev], 1, 0);
    res[ilev]->setVal(0.);
  }

  Real time = LevelData[crse_level]->get_state_data(PhiGrav_Type).curTime();

  Vector<Vector<MultiFab *>> grad_phi_null;
  solve_phi_with_mlmg(crse_level, finest_level_local, amrex::GetVecOfPtrs(phi),
                      amrex::GetVecOfPtrs(rhs), grad_phi_null,
                      amrex::GetVecOfPtrs(res), time);

  // Average residual from fine to coarse level before printing the norm
  for (int amr_lev = finest_level_local - 1; amr_lev >= 0; --amr_lev) {
    const IntVect &ratio = parent->refRatio(amr_lev);
    int ilev = amr_lev - crse_level;
    amrex::average_down(*res[ilev + 1], *res[ilev], 0, 1, ratio);
  }

  for (int amr_lev = crse_level; amr_lev <= finest_level_local; ++amr_lev) {
    Real resnorm = res[amr_lev]->norm0();
    if (ParallelDescriptor::IOProcessor()) {
      std::cout << "      ... norm of composite residual at level " << amr_lev
                << "  " << resnorm << '\n';
    }
  }
  if (ParallelDescriptor::IOProcessor()) {
    std::cout << std::endl;
  }
}
