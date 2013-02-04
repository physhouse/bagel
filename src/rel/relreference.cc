//
// BAGEL - Parallel electron correlation program.
// Filename: relreference.cc
// Copyright (C) 2013 Toru Shiozaki
//
// Author: Toru Shiozaki <shiozaki@northwestern.edu>
// Maintainer: Shiozaki group
//
// This file is part of the BAGEL package.
//
// The BAGEL package is free software; you can redistribute it and/or modify
// it under the terms of the GNU Library General Public License as published by
// the Free Software Foundation; either version 2, or (at your option)
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

#include <src/rel/relreference.h>
#include <src/util/constants.h>

using namespace std;
using namespace bagel;

shared_ptr<const RelReference> RelReference::project_coeff(shared_ptr<const Geometry> geomin) const {
  shared_ptr<const RelReference> out;

  if (*geom_ == *geomin) {
    out = shared_from_this();
  } else {
    // in this case we first form overlap matrices
    RelOverlap snew(geomin, true);
    ZMatrix sinv = snew * snew;

    MixedBasis<OverlapBatch> smixed(geom_, geomin);
    MixedBasis<KineticBatch> tmixed(geom_, geomin);
    const int nb = geomin->nbasis();
    const int mb = geom_->nbasis();
    const complex<double> one(1.0);
    const complex<double> sca = one * (0.5/(c__*c__));
    ZMatrix mixed(nb*4, mb*4);
    mixed.copy_real_block(one,    0,    0, nb, mb, smixed.data());
    mixed.copy_real_block(one,   nb,   mb, nb, mb, smixed.data());
    mixed.copy_real_block(sca, 2*nb, 2*mb, nb, mb, tmixed.data());
    mixed.copy_real_block(sca, 3*nb, 3*mb, nb, mb, tmixed.data());

    shared_ptr<ZMatrix> c(new ZMatrix(sinv * mixed * *coeff_));
    out = shared_ptr<const RelReference>(new RelReference(geomin, c));
  }
  return out;
}