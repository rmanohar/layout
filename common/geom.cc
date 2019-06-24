/*************************************************************************
 *
 *  Copyright (c) 2019 Rajit Manohar
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA  02110-1301, USA.
 *
 **************************************************************************
 */
#include <stdio.h>
#include <list.h>
#include <config.h>
#include <act/act.h>
#include <act/passes.h>
#include <act/passes/netlist.h>
#include "geom.h"
#include "tech.h"

Layer *Layer::base = NULL;

Tile::Tile ()
{
  /*-- default tile is a space tile that is infinitely large --*/
  ll.x = NULL;
  ll.y = NULL;
  ur.x = NULL;
  ur.y = NULL;
  llx = (signed)(1UL << (8*sizeof(llx)-1));
  lly = (signed)(1UL << (8*sizeof(lly)-1));
  xsz = ~0UL;
  ysz = ~0UL;
  up = NULL;
  down = NULL;
  space = 1;
  attr = 0;
}
  

Layer::Layer (Material *m)
{
  mat = m;

  up = NULL;
  down = NULL;
  other = NULL;

  hint = origin = new Tile();
  vhint = vorigin = new Tile();

  hint->up = vhint;
  vhint->down = hint;
}

void Layer::allocOther (int sz)
{
  Assert (other == NULL, "Hmm");
  Assert (sz > 0, "Hmm");
  MALLOC (other, Material *, sz);
  for (int i=0; i < sz; i++) {
    other[i] = NULL;
  }
  nother = sz;
  Assert (nother <= 64, "attr field is not big enough?");
}

void Layer::setOther (int idx, Material *m)
{
  Assert (other[idx] == NULL, "What?");
  other[idx] = m;
}

void Layer::setDownLink (Layer *x)
{
  down = x;
  x->up = this;

  /* link the tiles as well */
  x->vhint->up = hint;
  hint->down = x->vhint;
}
  
void Geometry::Init()
{
  Technology::Init ("layout.conf");
  
  /*-- create all the layers --*/

  /* 1. base layer for diff, well, fets */
  Layer *base = new Layer (Technology::T->poly);
  Layer::base = base;

  /* 2. Also has #flavors*6 materials! */
  int sz = config_get_table_size ("act.dev_flavors");
  Assert (sz > 0, "Hmm");

  base->allocOther (sz*6);

  /* order: nfet, pfet, 
            ndiff, pdiff
            nfet_well, pfet_well
  */
  for (int i=0; i < sz; i++) {
    base->setOther (6*i+2*0 + EDGE_NFET, Technology::T->fet[EDGE_NFET][i]);
    base->setOther (6*i+2*0 + EDGE_PFET, Technology::T->fet[EDGE_PFET][i]);
    base->setOther (6*i+2*1 + EDGE_NFET, Technology::T->diff[EDGE_NFET][i]);
    base->setOther (6*i+2*1 + EDGE_PFET, Technology::T->diff[EDGE_PFET][i]);
    base->setOther (6*i+2*2 + EDGE_NFET, Technology::T->well[EDGE_NFET][i]);
    base->setOther (6*i+2*2 + EDGE_PFET, Technology::T->well[EDGE_PFET][i]);
  }

  Layer *prev = base;

  /* create metal layers */
  for (int i=0; i < Technology::T->nmetals; i++) {
    Layer *metals = new Layer (Technology::T->metal[i]);
    metals->setDownLink (prev);
    prev = metals;
  }
}

