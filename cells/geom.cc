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
  llx = MIN_VALUE;
  lly = MIN_VALUE;
  
  up = NULL;
  down = NULL;
  space = 1;
  virt = 0;
  attr = 0;
}

Tile::~Tile()
{
  ll.x = NULL;
  ll.y = NULL;
  ur.x = NULL;
  ur.y = NULL;
  llx = MIN_VALUE;
  lly = MIN_VALUE;
  
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

  hint = new Tile();
  vhint = new Tile();

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

  nflavors = sz;
  nmetals = Technology::T->nmetals;

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

  MALLOC (metals, Layer *, Technology::T->nmetals);

  /* create metal layers */
  for (int i=0; i < Technology::T->nmetals; i++) {
    metals[i] = new Layer (Technology::T->metal[i]);
    metals[i]->setDownLink (prev);
    prev = metals[i];
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



Tile *Tile::find (long x, long y)
{
  Tile *t = this;
  do {
    if (x < t->llx) {
      while (x < t->llx) {
	t = t->ll.x;
      }
      Assert (xmatch (x), "Invariant failed");
    }
    else if (!xmatch (x)) {
      while (t->ur.x && (x >= nextx())) {
	t = t->ur.x;
      }
      Assert (xmatch (x), "Invariant failed");
    }

    if (y < t->lly) {
      while (y < t->lly) {
	t = t->ll.y;
      }
      Assert (ymatch (y), "Invariant failed");
    }
    else if (!ymatch (y)) {
      while (t->ur.y && (y >= nexty())) {
	t = t->ur.y;
      }
      Assert (ymatch (y), "Invariant failed");
    }
  } while (!xmatch (x));
  return t;
}


/*
  Returns a list of all the tiles that overlap with the specified region
*/
void Tile::applyTiles (long _llx, long _lly, unsigned long wx, unsigned long wy,
		       void *cookie, void (*f) (void *, Tile *))

{
  Tile *t;
  list_t *frontier;
  long _urx, _ury;

  _urx = _llx + wx - 1;
  _ury = _lly + wy - 1;

  t = find (_llx, _lly);

  frontier = list_new ();
  list_append (frontier, t);

  /*  double-check which tiles get added.

      ~~~~~~~~ 
     |        |
      ~~~~~~~~ 

      invariant: everything to the left and below is handled.
                 everything to the right that is below my bottom line
                 is handled.
		 everything to the top that is to the left of my left
                 edge is handled.
      
   */
  while (!list_isempty (frontier)) {
    Tile *tmp;
    t = (Tile *) list_delete_tail (frontier);
    /* invariant: t's ll corner is in the region */
    tmp = t->ur.x;
    while (tmp) {
      if (tmp->nexty() <= t->lly) break;
      if (_llx <= tmp->llx && tmp->llx <= _urx &&
	  _lly <= tmp->lly && tmp->lly <= _ury) {
	list_append_head (frontier, tmp);
      }
      else {
	break;
      }
      tmp = tmp->ll.y;
    }
    tmp = t->ur.y;
    while (tmp) {
      if (tmp->nextx() <= t->llx) break;
      if (_llx <= tmp->llx && tmp->llx <= _urx &&
	  _lly <= tmp->lly && tmp->lly <= _ury) {
	list_append_head (frontier, tmp);
      }
      else {
	break;
      }
      tmp = tmp->ll.x;
    }
    (*f) (cookie, t); 		/* apply function */
  }
  list_free (frontier);
}

static void append_tile (void *cookie, Tile *t)
{
  list_t *l = (list_t *) cookie;
  list_append (l, t);
}

/*
  Returns a list of all the tiles that overlap with the specified region
*/
list_t *Tile::collectRect (long _llx, long _lly,
			   unsigned long wx, unsigned long wy)
{
  list_t *l;

  l = list_new ();

  applyTiles (_llx, _lly, _llx + wx - 1, _lly + wy - 1, l, append_tile);
  
  return l;
}


Tile *Tile::addRect (long _llx, long _lly, unsigned long wx, unsigned long wy,
		     bool force)
{
  /*
    we first collect all the tiles within the region.
  */
  list_t *l = collectRect (_llx, _lly, wx, wy);
  
  list_t *ml;
  listitem_t *li;

  if (list_isempty (l)) {
    fatal_error ("Tile::collectRect() failed!");
  }

  Tile *t = (Tile *) list_value (list_first (l));
  void *tnet = t->net;

  /* 
     check that all the tile types match
  */
  if (!force) {
    for (li = list_first (l); li; li = list_next (li)) {
      Tile *tmp = (Tile *) list_value (li);
      if (t->space != tmp->space || t->virt != tmp->virt || t->attr != tmp->attr) {
	warning ("Tile::addRect() failed; inconsistent tile types being merged");
	list_free (l);
	return NULL;
      }
      if (tmp->net && tnet && tnet != tmp->net) {
	warning ("Tile::addRect() failed; inconsistent nets being merged");
	list_free (l);
	return NULL;
      }
      if (!tnet && tmp->net) {
	tnet = tmp->net;
      }
    }
  }

  ml = list_new ();

  /* create new rectangle */
  Tile *rt = new Tile();
  rt->net = tnet;
  rt->space = t->space;
  rt->virt = t->virt;
  rt->attr = t->attr;
    
  rt->llx = _llx;
  rt->lly = _lly;
  rt->ur.x = NULL;
  rt->ur.y = NULL;
  rt->ll.x = NULL;
  rt->ll.y = NULL;

  /* 
     walk through all the tiles with overlap, and prune them so that
     we have created a region that is the specified rectangle 
  */
  while (!list_isempty (l)) {
    t = (Tile *) list_delete_tail (l);

    if (t->llx < _llx) {
      t = t->splitX (_llx);	/* left edge prune */
    }
    if (t->lly < _lly) {
      t = t->splitY (_lly);	/* bottom edge prune */
    }
    if (t->nextx() > _llx + wx) {
      t->splitX (_llx+wx);	/* right edge prune */
    }
    if (t->nexty() > _lly + wy) {
      t->splitY (_lly+wy);	/* top edge prune */
    }
    list_append (ml, t);
  }

  while (!list_isempty (ml)) {
    /* repair stitches */
    Tile *tmp = (Tile *) list_delete_tail (ml);

    if (tmp->llx == _llx && tmp->lly == _lly) {
      /* ll corner tile; import stitches */
      rt->ll.x = tmp->ll.x;
      rt->ll.y = tmp->ll.y;
    }

    if ((tmp->nextx() == _llx + wx) && (tmp->nexty() == _lly + wy)) {
      /* ur corner; import stitches */
      rt->ur.x = tmp->ur.x;
      rt->ur.y = tmp->ur.y;
    }

    /* left edge */
    if (tmp->llx == _llx) {
      Tile *x = tmp->ll.x;
      while (x && (x->ury() <= _lly + wy - 1)) {
	if (x->ur.x == rt) break; //-- done this already
	x->ur.x = rt;
	x = x->ur.y;
      }
    }

    /* right edge */
    if (tmp->urx() == _llx + wx - 1) {
      Tile *x = tmp->ur.x;
      while (x && (x->lly >= _lly)) {
	if (x->ll.x == rt) break;
	x->ll.x = rt;
	x = x->ll.y;
      }
    }

    /* bottom edge */
    if (tmp->lly == _lly) {
      Tile *x = tmp->ll.y;
      while (x && (x->urx() <= _llx + wx - 1)) {
	if (x->ur.y == rt) break;
	x->ur.y = rt;
	x = x->ur.x;
      }
    }

    /* top edge */
    if (tmp->ury() == _lly + wy - 1) {
      Tile *x = tmp->ur.y;
      while (x && (x->llx >= _llx)) {
	if (x->ll.y == rt) break;
	x->ll.y = rt;
	x = x->ll.x;
      }
    }
    list_append (l, tmp);
  }
  list_free (ml);
  
  for (li = list_first (l); li; li = list_next (li)) {
    Tile *tmp = (Tile *) list_value (li);
    delete tmp;
  }
  list_free (l);

  return rt;
}


/*
 *  Split a tile at X coordinate specified. Returns the new tile.
 */
Tile *Tile::splitX (long x)
{
  Assert (llx < x && xmatch (x), "What?");

  Tile *t = new Tile ();

  t->space = space;
  t->virt = virt;
  t->attr = attr;
  t->up = up;
  t->down = down;

  t->llx = x;
  t->lly = lly;

  t->ur.x = ur.x;
  t->ur.y = ur.y;
  t->ll.x = this;

  /* find t->ll.y */
  if (lly == MIN_VALUE) {
    t->ll.y = NULL;
  }
  else {
    t->ll.y = find (x, lly-1);
  }

  if (ur.y) {
    ur.y = find (x-1, nexty());
  }
  ur.x = t;
  
  return t;
}


/*
 * Split a tile at the y-coordinate specified
 */
Tile *Tile::splitY (long y)
{
  Assert (lly < y && ymatch (y), "What?");

  Tile *t = new Tile ();

  t->space = space;
  t->virt = virt;
  t->attr = attr;
  t->up = up;
  t->down = down;

  t->llx = llx;
  t->lly = y;

  t->ur.x = ur.x;
  t->ur.y = ur.y;
  t->ll.y = this;

  /* find t->ll.x */
  if (llx == MIN_VALUE) {
    t->ll.x = NULL;
  }
  else {
    t->ll.x = find (llx-1, y);
  }

  if (ur.x) {
    ur.x = find (nextx(), y-1);
  }
  ur.y = t;
  
  return t;
}


int Layer::drawVia (long llx, long lly, unsigned long wx, unsigned long wy,
		    void *net, int attr)
{
  Tile *x;

  x = vhint->addRect (llx, lly, wx, wy);
  if (!x) return 0;

  if (!x->space) {
    /* overwriting a net */
    if (x->net && net && x->net != net) {
      return 0;
    }
    if (x->attr != attr) {
      return 0;
    }
  }
  x->space = 0;
  x->attr = attr;
  if (net) {
    x->net = net;
  }
  return 1;
}

int Layer::drawVia (long llx, long lly, unsigned long wx, unsigned long wy,
		    int type)
{
  return drawVia (llx, lly, wx, wy, NULL, type);
}


int Layer::Draw (long llx, long lly, unsigned long wx, unsigned long wy,
		 void *net, int attr)
{
  Tile *x;

  x = hint->addRect (llx, lly, wx, wy);
  if (!x) return 0;

  if (!x->space) {
    /* overwriting a net */
    if (x->net && net && x->net != net) {
      return 0;
    }
    if (x->attr != attr) {
      return 0;
    }
  }
  x->space = 0;
  x->attr = attr;
  if (net) {
    x->net = net;
  }
  return 1;
}

int Layer::Draw (long llx, long lly, unsigned long wx, unsigned long wy,
		 int type)
{
  return Draw (llx, lly, wx, wy, NULL, type);
}


int Layout::DrawPoly (long llx, long lly, unsigned long wx, unsigned long wy)
{
  return base->Draw (llx, lly, wx, wy, 0);
}


int Layout::DrawDiff (int flavor /* fet flavor */, int type /* n or p */,
		      long llx, long lly, unsigned long wx, unsigned long wy)
{
  if (flavor < 0 || flavor >= nflavors) return 0;
  return base->Draw (llx, lly, wx, wy,
		     1 + TOTAL_OFFSET (flavor, type, DIFF_OFFSET));
}


int Layout::DrawFet (int flavor, int type,
		     long llx, long lly, unsigned long wx, unsigned long wy)
{
  if (flavor < 0 || flavor >= nflavors) return 0;
  return base->Draw (llx, lly, wx, wy,
		     1 + TOTAL_OFFSET (flavor, type, FET_OFFSET));
}

int Layout::DrawDiffBBox (int flavor, int type,
			  long llx, long lly, unsigned long wx, unsigned long wy)
{
  if (flavor < 0 || flavor >= nflavors) return 0;
  /* ignore: have to do this properly */
  return 1;
}

  /* 0 = metal1, etc. */
int Layout::DrawMetal (int num, long llx, long lly, unsigned long wx, unsigned long wy )
{
  if (num < 0 || num >= nmetals) return 0;
  /* XXX: colors? */
  return metals[num]->Draw (llx, lly, wx, wy, 0);
}

/* 0 = base to metal1, 1 = metal1 to metal2, etc. */
int Layout::DrawVia (int num, long llx, long lly, unsigned long wx, unsigned long wy)
{
  if (num < 0 || num > nmetals) return 0;
  /* XXX: colors? */
  if (num == 0) {
    return base->drawVia (llx, lly, wx, wy, 0);
  }
  else {
    return metals[num-1]->drawVia (llx, lly, wx, wy, 0);
  }
}
