/*************************************************************************
 *
 *  Copyright (c) 2024 Rajit Manohar
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
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
#include <string.h>
#include <common/list.h>
#include <act/act.h>
#include <act/passes.h>
#include <act/passes/netlist.h>
#include <act/tech.h>
#include <common/qops.h>
#include "geom.h"

/*
 * Layer manipulation
 */

#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif


Layer::Layer (Material *m, netlist_t *_n)
{
  mat = m;
  N = _n;

  up = NULL;
  down = NULL;
  other = NULL;
  nother = 0;
  bbox = 0;

  hint = new Tile();
  vhint = new Tile();

  //hint->up = vhint;
  //vhint->down = hint;
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
  //x->vhint->up = hint;
  //hint->down = x->vhint;
}


int Layer::drawVia (long llx, long lly, unsigned long wx, unsigned long wy,
		    void *net, int attr)
{
  Tile *x;

  bbox = 0;

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

int Layer::isMetal ()
{
  if (nother > 0) return 0;
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

  bbox = 0;

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

int Layer::DrawVirt (int flavor, int type,
		     long llx, long lly, unsigned long wx, unsigned long wy)
{
  bbox = 0;
  return hint->addVirt (flavor, type, llx, lly, wx, wy);
}

int Layer::Draw (long llx, long lly, unsigned long wx, unsigned long wy,
		 int type)
{
  return Draw (llx, lly, wx, wy, NULL, type);
}

void Layer::markPins (void *net, int isinput)
{
  if (!net) return;

  list_t *l = searchMat (net);

  for (listitem_t *li = list_first (l); li; li = list_next (li)) {
    Tile *t = (Tile *) list_value (li);
#if 0
    printf ("  (%ld,%ld) -> (%ld,%ld)\n", t->getllx(), t->getlly(), t->geturx(), t->getury());
#endif    
    TILE_ATTR_MKPIN (t->attr);
    if (!isinput) {
      TILE_ATTR_MKOUTPUT (t->attr);
    }
  }
  list_free (l);
}

static void append_nonspacetile (void *cookie, Tile *t)
{
  list_t *l = (list_t *) cookie;

  if (!t->isSpace()) {
    list_append (l, t);
  }
}

static void append_nonspacebasetile (void *cookie, Tile *t)
{
  list_t *l = (list_t *) cookie;

  if (!t->isBaseSpace()) {
    list_append (l, t);
  }
}


static void dump_node (FILE *fp, netlist_t *N, node_t *n)
{
  if (n->v) {
    ActId *tmp = n->v->v->id->toid();
    tmp->Print (fp);
    delete tmp;
  }
  else {
    if (n == N->Vdd) {
      fprintf (fp, "Vdd");
    }
    else if (n == N->GND) {
      fprintf (fp, "GND");
    }
    else {
      fprintf (fp, "#%d", n->i);
    }
  }
}


void Layer::PrintRect (FILE *fp, TransformMat *t)
{
  list_t *l;

  l = list_new ();

  //debug_apply = 1;
  
  hint->applyTiles (MIN_VALUE, MIN_VALUE,
		    (unsigned long)MAX_VALUE - (MIN_VALUE + 1), (unsigned long)MAX_VALUE - (MIN_VALUE + 1),
		    l, append_nonspacetile);

  //hint->printall();
  
  //debug_apply = 0;


  while (!list_isempty (l)) {
    Tile *tmp = (Tile *) list_delete_tail (l);

    if (tmp->virt && TILE_ATTR_ISDIFF (tmp->getAttr())) {
      /* this is actually a space tile (virtual diff) */
      continue;
    }

    if (mat != Technology::T->poly && tmp->isPin()) {
      if (TILE_ATTR_ISOUTPUT(tmp->attr)) {
	fprintf (fp, "outrect ");
      }
      else {
	fprintf (fp, "inrect ");
      }
    }
    else {
      fprintf (fp, "rect ");
    }

    if (tmp->net) {
      dump_node (fp, N, (node_t *)tmp->net);
    }
    else {
      fprintf (fp, "#");
    }

    if ((tmp->virt && TILE_ATTR_ISFET(tmp->getAttr()))) {
      fprintf (fp, " %s", mat->getName());
    }
    else if (TILE_ATTR_ISROUTE(tmp->getAttr()) || (nother == 0)) {
      fprintf (fp, " %s", mat->getName());
    }
    else {
      fprintf (fp, " %s", other[TILE_ATTR_NONPOLY(tmp->getAttr())]->getName());
    }
    
    long llx, lly, urx, ury;

    if (t) {
      t->apply (tmp->getllx(), tmp->getlly(), &llx, &lly);
      t->apply (tmp->geturx(), tmp->getury(), &urx, &ury);

      if (llx > urx) {
	long x = llx;
	llx = urx;
	urx = x;
      }
      if (lly > ury) {
	long x = lly;
	lly = ury;
	ury = x;
      }
    }
    else {
      llx = tmp->getllx();
      lly = tmp->getlly();
      urx = tmp->geturx();
      ury = tmp->getury();
    }
    
    fprintf (fp, " %ld %ld %ld %ld", llx, lly, urx+1, ury+1);

    /*-- now if there is a fet to the right or the left then print it! --*/
    if (tmp->net) {
      Tile *tllx, *turx;
      int fet_left, fet_right;
      tllx = tmp->llxTile();
      turx = tmp->urxTile();
      
      if (tllx && !tllx->isSpace() && TILE_ATTR_ISFET (tllx->getAttr())) {
	fet_left = 1;
      }
      else {
	fet_left = 0;
      }
      if (turx && !turx->isSpace() && TILE_ATTR_ISFET (turx->getAttr())) {
	fet_right = 1;
      }
      else {
	fet_right = 0;
      }

      if (fet_left && fet_right) {
	fprintf (fp, " center");
      }
      else if (fet_right) {
	fprintf (fp, " left");
      }
      else if (fet_left) {
	fprintf (fp, " right");
      }
    }
    fprintf (fp, "\n");
  }    
  list_free (l);

  if (vhint) {
    l = list_new ();
    
    vhint->applyTiles (MIN_VALUE, MIN_VALUE,
		       (unsigned long)MAX_VALUE - (MIN_VALUE + 1), (unsigned long)MAX_VALUE - (MIN_VALUE + 1),
		       l, append_nonspacetile);

    while (!list_isempty (l)) {
      Tile *tmp = (Tile *) list_delete_tail (l);

      fprintf (fp, "rect ");
      if (tmp->net) {
	dump_node (fp, N, (node_t *)tmp->net);
      }
      else {
	fprintf (fp, "#");
      }

      if (nother == 0) {
	fprintf (fp, " %s", ((RoutingMat *)mat)->getUpC()->getName());
      }
      else {
	// we need to look at what is below
	Tile *dn;
	dn = find (tmp->getllx(), tmp->getlly());
	if (dn->isSpace() || TILE_ATTR_ISROUTE(dn->getAttr())) {
	  fprintf (fp, " %s", ((RoutingMat *)mat)->getUpC()->getName());
	}
	else {
	  Assert (TILE_ATTR_NONPOLY(dn->getAttr()) < nother, "What?");
	  Material *tm = other[TILE_ATTR_NONPOLY(dn->getAttr())];
	  fprintf (fp, " %s", ((DiffMat *)tm)->getUpC()->getName());
	}
      }

      long llx, lly, urx, ury;

      if (t) {
	t->apply (tmp->getllx(), tmp->getlly(), &llx, &lly);
	t->apply (tmp->geturx(), tmp->getury(), &urx, &ury);

	if (llx > urx) {
	  long x = llx;
	  llx = urx;
	  urx = x;
	}
	if (lly > ury) {
	  long x = lly;
	  lly = ury;
	  ury = x;
	}
      }
      else {
	llx = tmp->getllx();
	lly = tmp->getlly();
	urx = tmp->geturx();
	ury = tmp->getury();
      }
      fprintf (fp, " %ld %ld %ld %ld\n", llx, lly, urx+1, ury+1);
    }    
    list_free (l);
  }
}


void Layer::getBloatBBox (long *llx, long *lly, long *urx, long *ury)
{
  if (!bbox) {
    getBBox (llx, lly, urx, ury);
  }
  *llx = _bllx;
  *lly = _blly;
  *urx = _burx;
  *ury = _bury;
}

void Layer::getBBox (long *llx, long *lly, long *urx, long *ury)
{
  list_t *l;
  long xllx, xlly, xurx, xury;
  long bxllx, bxlly, bxurx, bxury;
  int first = 1;
  long bloat;

  if (bbox) {
    /* cached */
    *llx = _llx;
    *lly = _lly;
    *urx = _urx;
    *ury = _ury;
    return;
  }

  l = list_new ();
  
  hint->applyTiles (MIN_VALUE+1, MIN_VALUE+1,
		    (unsigned long)MAX_VALUE - (MIN_VALUE + 1), (unsigned long)MAX_VALUE - (MIN_VALUE + 1),
		    l, append_nonspacetile);

  xllx = 0;
  xlly = 0;
  xurx = -1;
  xury = -1;
  bxllx = 0;
  bxlly = 0;
  bxurx = -1;
  bxury = -1;

  while (!list_isempty (l)) {
    Tile *tmp = (Tile *) list_delete_tail (l);
    long tllx, tlly, turx, tury;

    if (tmp->virt && TILE_ATTR_ISDIFF (tmp->getAttr())) {
      /* this is actually a space tile (virtual diff) */
      continue;
    }

    tllx = tmp->getllx ();
    tlly = tmp->getlly ();
    turx = tmp->geturx ();
    tury = tmp->getury ();

    /* compute bloat for the tile */
    if (TILE_ATTR_ISROUTE(tmp->getAttr())) {
      bloat = ((RoutingMat *)mat)->minSpacing();
    }
    else if (nother == 0 && TILE_ATTR_ISPIN(tmp->getAttr())) {
      bloat = ((RoutingMat *)mat)->minSpacing();
    }
    else {
      Material *mo;
      Assert (nother > 0, "What?");
      Assert (TILE_ATTR_ISROUTE(tmp->getAttr()) < nother, "What?");
      mo = other[TILE_ATTR_NONPOLY(tmp->getAttr())];
      Assert (mo, "What?");

      if (TILE_ATTR_ISFET (tmp->getAttr())) {
	bloat = ((FetMat *)mo)->getSpacing(0);
      }
      else if (TILE_ATTR_ISDIFF(tmp->getAttr()) ||
	       TILE_ATTR_ISWDIFF(tmp->getAttr())) {
	bloat = Technology::T->getMaxSameDiffSpacing();
      }
      else {
	fatal_error ("Bad attributes?!");
      }
    }

    /* half bloat of min spacing; round up so that you can mirror the
       cells; if mirroring is not allowed during placement, we can
       change this to two different bloats: left/bot could be 
       floor(bloat/2), and right/top could be ceil(bloat/2).
    */
    bloat = (bloat + 1)/2;

    if (first) {
      xllx = tllx;
      xlly = tlly;
      xurx = turx;
      xury = tury;
      bxllx = tllx - bloat;
      bxlly = tlly - bloat;
      bxurx = turx + bloat;
      bxury = tury + bloat;
      first = 0;
    }
    else {
      xllx = MIN(xllx, tllx);
      xlly = MIN(xlly, tlly);
      xurx = MAX(xurx, turx);
      xury = MAX(xury, tury);

      bxllx = MIN(bxllx, tllx - bloat);
      bxlly = MIN(bxlly, tlly - bloat);
      bxurx = MAX(bxurx, turx + bloat);
      bxury = MAX(bxury, tury + bloat);
    }
  }
  list_free (l);
  
  *llx = xllx;
  *lly = xlly;
  *urx = xurx;
  *ury = xury;

  bbox = 1;
  _llx = xllx;
  _lly = xlly;
  _urx = xurx;
  _ury = xury;
  _bllx = bxllx;
  _blly = bxlly;
  _burx = bxurx;
  _bury = bxury;
}


static void *_searchnet = NULL;
static void appendnet (void *cookie, Tile *t)
{
  list_t *l = (list_t *)cookie;
  if (t->getNet () == _searchnet) {
    list_append (l, t);
  }
}

static int _searchtype = -1;
static void appendtype (void *cookie, Tile *t)
{
  list_t *l = (list_t *)cookie;
  if (t->getAttr() == _searchtype) {
    list_append (l, t);
  }
}

list_t *Layer::searchMat (void *net)
{
  list_t *l = list_new ();
  _searchnet = net;
  hint->applyTiles (MIN_VALUE, MIN_VALUE,
		    (unsigned long)MAX_VALUE + -(MIN_VALUE + 1),
		    (unsigned long)MAX_VALUE + -(MIN_VALUE + 1), l, appendnet);
  _searchnet = NULL;
  return l;
}

list_t *Layer::searchMat (int type)
{
  list_t *l = list_new ();
  _searchtype = type;
  hint->applyTiles (MIN_VALUE, MIN_VALUE,
		    (unsigned long)MAX_VALUE + -(MIN_VALUE + 1),
		    (unsigned long)MAX_VALUE + -(MIN_VALUE + 1), l, appendtype);
  return l;
}

list_t *Layer::searchVia (void *net)
{
  list_t *l = list_new ();
  _searchnet = net;
  vhint->applyTiles (MIN_VALUE, MIN_VALUE,
		    (unsigned long)MAX_VALUE + -(MIN_VALUE + 1),
		    (unsigned long)MAX_VALUE + -(MIN_VALUE + 1), l, appendnet);
  _searchnet = NULL;
  return l;
}

list_t *Layer::searchVia (int type)
{
  list_t *l = list_new ();
  _searchtype = type;
  vhint->applyTiles (MIN_VALUE, MIN_VALUE,
		    (unsigned long)MAX_VALUE + -(MIN_VALUE + 1),
		    (unsigned long)MAX_VALUE + -(MIN_VALUE + 1), l, appendtype);
  return l;
}

list_t *Layer::allNonSpaceMat ()
{
  list_t *l = list_new ();

  if (isMetal()) {
    hint->applyTiles (MIN_VALUE, MIN_VALUE,
		      (unsigned long)MAX_VALUE + -(MIN_VALUE + 1),
		      (unsigned long)MAX_VALUE + -(MIN_VALUE + 1), l,
		      append_nonspacetile);
  }
  else {
    hint->applyTiles (MIN_VALUE, MIN_VALUE,
		      (unsigned long)MAX_VALUE + -(MIN_VALUE + 1),
		      (unsigned long)MAX_VALUE + -(MIN_VALUE + 1), l,
		      append_nonspacebasetile);
  }
  return l;
}

list_t *Layer::allNonSpaceVia ()
{
  list_t *l = list_new ();
  vhint->applyTiles (MIN_VALUE, MIN_VALUE,
		    (unsigned long)MAX_VALUE + -(MIN_VALUE + 1),
		    (unsigned long)MAX_VALUE + -(MIN_VALUE + 1), l,
		    append_nonspacetile);
  return l;
}


Tile *Layer::find (long llx, long lly)
{
  return hint->find (llx, lly);
}


