//
// Newint - Parallel electron correlation program.
// Filename: test_mp2.cc
// Copyright (C) 2012 Toru Shiozaki
//
// Author: Toru Shiozaki <shiozaki@northwestern.edu>
// Maintainer: Shiozaki group
//
// This file is part of the Newint package (to be renamed).
//
// The Newint package is free software; you can redistribute it and\/or modify
// it under the terms of the GNU Library General Public License as published by
// the Free Software Foundation; either version 2, or (at your option)
// any later version.
//
// The Newint package is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Library General Public License for more details.
//
// You should have received a copy of the GNU Library General Public License
// along with the Newint package; see COPYING.  If not, write to
// the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
//


#include <memory>
#include <src/mp2/mp2.h>

double mp2_energy() {

  std::shared_ptr<std::ofstream> ofs(new std::ofstream("benzene_svp_mp2.testout", std::ios::trunc));
  std::streambuf* backup_stream = std::cout.rdbuf(ofs->rdbuf());

  // a bit ugly to hardwire an input file, but anyway...
  std::shared_ptr<InputData> idata(new InputData("../../test/benzene_svp_mp2.in"));
  stack = new StackMem(static_cast<size_t>(1000000LU));
  std::shared_ptr<Geometry> geom(new Geometry(idata));
  std::list<std::pair<std::string, std::multimap<std::string, std::string> > > keys = idata->data();

  for (auto iter = keys.begin(); iter != keys.end(); ++iter) {
    if (iter->first == "mp2") {
      std::shared_ptr<MP2> mp2(new MP2(iter->second, geom));
      mp2->compute();

      delete stack;
      std::cout.rdbuf(backup_stream);
      return mp2->energy();
    }
  }
  assert(false);
}
 
BOOST_AUTO_TEST_SUITE(TEST_MP2)
 
BOOST_AUTO_TEST_CASE(MP2) {
    BOOST_CHECK(compare(mp2_energy(), -231.31440958));
}
 
BOOST_AUTO_TEST_SUITE_END()
