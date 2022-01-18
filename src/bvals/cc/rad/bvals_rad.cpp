//========================================================================================
// Athena++ astrophysical MHD code
// Copyright(C) 2014 James M. Stone <jmstone@princeton.edu> and other code contributors
// Licensed under the 3-clause BSD License, see LICENSE file for details
//========================================================================================
//! \file bvals_rad.cpp
//  \brief implements boundary functions for Radiation variables in a derived class of the
//  CellCenteredBoundaryVariable base class

// C++ headers
#include <cmath>  // acos, atan2, cos, sin

// Athena++ headers
#include "bvals_rad.hpp"
#include "../../bvals.hpp"                       // BoundaryValues
#include "../../bvals_interfaces.hpp"            // BoundaryFlag
#include "../../../athena.hpp"                   // Real
#include "../../../coordinates/coordinates.hpp"  // Coordinates
#include "../../../mesh/mesh.hpp"                // MeshBlock

//----------------------------------------------------------------------------------------
// RadBoundaryVariable constructor
// Inputs:
//   pmb: pointer to containing MeshBlock
//   p_var: pointer to primitive/conserved array
//   p_coarse_var: pointer to primitive/conserved coarse array for refinement
//   flux_x: array of spatial flux arrays
// Notes:
//   Like with PassiveScalars but unlike with Hydro, primitive/conserved swapping is done
//       by code external to this class overwriting the stored p_var and p_coarse_var
//       pointers, so there are no analogues to Hydro::SwapHydroQuantity() and
//       Hydro::SelectCoarseBuffer().

RadBoundaryVariable::RadBoundaryVariable(MeshBlock *pmb, AthenaArray<Real> *p_var,
    AthenaArray<Real> *p_coarse_var, AthenaArray<Real> *flux_x, int num_zeta,
    int num_psi) :
    CellCenteredBoundaryVariable(pmb, p_var, p_coarse_var, flux_x),
    nzeta(num_zeta),
    npsi(num_psi),
    nang((nzeta + 2*NGHOST_RAD) * (npsi + 2*NGHOST_RAD)) {

  // Calculate index bounds
  zs = NGHOST_RAD;
  ze = nzeta + NGHOST_RAD - 1;
  ps = NGHOST_RAD;
  pe = npsi + NGHOST_RAD - 1;
  is = pmb->is;
  ie = pmb->ie;
  js = pmb->js;
  je = pmb->je;
  ks = pmb->ks;
  ke = pmb->ke;
  int il = is - NGHOST;
  int iu = ie + NGHOST;
  int jl = js == je ? js : js - NGHOST;
  int ju = js == je ? je : je + NGHOST;
  int kl = ks == ke ? ks : ks - NGHOST;
  int ku = ks == ke ? ke : ke + NGHOST;

  // Allocate memory for angles
  zetaf.NewAthenaArray(nzeta + 2 * NGHOST_RAD + 1);
  zetav.NewAthenaArray(nzeta + 2 * NGHOST_RAD);
  dzetaf.NewAthenaArray(nzeta + 2 * NGHOST_RAD);
  psif.NewAthenaArray(npsi + 2 * NGHOST_RAD + 1);
  psiv.NewAthenaArray(npsi + 2 * NGHOST_RAD);
  dpsif.NewAthenaArray(npsi + 2 * NGHOST_RAD);

  // Construct polar angles, equally spaced in cosine
  Real dczeta = -2.0 / nzeta;
  zetaf(zs) = 0.0;             // set north pole exactly
  zetaf(ze+1) = PI;            // set south pole exactly
  for (int l = zs+1; l <= (nzeta-1)/2+NGHOST_RAD; ++l) {
    Real czeta = 1.0 + (l - NGHOST_RAD) * dczeta;
    Real zeta = std::acos(czeta);
    zetaf(l) = zeta;                           // set northern active faces
    zetaf(ze+NGHOST_RAD+1-l) = PI - zeta;      // set southern active faces
  }
  if (nzeta%2 == 0) {
    zetaf(nzeta/2+NGHOST_RAD) = PI/2.0;  // set equator exactly if present
  }
  for (int l = zs-NGHOST_RAD; l <= zs-1; ++l) {
    zetaf(l) = -zetaf(2*NGHOST_RAD-l);                   // set northern ghost faces
    zetaf(ze+NGHOST_RAD+1-l) = 2.0*PI - zetaf(nzeta+l);  // set southern ghost faces
  }
  for (int l = zs-NGHOST_RAD; l <= ze+NGHOST_RAD; ++l) {
    zetav(l) = (zetaf(l+1) * std::cos(zetaf(l+1)) - std::sin(zetaf(l+1))
        - zetaf(l) * std::cos(zetaf(l)) + std::sin(zetaf(l))) / (std::cos(zetaf(l+1))
        - std::cos(zetaf(l)));
    dzetaf(l) = zetaf(l+1) - zetaf(l);
  }

  // Construct azimuthal angles, equally spaced
  Real dpsi = 2.0*PI / npsi;
  psif(ps) = 0.0;             // set origin exactly
  psif(pe+1) = 2.0*PI;        // set origin exactly
  for (int m = ps+1; m <= pe; ++m) {
    psif(m) = (m - NGHOST_RAD) * dpsi;  // set active faces
  }
  for (int m = ps-NGHOST_RAD; m <= ps-1; ++m) {
    psif(m) = psif(npsi+m) - 2.0*PI;                          // set beginning ghost faces
    psif(pe+NGHOST_RAD+1-m) = psif(2*NGHOST_RAD-m) + 2.0*PI;  // set end ghost faces
  }
  for (int m = ps-NGHOST_RAD; m <= pe+NGHOST_RAD; ++m) {
    psiv(m) = 0.5 * (psif(m) + psif(m+1));
    dpsif(m) = psif(m+1) - psif(m);
  }

  // Allocate memory for temporary geometric quantities
  AthenaArray<Real> e_g, e_a, e_cov_g, e_cov_a, omega_g, omega_a, nh_g;
  e_g.NewAthenaArray(4, 4);
  e_a.NewAthenaArray(4, 4);
  e_cov_g.NewAthenaArray(4, 4);
  e_cov_a.NewAthenaArray(4, 4);
  omega_g.NewAthenaArray(4, 4, 4);
  omega_a.NewAthenaArray(4, 4, 4);
  nh_g.NewAthenaArray(4, nang);

  // Calculate unit normal components in orthonormal frame
  for (int l = zs-NGHOST_RAD; l <= ze+NGHOST_RAD; ++l) {
    for (int m = ps-NGHOST_RAD; m <= pe+NGHOST_RAD; ++m) {
      int lm = AngleInd(l, m);
      nh_g(0,lm) = 1.0;
      nh_g(1,lm) = std::sin(zetav(l)) * std::cos(psiv(m));
      nh_g(2,lm) = std::sin(zetav(l)) * std::sin(psiv(m));
      nh_g(3,lm) = std::cos(zetav(l));
      if (nzeta == 1) {
        nh_g(1,l,m) *= 0.816496580927726;
        nh_g(2,l,m) *= 0.816496580927726;
      }
    }
  }

  // Allocate and calculate inner x^1 reflection transformation
  if (pbval_->block_bcs[BoundaryFace::inner_x1] == BoundaryFlag::reflect) {
    reflect_ind_ix1_.NewAthenaArray(4, nang, pmb->ncells3, pmb->ncells2, NGHOST);
    reflect_frac_ix1_.NewAthenaArray(4, nang, pmb->ncells3, pmb->ncells2, NGHOST);
    for (int k = kl; k <= ku; ++k) {
      for (int j = jl; j <= ju; ++j) {
        for (int di = 0; di < NGHOST; ++di) {
          int i_g = is - NGHOST + di;
          int i_a = is + NGHOST - 1 - di;
          Real x1_g = pmb->pcoord->x1v(i_g);
          Real x1_a = pmb->pcoord->x1v(i_a);
          Real x2 = pmb->pcoord->x2v(j);
          Real x3 = pmb->pcoord->x3v(k);
          pmb->pcoord->Tetrad(x1_g, x2, x3, e_g, e_cov_g, omega_g);
          pmb->pcoord->Tetrad(x1_a, x2, x3, e_a, e_cov_a, omega_a);
          for (int lm_g = 0; lm_g < nang; lm_g++) {
            Real n_g[4] = {};
            for (int n = 0; n < 4; ++n) {
              for (int p = 0; p < 4; ++p) {
                n_g[n] += e_g(p,n) * nh_g(p,lm_g);
              }
            }
            Real n_a[4] = {};
            for (int n = 0; n < 4; ++n) {
              n_a[n] = n_g[n];
            }
            n_a[1] *= -1.0;
            Real nh_a[4] = {};
            for (int n = 0; n < 4; ++n) {
              for (int p = 0; p < 4; ++p) {
                nh_a[n] += e_cov_a(n,p) * n_a[p];
              }
            }
            nh_a[0] *= -1.0;
            Real zeta_a = std::acos(nh_a[3] / nh_a[0]);
            Real psi_a = std::atan2(nh_a[2], nh_a[1]);
            psi_a += psi_a < 0.0 ? 2.0*PI : 0.0;
            int l_a;
            for (l_a = zs-1; l_a <= ze+1; ++l_a) {
              if (zetav(l_a) > zeta_a) {
                break;
              }
            }
            int m_a;
            for (m_a = ps-1; m_a <= pe+1; ++m_a) {
              if (psiv(m_a) > psi_a) {
                break;
              }
            }
            reflect_ind_ix1_(0,lm_g,k,j,di) = AngleInd(l_a - 1, m_a - 1);
            reflect_ind_ix1_(1,lm_g,k,j,di) = AngleInd(l_a - 1, m_a);
            reflect_ind_ix1_(2,lm_g,k,j,di) = AngleInd(l_a, m_a - 1);
            reflect_ind_ix1_(3,lm_g,k,j,di) = AngleInd(l_a, m_a);
            Real frac_l = (zeta_a - zetav(l_a-1)) / dzetaf(l_a-1);
            Real frac_m = (psi_a - psiv(m_a-1)) / dpsif(m_a-1);
            reflect_frac_ix1_(0,lm_g,k,j,di) = (1.0 - frac_l) * (1.0 - frac_m);
            reflect_frac_ix1_(1,lm_g,k,j,di) = (1.0 - frac_l) * frac_m;
            reflect_frac_ix1_(2,lm_g,k,j,di) = frac_l * (1.0 - frac_m);
            reflect_frac_ix1_(3,lm_g,k,j,di) = frac_l * frac_m;
          }
        }
      }
    }
  }

  // Allocate and calculate outer x^1 reflection transformation
  if (pbval_->block_bcs[BoundaryFace::outer_x1] == BoundaryFlag::reflect) {
    reflect_ind_ox1_.NewAthenaArray(4, nang, pmb->ncells3, pmb->ncells2, NGHOST);
    reflect_frac_ox1_.NewAthenaArray(4, nang, pmb->ncells3, pmb->ncells2, NGHOST);
    for (int k = kl; k <= ku; ++k) {
      for (int j = jl; j <= ju; ++j) {
        for (int di = 0; di < NGHOST; ++di) {
          int i_g = ie + 1 + di;
          int i_a = ie - di;
          Real x1_g = pmb->pcoord->x1v(i_g);
          Real x1_a = pmb->pcoord->x1v(i_a);
          Real x2 = pmb->pcoord->x2v(j);
          Real x3 = pmb->pcoord->x3v(k);
          pmb->pcoord->Tetrad(x1_g, x2, x3, e_g, e_cov_g, omega_g);
          pmb->pcoord->Tetrad(x1_a, x2, x3, e_a, e_cov_a, omega_a);
          for (int lm_g = 0; lm_g < nang; lm_g++) {
            Real n_g[4] = {};
            for (int n = 0; n < 4; ++n) {
              for (int p = 0; p < 4; ++p) {
                n_g[n] += e_g(p,n) * nh_g(p,lm_g);
              }
            }
            Real n_a[4] = {};
            for (int n = 0; n < 4; ++n) {
              n_a[n] = n_g[n];
            }
            n_a[1] *= -1.0;
            Real nh_a[4] = {};
            for (int n = 0; n < 4; ++n) {
              for (int p = 0; p < 4; ++p) {
                nh_a[n] += e_cov_a(n,p) * n_a[p];
              }
            }
            nh_a[0] *= -1.0;
            Real zeta_a = std::acos(nh_a[3] / nh_a[0]);
            Real psi_a = std::atan2(nh_a[2], nh_a[1]);
            psi_a += psi_a < 0.0 ? 2.0*PI : 0.0;
            int l_a;
            for (l_a = zs-1; l_a <= ze+1; ++l_a) {
              if (zetav(l_a) > zeta_a) {
                break;
              }
            }
            int m_a;
            for (m_a = ps-1; m_a <= pe+1; ++m_a) {
              if (psiv(m_a) > psi_a) {
                break;
              }
            }
            reflect_ind_ox1_(0,lm_g,k,j,di) = AngleInd(l_a - 1, m_a - 1);
            reflect_ind_ox1_(1,lm_g,k,j,di) = AngleInd(l_a - 1, m_a);
            reflect_ind_ox1_(2,lm_g,k,j,di) = AngleInd(l_a, m_a - 1);
            reflect_ind_ox1_(3,lm_g,k,j,di) = AngleInd(l_a, m_a);
            Real frac_l = (zeta_a - zetav(l_a-1)) / dzetaf(l_a-1);
            Real frac_m = (psi_a - psiv(m_a-1)) / dpsif(m_a-1);
            reflect_frac_ox1_(0,lm_g,k,j,di) = (1.0 - frac_l) * (1.0 - frac_m);
            reflect_frac_ox1_(1,lm_g,k,j,di) = (1.0 - frac_l) * frac_m;
            reflect_frac_ox1_(2,lm_g,k,j,di) = frac_l * (1.0 - frac_m);
            reflect_frac_ox1_(3,lm_g,k,j,di) = frac_l * frac_m;
          }
        }
      }
    }
  }

  // Allocate and calculate inner x^2 reflection transformation
  if (pbval_->block_bcs[BoundaryFace::inner_x2] == BoundaryFlag::reflect) {
    reflect_ind_ix2_.NewAthenaArray(4, nang, pmb->ncells3, NGHOST, pmb->ncells1);
    reflect_frac_ix2_.NewAthenaArray(4, nang, pmb->ncells3, NGHOST, pmb->ncells1);
    for (int k = kl; k <= ku; ++k) {
      for (int dj = 0; dj < NGHOST; ++dj) {
        int j_g = js - NGHOST + dj;
        int j_a = js + NGHOST - 1 - dj;
        for (int i = il; i <= iu; ++i) {
          Real x1 = pmb->pcoord->x1v(i);
          Real x2_g = pmb->pcoord->x2v(j_g);
          Real x2_a = pmb->pcoord->x2v(j_a);
          Real x3 = pmb->pcoord->x3v(k);
          pmb->pcoord->Tetrad(x1, x2_g, x3, e_g, e_cov_g, omega_g);
          pmb->pcoord->Tetrad(x1, x2_a, x3, e_a, e_cov_a, omega_a);
          for (int lm_g = 0; lm_g < nang; lm_g++) {
            Real n_g[4] = {};
            for (int n = 0; n < 4; ++n) {
              for (int p = 0; p < 4; ++p) {
                n_g[n] += e_g(p,n) * nh_g(p,lm_g);
              }
            }
            Real n_a[4] = {};
            for (int n = 0; n < 4; ++n) {
              n_a[n] = n_g[n];
            }
            n_a[2] *= -1.0;
            Real nh_a[4] = {};
            for (int n = 0; n < 4; ++n) {
              for (int p = 0; p < 4; ++p) {
                nh_a[n] += e_cov_a(n,p) * n_a[p];
              }
            }
            nh_a[0] *= -1.0;
            Real zeta_a = std::acos(nh_a[3] / nh_a[0]);
            Real psi_a = std::atan2(nh_a[2], nh_a[1]);
            psi_a += psi_a < 0.0 ? 2.0*PI : 0.0;
            int l_a;
            for (l_a = zs-1; l_a <= ze+1; ++l_a) {
              if (zetav(l_a) > zeta_a) {
                break;
              }
            }
            int m_a;
            for (m_a = ps-1; m_a <= pe+1; ++m_a) {
              if (psiv(m_a) > psi_a) {
                break;
              }
            }
            reflect_ind_ix2_(0,lm_g,k,dj,i) = AngleInd(l_a - 1, m_a - 1);
            reflect_ind_ix2_(1,lm_g,k,dj,i) = AngleInd(l_a - 1, m_a);
            reflect_ind_ix2_(2,lm_g,k,dj,i) = AngleInd(l_a, m_a - 1);
            reflect_ind_ix2_(3,lm_g,k,dj,i) = AngleInd(l_a, m_a);
            Real frac_l = (zeta_a - zetav(l_a-1)) / dzetaf(l_a-1);
            Real frac_m = (psi_a - psiv(m_a-1)) / dpsif(m_a-1);
            reflect_frac_ix2_(0,lm_g,k,dj,i) = (1.0 - frac_l) * (1.0 - frac_m);
            reflect_frac_ix2_(1,lm_g,k,dj,i) = (1.0 - frac_l) * frac_m;
            reflect_frac_ix2_(2,lm_g,k,dj,i) = frac_l * (1.0 - frac_m);
            reflect_frac_ix2_(3,lm_g,k,dj,i) = frac_l * frac_m;
          }
        }
      }
    }
  }

  // Allocate and calculate outer x^2 reflection transformation
  if (pbval_->block_bcs[BoundaryFace::outer_x2] == BoundaryFlag::reflect) {
    reflect_ind_ox2_.NewAthenaArray(4, nang, pmb->ncells3, NGHOST, pmb->ncells1);
    reflect_frac_ox2_.NewAthenaArray(4, nang, pmb->ncells3, NGHOST, pmb->ncells1);
    for (int k = kl; k <= ku; ++k) {
      for (int dj = 0; dj < NGHOST; ++dj) {
        int j_g = je + 1 + dj;
        int j_a = je - dj;
        for (int i = il; i <= iu; ++i) {
          Real x1 = pmb->pcoord->x1v(i);
          Real x2_g = pmb->pcoord->x2v(j_g);
          Real x2_a = pmb->pcoord->x2v(j_a);
          Real x3 = pmb->pcoord->x3v(k);
          pmb->pcoord->Tetrad(x1, x2_g, x3, e_g, e_cov_g, omega_g);
          pmb->pcoord->Tetrad(x1, x2_a, x3, e_a, e_cov_a, omega_a);
          for (int lm_g = 0; lm_g < nang; lm_g++) {
            Real n_g[4] = {};
            for (int n = 0; n < 4; ++n) {
              for (int p = 0; p < 4; ++p) {
                n_g[n] += e_g(p,n) * nh_g(p,lm_g);
              }
            }
            Real n_a[4] = {};
            for (int n = 0; n < 4; ++n) {
              n_a[n] = n_g[n];
            }
            n_a[2] *= -1.0;
            Real nh_a[4] = {};
            for (int n = 0; n < 4; ++n) {
              for (int p = 0; p < 4; ++p) {
                nh_a[n] += e_cov_a(n,p) * n_a[p];
              }
            }
            nh_a[0] *= -1.0;
            Real zeta_a = std::acos(nh_a[3] / nh_a[0]);
            Real psi_a = std::atan2(nh_a[2], nh_a[1]);
            psi_a += psi_a < 0.0 ? 2.0*PI : 0.0;
            int l_a;
            for (l_a = zs-1; l_a <= ze+1; ++l_a) {
              if (zetav(l_a) > zeta_a) {
                break;
              }
            }
            int m_a;
            for (m_a = ps-1; m_a <= pe+1; ++m_a) {
              if (psiv(m_a) > psi_a) {
                break;
              }
            }
            reflect_ind_ox2_(0,lm_g,k,dj,i) = AngleInd(l_a - 1, m_a - 1);
            reflect_ind_ox2_(1,lm_g,k,dj,i) = AngleInd(l_a - 1, m_a);
            reflect_ind_ox2_(2,lm_g,k,dj,i) = AngleInd(l_a, m_a - 1);
            reflect_ind_ox2_(3,lm_g,k,dj,i) = AngleInd(l_a, m_a);
            Real frac_l = (zeta_a - zetav(l_a-1)) / dzetaf(l_a-1);
            Real frac_m = (psi_a - psiv(m_a-1)) / dpsif(m_a-1);
            reflect_frac_ox2_(0,lm_g,k,dj,i) = (1.0 - frac_l) * (1.0 - frac_m);
            reflect_frac_ox2_(1,lm_g,k,dj,i) = (1.0 - frac_l) * frac_m;
            reflect_frac_ox2_(2,lm_g,k,dj,i) = frac_l * (1.0 - frac_m);
            reflect_frac_ox2_(3,lm_g,k,dj,i) = frac_l * frac_m;
          }
        }
      }
    }
  }

  // Allocate and calculate inner x^3 reflection transformation
  if (pbval_->block_bcs[BoundaryFace::inner_x3] == BoundaryFlag::reflect) {
    reflect_ind_ix3_.NewAthenaArray(4, nang, NGHOST, pmb->ncells2, pmb->ncells1);
    reflect_frac_ix3_.NewAthenaArray(4, nang, NGHOST, pmb->ncells2, pmb->ncells1);
    for (int dk = 0; dk < NGHOST; ++dk) {
      int k_g = ks - NGHOST + dk;
      int k_a = ks + NGHOST - 1 - dk;
      for (int j = jl; j <= ju; ++j) {
        for (int i = il; i <= iu; ++i) {
          Real x1 = pmb->pcoord->x1v(i);
          Real x2 = pmb->pcoord->x2v(j);
          Real x3_g = pmb->pcoord->x3v(k_g);
          Real x3_a = pmb->pcoord->x3v(k_a);
          pmb->pcoord->Tetrad(x1, x2, x3_g, e_g, e_cov_g, omega_g);
          pmb->pcoord->Tetrad(x1, x2, x3_a, e_a, e_cov_a, omega_a);
          for (int lm_g = 0; lm_g < nang; lm_g++) {
            Real n_g[4] = {};
            for (int n = 0; n < 4; ++n) {
              for (int p = 0; p < 4; ++p) {
                n_g[n] += e_g(p,n) * nh_g(p,lm_g);
              }
            }
            Real n_a[4] = {};
            for (int n = 0; n < 4; ++n) {
              n_a[n] = n_g[n];
            }
            n_a[3] *= -1.0;
            Real nh_a[4] = {};
            for (int n = 0; n < 4; ++n) {
              for (int p = 0; p < 4; ++p) {
                nh_a[n] += e_cov_a(n,p) * n_a[p];
              }
            }
            nh_a[0] *= -1.0;
            Real zeta_a = std::acos(nh_a[3] / nh_a[0]);
            Real psi_a = std::atan2(nh_a[2], nh_a[1]);
            psi_a += psi_a < 0.0 ? 2.0*PI : 0.0;
            int l_a;
            for (l_a = zs-1; l_a <= ze+1; ++l_a) {
              if (zetav(l_a) > zeta_a) {
                break;
              }
            }
            int m_a;
            for (m_a = ps-1; m_a <= pe+1; ++m_a) {
              if (psiv(m_a) > psi_a) {
                break;
              }
            }
            reflect_ind_ix3_(0,lm_g,dk,j,i) = AngleInd(l_a - 1, m_a - 1);
            reflect_ind_ix3_(1,lm_g,dk,j,i) = AngleInd(l_a - 1, m_a);
            reflect_ind_ix3_(2,lm_g,dk,j,i) = AngleInd(l_a, m_a - 1);
            reflect_ind_ix3_(3,lm_g,dk,j,i) = AngleInd(l_a, m_a);
            Real frac_l = (zeta_a - zetav(l_a-1)) / dzetaf(l_a-1);
            Real frac_m = (psi_a - psiv(m_a-1)) / dpsif(m_a-1);
            reflect_frac_ix3_(0,lm_g,dk,j,i) = (1.0 - frac_l) * (1.0 - frac_m);
            reflect_frac_ix3_(1,lm_g,dk,j,i) = (1.0 - frac_l) * frac_m;
            reflect_frac_ix3_(2,lm_g,dk,j,i) = frac_l * (1.0 - frac_m);
            reflect_frac_ix3_(3,lm_g,dk,j,i) = frac_l * frac_m;
          }
        }
      }
    }
  }

  // Allocate and calculate outer x^3 reflection transformation
  if (pbval_->block_bcs[BoundaryFace::outer_x2] == BoundaryFlag::reflect) {
    reflect_ind_ox3_.NewAthenaArray(4, nang, NGHOST, pmb->ncells2, pmb->ncells1);
    reflect_frac_ox3_.NewAthenaArray(4, nang, NGHOST, pmb->ncells2, pmb->ncells1);
    for (int dk = 0; dk < NGHOST; ++dk) {
      int k_g = ke + 1 + dk;
      int k_a = ke - dk;
      for (int j = jl; j <= ju; ++j) {
        for (int i = il; i <= iu; ++i) {
          Real x1 = pmb->pcoord->x1v(i);
          Real x2 = pmb->pcoord->x2v(j);
          Real x3_g = pmb->pcoord->x3v(k_g);
          Real x3_a = pmb->pcoord->x3v(k_a);
          pmb->pcoord->Tetrad(x1, x2, x3_g, e_g, e_cov_g, omega_g);
          pmb->pcoord->Tetrad(x1, x2, x3_a, e_a, e_cov_a, omega_a);
          for (int lm_g = 0; lm_g < nang; lm_g++) {
            Real n_g[4] = {};
            for (int n = 0; n < 4; ++n) {
              for (int p = 0; p < 4; ++p) {
                n_g[n] += e_g(p,n) * nh_g(p,lm_g);
              }
            }
            Real n_a[4] = {};
            for (int n = 0; n < 4; ++n) {
              n_a[n] = n_g[n];
            }
            n_a[3] *= -1.0;
            Real nh_a[4] = {};
            for (int n = 0; n < 4; ++n) {
              for (int p = 0; p < 4; ++p) {
                nh_a[n] += e_cov_a(n,p) * n_a[p];
              }
            }
            nh_a[0] *= -1.0;
            Real zeta_a = std::acos(nh_a[3] / nh_a[0]);
            Real psi_a = std::atan2(nh_a[2], nh_a[1]);
            psi_a += psi_a < 0.0 ? 2.0*PI : 0.0;
            int l_a;
            for (l_a = zs-1; l_a <= ze+1; ++l_a) {
              if (zetav(l_a) > zeta_a) {
                break;
              }
            }
            int m_a;
            for (m_a = ps-1; m_a <= pe+1; ++m_a) {
              if (psiv(m_a) > psi_a) {
                break;
              }
            }
            reflect_ind_ox3_(0,lm_g,dk,j,i) = AngleInd(l_a - 1, m_a - 1);
            reflect_ind_ox3_(1,lm_g,dk,j,i) = AngleInd(l_a - 1, m_a);
            reflect_ind_ox3_(2,lm_g,dk,j,i) = AngleInd(l_a, m_a - 1);
            reflect_ind_ox3_(3,lm_g,dk,j,i) = AngleInd(l_a, m_a);
            Real frac_l = (zeta_a - zetav(l_a-1)) / dzetaf(l_a-1);
            Real frac_m = (psi_a - psiv(m_a-1)) / dpsif(m_a-1);
            reflect_frac_ox3_(0,lm_g,dk,j,i) = (1.0 - frac_l) * (1.0 - frac_m);
            reflect_frac_ox3_(1,lm_g,dk,j,i) = (1.0 - frac_l) * frac_m;
            reflect_frac_ox3_(2,lm_g,dk,j,i) = frac_l * (1.0 - frac_m);
            reflect_frac_ox3_(3,lm_g,dk,j,i) = frac_l * frac_m;
          }
        }
      }
    }
  }

  // Allocate polar boundary remapping arrays
  if (pbval_->block_bcs[BoundaryFace::inner_x2] == BoundaryFlag::polar
      or pbval_->block_bcs[BoundaryFace::outer_x2] == BoundaryFlag::polar) {
    polar_vals_.NewAthenaArray(nang);
  }

  // Calculate north transformation
  if (pbval_->block_bcs[BoundaryFace::inner_x2] == BoundaryFlag::polar) {
    for (int k = kl; k <= ku; ++k) {
      for (int dj = 0; dj < NGHOST; ++dj) {
        int j = js - NGHOST + dj;
        for (int i = il; i <= iu; ++i) {
          Real x1 = pmb->pcoord->x1v(i);
          Real x2 = pmb->pcoord->x2v(j);
          Real x3 = pmb->pcoord->x3v(k);
          pmb->pcoord->Tetrad(x1, x2, x3, e_g, e_cov_g, omega_g);
          for (int lm_g = 0; lm_g < nang; lm_g++) {
            Real n_g[4] = {};
            for (int n = 0; n < 4; ++n) {
              for (int p = 0; p < 4; ++p) {
                n_g[n] += e_g(p,n) * nh_g(p,lm_g);
              }
            }
            Real n_a[4] = {};
            for (int n = 0; n < 4; ++n) {
              n_a[n] = n_g[n];
            }
            n_a[2] *= -1.0;
            n_a[3] *= -1.0;
            Real nh_a[4] = {};
            for (int n = 0; n < 4; ++n) {
              for (int p = 0; p < 4; ++p) {
                nh_a[n] += e_cov_a(n,p) * n_a[p];
              }
            }
            nh_a[0] *= -1.0;
            Real zeta_a = std::acos(nh_a[3] / nh_a[0]);
            Real psi_a = std::atan2(nh_a[2], nh_a[1]);
            psi_a += psi_a < 0.0 ? 2.0*PI : 0.0;
            int l_a;
            for (l_a = zs-1; l_a <= ze+1; ++l_a) {
              if (zetav(l_a) > zeta_a) {
                break;
              }
            }
            int m_a;
            for (m_a = ps-1; m_a <= pe+1; ++m_a) {
              if (psiv(m_a) > psi_a) {
                break;
              }
            }
            polar_ind_north_(0,lm_g,k,dj,i) = AngleInd(l_a - 1, m_a - 1);
            polar_ind_north_(1,lm_g,k,dj,i) = AngleInd(l_a - 1, m_a);
            polar_ind_north_(2,lm_g,k,dj,i) = AngleInd(l_a, m_a - 1);
            polar_ind_north_(3,lm_g,k,dj,i) = AngleInd(l_a, m_a);
            Real frac_l = (zeta_a - zetav(l_a-1)) / dzetaf(l_a-1);
            Real frac_m = (psi_a - psiv(m_a-1)) / dpsif(m_a-1);
            polar_frac_north_(0,lm_g,k,dj,i) = (1.0 - frac_l) * (1.0 - frac_m);
            polar_frac_north_(1,lm_g,k,dj,i) = (1.0 - frac_l) * frac_m;
            polar_frac_north_(2,lm_g,k,dj,i) = frac_l * (1.0 - frac_m);
            polar_frac_north_(3,lm_g,k,dj,i) = frac_l * frac_m;
          }
        }
      }
    }
  }

  // Calculate south transformation
  if (pbval_->block_bcs[BoundaryFace::outer_x2] == BoundaryFlag::polar) {
    for (int k = kl; k <= ku; ++k) {
      for (int dj = 0; dj < NGHOST; ++dj) {
        int j = je + 1 + dj;
        for (int i = il; i <= iu; ++i) {
          Real x1 = pmb->pcoord->x1v(i);
          Real x2 = pmb->pcoord->x2v(j);
          Real x3 = pmb->pcoord->x3v(k);
          pmb->pcoord->Tetrad(x1, x2, x3, e_g, e_cov_g, omega_g);
          for (int lm_g = 0; lm_g < nang; lm_g++) {
            Real n_g[4] = {};
            for (int n = 0; n < 4; ++n) {
              for (int p = 0; p < 4; ++p) {
                n_g[n] += e_g(p,n) * nh_g(p,lm_g);
              }
            }
            Real n_a[4] = {};
            for (int n = 0; n < 4; ++n) {
              n_a[n] = n_g[n];
            }
            n_a[2] *= -1.0;
            n_a[3] *= -1.0;
            Real nh_a[4] = {};
            for (int n = 0; n < 4; ++n) {
              for (int p = 0; p < 4; ++p) {
                nh_a[n] += e_cov_a(n,p) * n_a[p];
              }
            }
            nh_a[0] *= -1.0;
            Real zeta_a = std::acos(nh_a[3] / nh_a[0]);
            Real psi_a = std::atan2(nh_a[2], nh_a[1]);
            psi_a += psi_a < 0.0 ? 2.0*PI : 0.0;
            int l_a;
            for (l_a = zs-1; l_a <= ze+1; ++l_a) {
              if (zetav(l_a) > zeta_a) {
                break;
              }
            }
            int m_a;
            for (m_a = ps-1; m_a <= pe+1; ++m_a) {
              if (psiv(m_a) > psi_a) {
                break;
              }
            }
            polar_ind_south_(0,lm_g,k,dj,i) = AngleInd(l_a - 1, m_a - 1);
            polar_ind_south_(1,lm_g,k,dj,i) = AngleInd(l_a - 1, m_a);
            polar_ind_south_(2,lm_g,k,dj,i) = AngleInd(l_a, m_a - 1);
            polar_ind_south_(3,lm_g,k,dj,i) = AngleInd(l_a, m_a);
            Real frac_l = (zeta_a - zetav(l_a-1)) / dzetaf(l_a-1);
            Real frac_m = (psi_a - psiv(m_a-1)) / dpsif(m_a-1);
            polar_frac_south_(0,lm_g,k,dj,i) = (1.0 - frac_l) * (1.0 - frac_m);
            polar_frac_south_(1,lm_g,k,dj,i) = (1.0 - frac_l) * frac_m;
            polar_frac_south_(2,lm_g,k,dj,i) = frac_l * (1.0 - frac_m);
            polar_frac_south_(3,lm_g,k,dj,i) = frac_l * frac_m;
          }
        }
      }
    }
  }
}