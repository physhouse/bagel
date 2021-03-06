//
// BAGEL - Parallel electron correlation program.
// Filename: pfmm.cc
// Copyright (C) 2015 Toru Shiozaki
//
// Author: Hai-Anh Le <anh@u.northwestern.edu>
// Maintainer: Shiozaki group
//
// This file is part of the BAGEL package.
//
// The BAGEL package is free software; you can redistribute it and/or modify
// it under the terms of the GNU Library General Public License as published by
// the Free Software Foundation; either version 3, or (at your option)
// any later version.
//
// The BAGEL package is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Library General Public License for more details.
//
// You should have received a copy of the GNU Library General Public License
// along with the BAGEL package; see COPYING.  If not, write to
// the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
//


#include <src/periodic/pfmm.h>
#include <boost/math/special_functions/expint.hpp>

using namespace std;
using namespace bagel;

const static Legendre plm;
const static Factorial f;
const static Gamma_scaled sgamma;

const static double beta__ = sqrt(pi__); // convergence parameter

PFMM::PFMM(shared_ptr<const Lattice> lattice, const tuple<int, int, double, bool, int>& fmmp, const bool dodf, std::shared_ptr<StackMem> stack)
  : lattice_(lattice), dodf_(dodf), lmax_(get<0>(fmmp)), ws_(get<1>(fmmp)), beta_(get<2>(fmmp) * beta__) {

  scell_ = make_shared<const SimulationCell>(lattice->primitive_cell(), get<0>(fmmp));
  const bool ewald = get<3>(fmmp);
  if (ewald) {
    if (stack == nullptr) {
      stack_ = resources__->get();
      allocated_here_ = true;
    } else {
      stack_ = stack;
      allocated_here_ = false;
    }
  }

  ndim_ = scell_->ndim();
  if (ndim_ > 3 || ndim_ < 1)
    throw runtime_error("System must be periodic in 1-, 2-, or 3-D");

  msize_ = (2*lmax_ + 1) * (2*lmax_ + 1);
  osize_ = (lmax_ + 1) * (lmax_ + 1);
  max_rank_ = (lmax_ * 2) + 1;
  thresh_ = lattice->thresh();

  primvecs_.resize(3);
  for (int i = 0; i != ndim_; ++i)
    primvecs_[i] = scell_->primitive_vectors(i);
  for (int i = ndim_; i != 3; ++i)
    primvecs_[i] = {{0.0, 0.0, 0.0}};

  extent_sum_ = ewald ? get<4>(fmmp) : 0;
  if (ewald) {
    compute_Mlm();
    stack_->release(size_allocated_, buff_);
    resources__->release(stack_);
  } else {
    compute_Mlm_direct();
    //compute_Mlm_slow();
  }
}


bool PFMM::is_in_cff(array<double, 3> L) {

  const double extent = scell_->extent();

  const double rsq = L[0]*L[0] + L[1]*L[1] + L[2]*L[2];
  const bool out = (rsq > 2.0 * (1 + ws_) *  extent) ? true : false;

  return out;
}


void PFMM::compute_Mlm_direct() {

  // M* = sum of M in [-1, 1]
  const int n0 = pow(3, ndim_);
  vector<array<int, 3>> vidx0 = generate_vidx(1);
  std::sort(vidx0.begin(), vidx0.end(), sort_vector);
  assert(vidx0.size() == n0);

  vector<complex<double>> mstar(osize_, 0.0);

  for (int n = 0; n != n0; ++n) {
    array<int, 3> idx = vidx0[n];
    array<double, 3> mvec;
    mvec[0] = -(idx[0] * primvecs_[0][0] + idx[1] * primvecs_[1][0] + idx[2] * primvecs_[2][0]);
    mvec[1] = -(idx[0] * primvecs_[0][1] + idx[1] * primvecs_[1][1] + idx[2] * primvecs_[2][1]);
    mvec[2] = -(idx[0] * primvecs_[0][2] + idx[1] * primvecs_[1][2] + idx[2] * primvecs_[2][2]);
    const double rsq = mvec[0] * mvec[0] + mvec[1] * mvec[1] + mvec[2] * mvec[2];
    if (rsq > numerical_zero__) {
      const double r = sqrt(rsq);
      const double ctheta = mvec[2]/r;
      const double phi = atan2(mvec[1], mvec[0]);

      for (int l = 0; l <= lmax_; ++l) {
        for (int m = 0; m <= 2 * l; ++m) {
          const int am = abs(m - l);
          const int imul = l * l + m;

          double plm_tilde = plm.compute(l, am, ctheta) * pow(r, l);
          double ft = 1.0;
          for (int i = 1; i <= l + am; ++i) {
            plm_tilde /= ft;
            ft += 1.0;
          }

          const double sign = (m - l >= 0) ? 1.0 : -1.0;
          const double real = sign * cos(-am * phi) * plm_tilde;
          const double imag = sin(-am * phi) * plm_tilde;
          mstar[imul] += complex<double>(real, imag);
        }
      }
    }
  }

  // get L* = sum of L in FF'
  const int ws1 = 3 * ws_ + 1;
  const int n1 = pow(2*ws1+1, ndim_);
  vector<array<int, 3>> tmp = generate_vidx(ws1);
  assert(tmp.size() == n1);
  std::sort(tmp.begin(), tmp.end(), sort_vector);

  vector<array<int, 3>> vidx1;
  for (int n = 0; n != n1; ++n) {
    array<int, 3> idx = tmp[n];
    if (abs(idx[0]) > ws_ || abs(idx[1]) > ws_ || abs(idx[2]) > ws_)
      vidx1.push_back(idx);
  }

  const int nvec = vidx1.size();
  vector<complex<double>> lstar(msize_, 0.0);

  for (int ivec = 0; ivec != nvec; ++ivec) {
    array<int, 3> idx = vidx1[ivec];
    array<double, 3> mvec;
    mvec[0] = idx[0] * primvecs_[0][0] + idx[1] * primvecs_[1][0] + idx[2] * primvecs_[2][0];
    mvec[1] = idx[0] * primvecs_[0][1] + idx[1] * primvecs_[1][1] + idx[2] * primvecs_[2][1];
    mvec[2] = idx[0] * primvecs_[0][2] + idx[1] * primvecs_[1][2] + idx[2] * primvecs_[2][2];

    const double rsq = mvec[0] * mvec[0] + mvec[1] * mvec[1] + mvec[2] * mvec[2];
    if (rsq > numerical_zero__) {
    const double r = sqrt(rsq);
    const double ctheta = mvec[2]/r;
    const double phi = atan2(mvec[1], mvec[0]);

    for (int l = 0; l < max_rank_; ++l) {
      for (int m = 0; m <= 2 * l; ++m) {
        const int am = abs(m - l);
        const int imul = l * l + m;

        double plm_tilde = plm.compute(l, am, ctheta) / pow(r, l+1);
        double ft = 1.0;
        for (int i = 1; i <= l - am; ++i) {
          plm_tilde *= ft;
          ft += 1.0;
        }

        const double sign = (m - l >=0) ? 1.0 : -1.0;
        const double real = sign * cos(am * phi) * plm_tilde;
        const double imag = sin(am * phi) * plm_tilde;
        lstar[imul] += complex<double>(real, imag);
      }
    }
    }
  }


  // Mlm(n)
  mlm_ = lstar; // iter 0
  const int max_iter = 16;
  for (int n = 1; n <= max_iter; ++n) {
    vector<complex<double>> previous(msize_);
    for (int l = 0; l < max_rank_; ++l) {
      for (int m = 0; m <= 2*l; ++m) {
        const int im0 = l * l + m;
        previous[im0] = mlm_[im0] / pow(3.0, l+1);
        mlm_[im0] = 0.0;
      }
    }

    for (int l = 0; l < max_rank_; ++l) {
      for (int m = 0; m <= 2*l; ++m) {
        const int im0 = l * l + m;
        for (int j = 0; j <= lmax_ - l; ++j) {
          for (int k = 0; k <= 2*j; ++k) {
            const int im1 = j * j + k;
            const int im = (l+j)*(l+j) + m + k;
            assert(l + j < max_rank_);
            mlm_[im0] += previous[im] * mstar[im1];
          }
        }
        mlm_[im0] += lstar[im0];
      }
    }
    mlm_[0] = 0.0;
  }

#if 0
  // DEBUG
  cout << "RESULTS FROM DIRECT SUMMATION" << endl;
  for (int l = 0; l < max_rank_; ++l)
    for (int m = 0; m <= l; ++m) { // Slm = -sl-m
      const int imul = l * l + m + l;
      const double tmp = mlm_[imul].real();
      //if (abs(tmp) > 1e-8)
      if (l % 2 == 0 && m % 4 == 0)
        cout << "l = " << l << "  m = " << m << "  mlm = " << setw(20) << scientific << setprecision(14) << tmp << endl;
    }
  cout << " ******* END ******* " << endl;
  // END DEBUG
#endif
}


void PFMM::compute_Mlm() { // rectangular scell for now

  assert(lmax_ <= 25); // ERIRootList
  mlm_.clear();
  mlm_.resize(msize_);
  const double pibeta = pi__ * pi__ / (beta_ * beta_);
  const int nvec = pow(2*extent_sum_+1, ndim_);
//  allocate_arrays(10);
  allocate_arrays(nvec);
  vector<array<int, 3>> vidx = generate_vidx(extent_sum_);
  assert(vidx.size() == nvec);
  std::sort(vidx.begin(), vidx.end(), sort_vector); // sort to sum spherically

  for (int ivec = 0; ivec != nvec; ++ivec) {
    const int pos = ivec * 3;
    array<int, 3> idx = vidx[ivec];
    rvec_[pos    ] = idx[0] * primvecs_[0][0] + idx[1] * primvecs_[1][0] + idx[2] * primvecs_[2][0];
    rvec_[pos + 1] = idx[0] * primvecs_[0][1] + idx[1] * primvecs_[1][1] + idx[2] * primvecs_[2][1];
    rvec_[pos + 2] = idx[0] * primvecs_[0][2] + idx[1] * primvecs_[1][2] + idx[2] * primvecs_[2][2];
    Rsq_[ivec] = rvec_[pos]*rvec_[pos] + rvec_[pos+1]*rvec_[pos+1] + rvec_[pos+2]*rvec_[pos+2];
    T_[ivec] = Rsq_[ivec] * beta_ * beta_;
  }

  for (int l = 0; l < max_rank_; ++l) {
    root_weight(l, nvec);
    const int rank = l + 1;

    for (int ivec = 0; ivec != nvec; ++ivec) {
      if (Rsq_[ivec] > numerical_zero__) {
        array<int, 3> idx = vidx[ivec];

        const int pos = ivec * 3;
        const double r = sqrt(Rsq_[ivec]);
        const double ctheta = rvec_[pos+2]/r;
        const double phi = atan2(rvec_[pos+1], rvec_[pos]);
        const double* croots = roots_ + ivec * rank;
        const double* cweights = weights_ + ivec * rank;

        double glower = 0.0;
        if (l == 0) {
          for (int i = 0; i != rank; ++i)
            glower += cweights[i];
        } else {
          for (int i = 0; i != rank; ++i)
            glower += cweights[i] * pow(croots[i], l);
        }

        glower *= 2.0 * pow(beta_, 2*l+1) * sgamma(l, r);
        const double gupper = 1.0 / pow(r, l+1.0) - glower;

        for (int mm = 0; mm <= 2 * l; ++mm) {
          const int m = mm - l;
          const int am = abs(m);
          const int imul = l * l + mm;

          double plm_tilde = plm.compute(l, am, ctheta);
          double ft = 1.0;
          for (int i = 1; i <= l - am; ++i) {
            plm_tilde *= ft;
            ft += 1.0;
          }
          const double sign = (m >=0) ? 1.0 : -1.0;

          if (abs(idx[0]) > ws_ || abs(idx[1]) > ws_ || abs(idx[2]) > ws_) {
            // real term
            const double real = gupper * sign * cos(am * phi) * plm_tilde;
            const double imag = gupper * sin(am * phi) * plm_tilde;
            mlm_[imul] += complex<double>(real, imag);
          } else {
            // substract smooth part within ws_
            const double real = glower * sign * cos(am * phi) * plm_tilde;
            const double imag = glower * sin(am * phi) * plm_tilde;
            mlm_[imul] -= complex<double>(real, imag);
          }
        }
      }
    }
  }

  double volume = 0.0;
  vector<array<double, 3>> primkvecs(3);
  switch (ndim_) {
    case 1:
      {
        const double a1sq = dot(primvecs_[0], primvecs_[0]);
        for (int i = 0; i != 3; ++i)
          primkvecs[0][i] = primvecs_[0][i] / a1sq;
        volume = sqrt(a1sq);
        break;
      }
    case 2:
      {
        array<double, 3> a12 = cross(primvecs_[0], primvecs_[1]);
        const double a12sq = dot(a12, a12);
        volume = sqrt(a12sq);
        const double scale = 1.0 / a12sq;
        primkvecs[0] = cross(primvecs_[1], a12, scale);
        primkvecs[1] = cross(a12, primvecs_[0], scale);
        break;
      }
    case 3:
      {
        array<double, 3> a23 = cross(primvecs_[1], primvecs_[2]);
        volume = dot(primvecs_[0], a23);
        const double scale = 1.0 / volume;
        primkvecs[0] = cross(primvecs_[1], primvecs_[2], scale);
        primkvecs[1] = cross(primvecs_[2], primvecs_[0], scale);
        primkvecs[2] = cross(primvecs_[0], primvecs_[1], scale);
        break;
      }
  }

  for (int i = ndim_; i != 3; ++i)
    primkvecs[i] = {{0.0, 0.0, 0.0}};

  for (int ivec = 0; ivec != nvec; ++ivec) {
    array<int, 3> idx = vidx[ivec];
    array<double, 3> kvec;
    kvec[0] = idx[0] * primkvecs[0][0] + idx[1] * primkvecs[1][0] + idx[2] * primkvecs[2][0];
    kvec[1] = idx[0] * primkvecs[0][1] + idx[1] * primkvecs[1][1] + idx[2] * primkvecs[2][1];
    kvec[2] = idx[0] * primkvecs[0][2] + idx[1] * primkvecs[1][2] + idx[2] * primkvecs[2][2];
    const double rsq = kvec[0]*kvec[0] + kvec[1]*kvec[1] + kvec[2]*kvec[2];
    const double x = rsq * pibeta;
    if (rsq > numerical_zero__) {
      const double r = sqrt(rsq);
      const double ctheta = kvec[2]/r;
      const double phi = atan2(kvec[1], kvec[0]);
      double gamma = 0.0;
      if (ndim_ == 3) {
        gamma = exp(-x);
      } else if (ndim_ == 2) {
        gamma = sqrt(pi__) * erfc(sqrt(x));
      } else {
        gamma = -1.0 * boost::math::expint(-x);
      }
      const double prefact = pow(pi__, 1-ndim_/2.0) * pow(r, 1.0-ndim_) * gamma / volume;
      for (int l = 0; l < max_rank_; ++l) {
        const complex<double> coeffl = std::pow(complex<double>(0.0, 1.0), l) * pow(pi__, l) * sgamma(l, r);

        for (int mm = 0; mm <= 2 * l; ++mm) {
          const int m = mm - l;
          const int am = abs(m);
          const int imul = l * l + mm;

          double plm_tilde = plm.compute(l, am, ctheta);
          double ft = 1.0;
          for (int i = 1; i <= l - am; ++i) {
            plm_tilde *= ft;
            ft += 1.0;
          }

          const double sign = (m >=0) ? 1.0 : -1.0;

          // smooth term
          double real = sign * cos(am * phi);
          double imag = sin(am * phi);
          mlm_[imul] += prefact * coeffl * plm_tilde * complex<double>(real, imag);
        }
      }
    }
  }

#if 0
  // DEBUG
  cout << "RESULTS FROM EWALD SUMMATION" << endl;
  for (int l = 0; l < max_rank_; ++l)
    for (int m = 0; m <= l; ++m) { // Mlm = -Ml-m
      const int imul = l * l + m + l;
      const double mlm = mlm_[imul].real();
      //if (abs(mlm) > 1e-8)
      if (l % 2 == 0 && m % 4 == 0)
        cout << "l = " << l << "  m = " << m << "  mlm = " << setw(25) << scientific << setprecision(14) << mlm << endl;
    }
  // END DEBUG
#endif
}


void PFMM::root_weight(const int l, const int size) {
  if (l == 0) {
    for (int i = 0; i != size; ++i) {
      if (abs(T_[i]) < thresh_) {
        weights_[i] = 1.0;
      } else {
        const double sqrtt = sqrt(T_[i]);
        const double erfsqt = erf(sqrtt);
        weights_[i] = erfsqt * sqrt(pi__) * 0.5 / sqrtt;
      }
    }
  } else {
    const int rank = l + 1;
    eriroot__.root(rank, T_, roots_, weights_, size);
  }
}


void PFMM::allocate_arrays(const size_t ps) {

  size_allocated_ = (max_rank_ * 2 + 5) * ps;
  buff_ = stack_->get(size_allocated_);
  double* pointer = buff_;

  rvec_  = pointer;   pointer += ps * 3;
  Rsq_ = pointer;       pointer += ps;
  T_ = pointer;       pointer += ps;
  roots_ = pointer;   pointer += max_rank_ * ps;
  weights_ = pointer; pointer += max_rank_ * ps;
}


vector<shared_ptr<const ZMatrix>> PFMM::compute_multipoles(shared_ptr<const Geometry> geom0, shared_ptr<const Geometry> geom1) const {

  vector<shared_ptr<const ZMatrix>> out(osize_);

  const size_t nbasis = geom0->nbasis();
  vector<shared_ptr<ZMatrix>> multipoles(osize_);
  for (int i = 0; i != osize_; ++i)
    multipoles[i] = make_shared<ZMatrix>(nbasis, nbasis);

  vector<shared_ptr<const Atom>> atoms0 = geom0->atoms();
  vector<shared_ptr<const Atom>> atoms1 = geom1->atoms();
  size_t ob0 = 0;
  for (auto& atom0 : atoms0) {
    for (auto& b0 : atom0->shells()) {
      size_t ob1 = 0;
      for (auto& atom1 : atoms1) {
        for (auto& b1 : atom1->shells()) {
          MultipoleBatch mpole(array<shared_ptr<const Shell>, 2>{{b1, b0}}, geom0->charge_center(), lmax_);
          mpole.compute();
          for (int i = 0; i != osize_; ++i)
            multipoles[i]->copy_block(ob1, ob0, b1->nbasis(), b0->nbasis(), mpole.data(i));

          ob1 += b1->nbasis();
        }
      }
      ob0 += b0->nbasis();
    }
  }

  for (int i = 0; i != osize_; ++i)
    out[i] = make_shared<const ZMatrix>(*multipoles[i]);

  return out;
}


shared_ptr<const PData> PFMM::compute_far_field(shared_ptr<const PData> density) const {

  // sums over L and m have extent ws_ for now
  const int nvec = pow(2*ws_+1, ndim_);
  vector<array<int, 3>> vidx = generate_vidx(ws_);
  assert(vidx.size() == nvec);

  // compute Olm(m), contract with density D_ab(m), and sum over m
  const size_t nbas = scell_->nbasis();
  vector<complex<double>> olm(osize_);

  vector<shared_ptr<const ZMatrix>> nai(nvec);
  for (int ivec = 0; ivec != nvec; ++ivec) { // m
    array<int, 3> idx = vidx[ivec];
    // compute multipoles (0|Olm|m)
    array<double, 3> mvec;
    mvec[0] = idx[0] * primvecs_[0][0] + idx[1] * primvecs_[1][0] + idx[2] * primvecs_[2][0];
    mvec[1] = idx[0] * primvecs_[0][1] + idx[1] * primvecs_[1][1] + idx[2] * primvecs_[2][1];
    mvec[2] = idx[0] * primvecs_[0][2] + idx[1] * primvecs_[1][2] + idx[2] * primvecs_[2][2];
    auto cell = make_shared<const Geometry>(*scell_->geom(), mvec);
    vector<shared_ptr<const ZMatrix>> olm_ab_m = compute_multipoles(scell_->geom(), cell);

    if (density) {
      shared_ptr<const ZMatrix> ffden = density->pdata(ivec);
      for (int i = 0; i != osize_; ++i) {
        complex<double> olm_m = 0.0;
        for (int a = 0; a != nbas; ++a)
          for (int b = 0; b != nbas; ++b)
            olm_m += olm_ab_m[i]->element(b, a) * ffden->element(b, a);
        olm[i] += olm_m;
      }
    }

    // NAI
    auto tmpnai = make_shared<ZMatrix>(nbas, nbas);
    for (auto& atom : cell->atoms())
      *tmpnai += -2.0 * atom->atom_charge() * mlm_.at(0) * *olm_ab_m.at(0); // 2*NAI
    nai[ivec] = make_shared<const ZMatrix>(*tmpnai);
  }

  vector<shared_ptr<const ZMatrix>> out(nvec);
  if (density) {
    // contract with Mlm
    vector<complex<double>> slm(osize_);
    for (int l = 0; l <= lmax_; ++l) {
      for (int m = 0; m <= 2*l; ++m) {
        const int im1 = l * l + m;

        complex<double> slmjk;
        for (int j = 0; j <= lmax_; ++j) {
          for (int k = 0; k <= 2*j; ++k) {
            const int im2 = j * j + k;
            const int im = (l+j)*(l+j) + m + k;
            slmjk += mlm_.at(im) * olm.at(im2);
          }
        }
        slm[im1] = pow(-1.0, l) * slmjk;
      }
    }

    for (int ivec = 0; ivec != nvec; ++ivec) { // L
      array<int, 3> idx = vidx[ivec];
      // re-compute multipoles (0|Olm|L)
      array<double, 3> lvec;
      lvec[0] = idx[0] * primvecs_[0][0] + idx[1] * primvecs_[1][0] + idx[2] * primvecs_[2][0];
      lvec[1] = idx[0] * primvecs_[0][1] + idx[1] * primvecs_[1][1] + idx[2] * primvecs_[2][1];
      lvec[2] = idx[0] * primvecs_[0][2] + idx[1] * primvecs_[1][2] + idx[2] * primvecs_[2][2];
      auto cell = make_shared<const Geometry>(*scell_->geom(), lvec);
      vector<shared_ptr<const ZMatrix>> olm_rs_L = compute_multipoles(scell_->geom(), cell);

      auto jrs_L = make_shared<ZMatrix>(nbas, nbas);
      for (int i = 0; i != osize_; ++i)
        *jrs_L += slm[i] * *olm_rs_L.at(i);

      out[ivec] = make_shared<const ZMatrix>(*jrs_L + *nai[ivec]);
    }
  } else {
    for (int ivec = 0; ivec != nvec; ++ivec) // L
      out[ivec] = make_shared<const ZMatrix>(*nai[ivec]);
  }

  return make_shared<const PData>(out);
}


shared_ptr<const PData> PFMM::compute_cfmm(shared_ptr<const PData> density) const { // within ws

  Timer time;
  const int nvec = pow(2*ws_+1, ndim_);
  // get density for supergeom = lattice: (ws = ncell in lattice)
  const size_t nbas = nvec * scell_->nbasis();
  shared_ptr<Matrix> superden;
  if (density)
    superden = make_shared<Matrix>(nbas, nbas);
  int offset = 0;
  const int blk0 = lattice_->central_cell();
  for (int i = 0; i != nvec; ++i, offset += scell_->nbasis()) {
    if (density) {
      superden->copy_block(offset, offset, scell_->nbasis(), scell_->nbasis(), *density->pdata(i)->get_real_part());
      superden->copy_block(offset, offset, scell_->nbasis(), scell_->nbasis(), *density->pdata(i)->get_real_part());
    }
  }

  // compute Coulomb from lattice tree
  shared_ptr<const ZMatrix> coulomb = lattice_->fmmtree()->fmm(superden, dodf_, 2.0/*nai*/, lattice_->schwarz_thresh());
  time.tick_print("  Compute NF Coulomb matrix");

  vector<shared_ptr<const ZMatrix>> out(nvec);
  offset = 0;
  for (int i = 0; i != nvec; ++i, offset += scell_->nbasis()) {
    auto tmp = coulomb->get_submatrix(blk0*scell_->nbasis(), offset, scell_->nbasis(), scell_->nbasis());
    out[i] = make_shared<const ZMatrix>(*tmp);
  }

  return make_shared<const PData>(out);
}


vector<array<int, 3>> PFMM::generate_vidx(const int n) const {

  const int nvec = pow(2*n+1, ndim_);
  vector<array<int, 3>> vidx(nvec);

  int cnt = 0;
  if (ndim_ == 3) {
    for (int n3 = -n; n3 <= n; ++n3)
      for (int n2 = -n; n2 <= n; ++n2)
        for (int n1 = -n; n1 <= n; ++n1, ++cnt)
          vidx[cnt] = {{n1, n2, n3}};
  } else if (ndim_ == 2) {
    for (int n2 = -n; n2 <= n; ++n2)
      for (int n1 = -n; n1 <= n; ++n1, ++cnt)
      vidx[cnt] = {{n1, n2, 0}};
  } else if (ndim_ == 1) {
    for (int n1 = -n; n1 <= n; ++n1, ++cnt)
      vidx[cnt] = {{n1, 0, 0}};
  }
  assert(cnt == nvec);

  return vidx;
}


shared_ptr<const PData> PFMM::pcompute_Jop(shared_ptr<const PData> density) const {
  shared_ptr<const PData> nf = compute_cfmm(density);
  shared_ptr<const PData> ff = compute_far_field(density);

  return make_shared<const PData>(*nf + *ff);
}


double PFMM::nuclear_repulsion_ff() const {

  const int nvec = pow(2*extent_sum_+1, ndim_);
  vector<array<int, 3>> vidx = generate_vidx(extent_sum_);
  assert(vidx.size() == nvec);

  double out = 0.0;
  vector<shared_ptr<const Atom>> atoms0 = scell_->geom()->atoms();
  int icell0 = 0;
  for (int i = 0; i != ndim_; ++i) icell0 += extent_sum_ * pow(2 * extent_sum_ + 1, i);
  for (int ivec = 0; ivec != nvec; ++ivec) {
    array<int, 3> idx = vidx[ivec];
    array<double, 3> disp;
    disp[0] = idx[0] * primvecs_[0][0] + idx[1] * primvecs_[1][0] + idx[2] * primvecs_[2][0];
    disp[1] = idx[0] * primvecs_[0][1] + idx[1] * primvecs_[1][1] + idx[2] * primvecs_[2][1];
    disp[2] = idx[0] * primvecs_[0][2] + idx[1] * primvecs_[1][2] + idx[2] * primvecs_[2][2];

    auto cell = make_shared<const Geometry>(*scell_->geom(), disp);
    vector<shared_ptr<const Atom>> atoms = cell->atoms();
    for (auto iter0 = atoms0.begin(); iter0 != atoms0.end(); ++iter0) {
      const double c0 = (*iter0)->atom_charge();
      auto ia0 = distance(atoms0.begin(), iter0);
      for (auto iter1 = atoms.begin(); iter1 != atoms.end(); ++iter1) {
        const double c = (*iter1)->atom_charge();
        auto ia1 = distance(atoms.begin(), iter1);
        if (ivec == icell0 && ia0 == ia1) continue;
        out += 0.5 * c0 * c / (*iter0)->distance(*iter1);
      }
    }
  }

  return out;
}


void PFMM::compute_Mlm_slow() {

  const size_t limit = 30;
  const int n1 = pow(2*limit+1, ndim_);
  vector<array<int, 3>> tmp = generate_vidx(limit);
  std::sort(tmp.begin(), tmp.end(), sort_vector);

  vector<array<int, 3>> vidx;
  for (int n = 0; n != n1; ++n) {
    array<int, 3> idx = tmp[n];
    if (abs(idx[0]) > ws_ || abs(idx[1]) > ws_ || abs(idx[2]) > ws_)
      vidx.push_back(idx);
  }
  const int nvec = vidx.size();

  vector<complex<double>> mlm(msize_, 0.0);

  for (int ivec = 0; ivec != nvec; ++ivec) {
    array<int, 3> idx = vidx[ivec];
    array<double, 3> vec;
    vec[0] = idx[0] * primvecs_[0][0] + idx[1] * primvecs_[1][0] + idx[2] * primvecs_[2][0];
    vec[1] = idx[0] * primvecs_[0][1] + idx[1] * primvecs_[1][1] + idx[2] * primvecs_[2][1];
    vec[2] = idx[0] * primvecs_[0][2] + idx[1] * primvecs_[1][2] + idx[2] * primvecs_[2][2];

    const double rsq = vec[0] * vec[0] + vec[1] * vec[1] + vec[2] * vec[2];
    if (rsq > numerical_zero__) {
      const double r = sqrt(rsq);
      const double ctheta = vec[2]/r;
      const double phi = atan2(vec[1], vec[0]);

      for (int l = 0; l < max_rank_; ++l) {
        for (int m = 0; m <= 2 * l; ++m) {
          const int am = abs(m - l);
          const int imul = l * l + m;

          double plm_tilde = plm.compute(l, am, ctheta) / pow(r, l+1);
          double ft = 1.0;
          for (int i = 1; i <= l - am; ++i) {
            plm_tilde *= ft;
            ft += 1.0;
          }

          const double sign = (m - l >=0) ? 1.0 : -1.0;
          const double real = sign * cos(am * phi) * plm_tilde;
          const double imag = sin(am * phi) * plm_tilde;
          mlm[imul] += complex<double>(real, imag);
        }
      }
    }
  }
#if 0
  // DEBUG
  cout << "RESULTS FROM SLOW SUMMATION" << endl;
  for (int l = 0; l < max_rank_; ++l)
    for (int m = 0; m <= l; ++m) { // Slm = -sl-m
      const int imul = l * l + m + l;
      const double tmp = mlm[imul].real();
      if (l % 2 == 0 && m % 4 == 0)
        cout << "l = " << l << "  m = " << m << "  Mlm = " << setw(20) << scientific << setprecision(14) << tmp << endl;
    }
  cout << " ******* END ******* " << endl;
#endif
}
