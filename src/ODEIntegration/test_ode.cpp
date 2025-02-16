//==============================================================================
// TwoMomentRad - a radiation transport library for patch-based AMR codes
// Copyright 2020 Benjamin Wibking.
// Released under the MIT license. See LICENSE file included in the GitHub repo.
//==============================================================================
/// \file test_ode.cpp
/// \brief Defines a test problem for ODE integration.
///

#include "test_ode.hpp"
#include "radiation_system.hpp"

using amrex::Real;

constexpr double Tgas0 = 6000.;                    // K
constexpr double rho0 = 0.01 * hydrogen_mass_cgs_; // g cm^-3

struct ODEUserData {
  amrex::Real rho;
};

AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE auto cooling_function(Real const rho,
                                                               Real const T)
    -> Real {
  // use fitting function from Koyama & Inutsuka (2002)
  Real gamma_heat = 2.0e-26;
  Real lambda_cool = gamma_heat * (1.0e7 * std::exp(-114800. / (T + 1000.)) +
                                   14. * std::sqrt(T) * std::exp(-92. / T));
  Real rho_over_mh = rho / hydrogen_mass_cgs_;
  Real cooling_source_term =
      rho_over_mh * gamma_heat - (rho_over_mh * rho_over_mh) * lambda_cool;
  return cooling_source_term;
}

AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE auto
user_rhs(Real /*t*/, quokka::valarray<Real, 1> &y_data,
         quokka::valarray<Real, 1> &y_rhs, void *user_data) -> int {
  // unpack user_data
  auto *udata = static_cast<ODEUserData *>(user_data);
  Real rho = udata->rho;

  // compute temperature
  Real Eint = y_data[0];
  Real T = RadSystem<ODETest>::ComputeTgasFromEgas(rho, Eint);

  // compute cooling function
  y_rhs[0] = cooling_function(rho, T);
  return 0;
}

auto problem_main() -> int {
  // set up initial conditions
  const Real Eint0 = RadSystem<ODETest>::ComputeEgasFromTgas(rho0, Tgas0);
  const Real Edot0 = cooling_function(rho0, Tgas0);
  const Real tcool = std::abs(Eint0 / Edot0);
  const Real max_time = 10.0 * tcool;

  std::cout << "Initial temperature: " << Tgas0 << std::endl;
  std::cout << "Initial cooling time: " << tcool / seconds_in_year << std::endl;
  std::cout << "Initial edot = " << Edot0 << std::endl;

  // solve cooling
  ODEUserData user_data{rho0};
  quokka::valarray<Real, 1> y = {Eint0};
  quokka::valarray<Real, 1> abstol = 1.0e-20 * y;
  const Real rtol = 1.0e-4; // appropriate for RK12
  int steps_taken = 0;
  rk_adaptive_integrate(user_rhs, 0, y, max_time, &user_data, rtol, abstol, steps_taken);

  const Real Tgas = RadSystem<ODETest>::ComputeTgasFromEgas(rho0, y[0]);
  // for n_H = 0.01 cm^{-3} (for IK cooling function)
  const Real Teq = 160.52611612610758;
  const Real Terr_rel = std::abs(Tgas - Teq) / Teq;
  const Real reltol = 1.0e-4; // relative error tolerance

  std::cout << "Final temperature: " << Tgas << std::endl;
  std::cout << "Relative error: " << Terr_rel << std::endl;

  // Cleanup and exit
  int status = 0;
  if ((Terr_rel > reltol) || (std::isnan(Terr_rel))) {
    status = 1;
  }
  return status;
}
