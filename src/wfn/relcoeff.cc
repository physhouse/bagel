//
// BAGEL - Parallel electron correlation program.
// Filename: relcoeff.cc
// Copyright (C) 2015 Toru Shiozaki
//
// Author: Ryan D. Reynolds <RyanDReynolds@u.northwestern.edu>
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

#include <src/wfn/relcoeff.h>
#include <cassert>
#include <iostream>
#include <iomanip>

using namespace std;
using namespace bagel;

RelCoeff::RelCoeff(const ZMatrix& _coeff, const int _nclosed, const int _nact, const int _nvirt, const int _nneg, const bool move_neg)
 : ZMatrix(_coeff.ndim(), _coeff.mdim(), _coeff.localized()), nbasis_(ndim()/4), nclosed_(_nclosed), nact_(_nact), nvirt_(_nvirt), nneg_(_nneg) {
  assert(ndim()%4 == 0);
  assert(2 * (nclosed_ + nact_ + nvirt_) + nneg_ == mdim());

  if (!move_neg) {
    // copy input matrix directly
    copy_block(0, 0, ndim(), mdim(), _coeff);
  } else {
    // move positronic orbitals to end of virtual space
    copy_block(0, 0,      ndim(), npos(), _coeff.slice(nneg_, nneg_+npos()));
    copy_block(0, npos(), ndim(), nneg_,  _coeff.slice(0,     nneg_));
  }
}


// Transforms a coefficient matrix from striped format to block format : assumes ordering is (c,a,v,positrons)
std::shared_ptr<RelCoeff_Block> RelCoeff_Striped::block_format() const {
  assert(nneg_ % 2 == 0);
  const int nvirt2 = nvirt_ + nneg_/2;
  shared_ptr<ZMatrix> ctmp2 = clone();
  int n = ndim();
  // closed
  for (int j=0; j!=nclosed_; ++j) {
    ctmp2->copy_block(0,            j, n, 1, slice(j*2  , j*2+1));
    ctmp2->copy_block(0, nclosed_ + j, n, 1, slice(j*2+1, j*2+2));
  }
  int offset = nclosed_*2;
  // active
  for (int j=0; j!=nact_; ++j) {
    ctmp2->copy_block(0, offset + j,         n, 1, slice(offset +j*2,   offset + j*2+1));
    ctmp2->copy_block(0, offset + nact_ + j, n, 1, slice(offset +j*2+1, offset + j*2+2));
  }
  offset = (nclosed_+nact_)*2;
  // virtual (including positrons)
  for (int j=0; j!=nvirt2; ++j) {
    ctmp2->copy_block(0, offset + j,            n, 1, slice(offset + j*2,   offset + j*2+1));
    ctmp2->copy_block(0, offset + nvirt2 + j,   n, 1, slice(offset + j*2+1, offset + j*2+2));
  }
  auto out = make_shared<RelCoeff_Block>(*ctmp2, nclosed_, nact_, nvirt_, nneg_);
  return out;
}


std::shared_ptr<RelCoeff_Striped> RelCoeff_Block::striped_format() const {
  assert(nneg_ % 2 == 0);
  const int nvirt2 = nvirt_ + nneg_/2;
  auto ctmp2 = clone();
  // Transforms a coefficient matrix from block format to striped format : assumes ordering is (c,a,v,positrons)
  // striped format
  int n = ndim();
  int offset = nclosed_;
  // closed
  for (int j=0; j!=nclosed_; ++j) {
    ctmp2->copy_block(0, j*2,   n, 1, slice(j, j+1));
    ctmp2->copy_block(0, j*2+1, n, 1, slice(offset + j, offset + j+1));
  }
  offset = nclosed_*2;
  // active
  for (int j=0; j!=nact_; ++j) {
    ctmp2->copy_block(0, offset + j*2,   n, 1, slice(offset + j,         offset + j+1));
    ctmp2->copy_block(0, offset + j*2+1, n, 1, slice(offset + nact_ + j, offset + nact_ + j+1));
  }
  offset = (nclosed_+nact_)*2;
  // vituals (including positrons)
  for (int j=0; j!=nvirt2; ++j) {
    ctmp2->copy_block(0, offset + j*2,   n, 1, slice(offset + j,          offset + j+1));
    ctmp2->copy_block(0, offset + j*2+1, n, 1, slice(offset + nvirt2 + j, offset + nvirt2 + j+1));
  }
  auto out = make_shared<RelCoeff_Striped>(*ctmp2, nclosed_, nact_, nvirt_, nneg_);
  return out;
}


#if 0
BOOST_CLASS_EXPORT_IMPLEMENT(RelCoeff)
BOOST_CLASS_EXPORT_IMPLEMENT(RelCoeff_Striped)
BOOST_CLASS_EXPORT_IMPLEMENT(RelCoeff_Block)
#endif
