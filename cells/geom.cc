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
  virt = 0;
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

Layer::~Layer()
{
  /* XXX: delete all tiles! */
  
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
  
void Layout::Init()
{
  Technology::Init ("layout.conf");
}

Layout::Layout()
{
  /*-- create all the layers --*/
  Assert (Technology::T, "Initialization error");

  /* 1. base layer for diff, well, fets */
  base = new Layer (Technology::T->poly);

  /* 2. Also has #flavors*6 materials! */
  int sz = config_get_table_size ("act.dev_flavors");
  Assert (sz > 0, "Hmm");

  base->allocOther (sz*4);
  for (int i=0; i < sz; i++) {
    base->setOther (TOTAL_OFFSET(i, EDGE_NFET, FET_OFFSET),
		    Technology::T->fet[EDGE_NFET][i]);
    base->setOther (TOTAL_OFFSET(i, EDGE_PFET, FET_OFFSET),
		    Technology::T->fet[EDGE_PFET][i]);

    base->setOther (TOTAL_OFFSET(i, EDGE_NFET, DIFF_OFFSET),
		    Technology::T->diff[EDGE_NFET][i]);
    base->setOther (TOTAL_OFFSET(i, EDGE_PFET, DIFF_OFFSET),
		    Technology::T->diff[EDGE_PFET][i]);
  }

  Layer *prev = base;

  /* create metal layers */
  for (int i=0; i < Technology::T->nmetals; i++) {
    Layer *metals = new Layer (Technology::T->metal[i]);
    metals->setDownLink (prev);
    prev = metals;
  }
}

Layout::~Layout()
{
  Layer *t, *l;

  t = base;

  while (t) {
    l = t;
    t = t->up;
    delete l;
  }
}


FetMat *Layout::getFet (int type, int flavor)
{
  FetMat *f;
  Material *m;
  m = base->other[TOTAL_OFFSET(flavor,type,FET_OFFSET)];
  f = Technology::T->fet[type][flavor];
  Assert (f == m, "Eh?");
  return f;
}

DiffMat *Layout::getDiff (int type, int flavor)
{
  DiffMat *d;
  Material *m;
  m = base->other[TOTAL_OFFSET(flavor,type,DIFF_OFFSET)];
  d = Technology::T->diff[type][flavor];
  Assert (d == m, "Eh?");
  return d;
}

WellMat *Layout::getWell (int type, int flavor)
{
  return Technology::T->well[type][flavor];
}


PolyMat *Layout::getPoly ()
{
  return Technology::T->poly;
}
