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
#include "subcell.h"

#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif


LayoutBlob::LayoutBlob (ExternMacro *m)
{
  long llx, lly, urx, ury;
  t = BLOB_MACRO;
  macro = m;
  if (macro && macro->isValid()) {
    macro->getBBox (&llx, &lly, &urx, &ury);
    _bbox.setRectCoords (llx, lly, urx, ury);
    _bloatbbox = _bbox;
    _abutbox = _bbox;
  }
  else {
    _bbox.clear ();
    _bloatbbox.clear ();
    _abutbox.clear ();
  }    
}

LayoutBlob::LayoutBlob (blob_type type, Layout *lptr)
{
  t = type;
  readRect = false;

  count = 0;
  
  switch (t) {
  case BLOB_MACRO:
    macro = NULL;
    warning ("LayoutBlob: create macro using a different constructor!");
    break;

  case BLOB_CELLS:
    warning ("LayoutBlob:: constructor called with BLOB_CELLS; converting to BLOB_BASE!");
    t = BLOB_BASE;
    type = BLOB_BASE;
  case BLOB_BASE:
    base.l = lptr;
    if (lptr) {
      long llx, lly, urx, ury;
      lptr->getBBox (&llx, &lly, &urx, &ury);
      _bbox.setRectCoords (llx, lly, urx, ury);
      lptr->getBloatBBox (&llx, &lly, &urx, &ury);
      _bloatbbox.setRectCoords (llx, lly, urx, ury);
      _abutbox = lptr->getAbutBox ();
    }
    else {
      _bbox.clear ();
      _bloatbbox.clear ();
      _abutbox.clear ();
    }
    /* XXX: set edge attributes */
    break;

  case BLOB_LIST:
    l.hd = NULL;
    l.tl = NULL;
    if (lptr) {
      blob_list *bl;
      NEW (bl, blob_list);
      bl->b = new LayoutBlob (BLOB_BASE, lptr);
      bl->next = NULL;
      bl->T.mkI();
      q_ins (l.hd, l.tl, bl);
      _bbox = bl->b->_bbox;
      _bloatbbox = bl->b->_bloatbbox;
      _abutbox = bl->b->_abutbox;
      _le = bl->b->_le;
    }
    else {
      _bbox.clear ();
      _bloatbbox.clear ();
      _abutbox.clear ();
      _le.clear ();
    }
    break;
  }
}


LayoutBlob::LayoutBlob (LayerSubcell *cells)
{
  t = BLOB_CELLS;
  
  readRect = false;
  count = 0;

  Assert (cells, "What?");

  subcell.l = cells;
  _bbox = cells->getBBox ();
  _bloatbbox = cells->getBloatBBox ();
  _abutbox = cells->getAbutBox ();
}


void LayoutBlob::setBBox (long _llx, long _lly, long _urx, long _ury)
{
  if (t == BLOB_MACRO) {
    return;
  }
  if (t != BLOB_BASE || base.l) {
    warning ("LayoutBlob::setBBox(): called on non-empty layout; ignored");
    return;
  }

  Rectangle r;
  r.setRectCoords (_llx, _lly, _urx, _ury);

  if (!_bbox.empty() && !r.contains (_bbox)) {
    fatal_error ("LayoutBlob::setBBox(): shrinking the bounding box is not permitted");
  }
  _bbox = r;
  _bloatbbox = r;
  _abutbox.clear ();
}

void LayoutBlob::appendBlob (LayoutBlob *b, blob_compose c, long gap)
{
  if (t == BLOB_BASE) {
    warning ("LayoutBlob::appendBlob() called on BASE; error ignored!");
    return;
  }
  if (t == BLOB_MACRO) {
    warning ("LayoutBlob::appendBlob() called on MACRO; error ignored!");
    return;
  }
  if (t == BLOB_CELLS) {
    warning ("LayoutBlob::appendBlob() called on CELLS; error ignored!");
    return;
  }

  Assert (t == BLOB_LIST, "What?");

  blob_list *bl, *prev;
  NEW (bl, blob_list);
  bl->next = NULL;
  bl->b = b;
  bl->T.mkI();

  if (l.hd) {
    prev = l.tl;
  }
  else {
    prev = NULL;
  }

  q_ins (l.hd, l.tl, bl);

  if (l.hd == l.tl) {
    // the first blob
    _bbox = b->getBBox ();
    _bloatbbox = b->getBloatBBox ();
    _abutbox = b->getAbutBox ();
    if (c == BLOB_HORIZ) {
      bl->T.translate (gap, 0);
    }
    else if (c == BLOB_VERT) {
      bl->T.translate (0, gap);
    }
    _bbox = bl->T.applyBox (_bbox);
    _bloatbbox = bl->T.applyBox (_bloatbbox);
    _abutbox = bl->T.applyBox (_abutbox);
  }
  else {
    // we are actually appending to the blob
    Assert (prev, "What?");
    if (c == BLOB_HORIZ) {
      long damt;
      bool valid;
      /* align! */
      valid = LayoutEdgeAttrib::align (l.tl->b->getRightAlign(),
				       b->getLeftAlign(), &damt);
      if (!valid) {
	warning ("appendBlob: no valid alignment, but continuing anyway");
	damt = 0;
      }

      int shiftamt = gap
	+ (_bloatbbox.urx() - _bbox.llx() + 1)  /* width, including bloat */
	+ (b->getBBox().llx() - b->getBloatBBox().llx()); /* left
							     bloat */

      TransformMat t;
      t.translate (shiftamt, damt);
      bl->T = t;

      // now we have shifted the blob; so
      Rectangle r;

      r = bl->T.applyBox (b->getBBox());

      _bbox = _bbox ^ r;

      r = bl->T.applyBox (b->getBloatBBox());
      _bloatbbox = _bloatbbox ^ r;

      r = b->getAbutBox();
      if (!r.empty()) {
	r = bl->T.applyBox (b->getAbutBox());
	_abutbox = _abutbox ^ r;
      }
    }
    else if (c == BLOB_VERT) {
      long damt;
      bool valid;
      /* align! */
      valid = LayoutEdgeAttrib::align (l.tl->b->getTopAlign(),
				       b->getBotAlign(), &damt);

      if (!valid) {
	warning ("appendBlob: no valid alignment, but continuing anyway");
	damt = 0;
      }

      int shiftamt = gap
	+ (_bloatbbox.ury() - _bbox.lly() + 1) /* width, including bloat */
	+ (b->getBBox().lly() - b->getBloatBBox().lly() + 1); /* bottom bloat */

      TransformMat t;
      t.translate (damt, shiftamt);
      bl->T = t;
      
      Rectangle r;

      r = bl->T.applyBox (b->getBBox());

      _bbox = _bbox ^ r;

      r = bl->T.applyBox (b->getBloatBBox ());
      _bloatbbox = _bloatbbox ^ r;

      r = b->getAbutBox();
      if (!r.empty()) {
	r = bl->T.applyBox (b->getAbutBox());
	_abutbox = _abutbox ^ r;
      }
    }
    else if (c == BLOB_MERGE) {
      _bbox = _bbox ^ b->getBBox();
      _bloatbbox = _bloatbbox ^ b->getBloatBBox();
      if (!b->getAbutBox().empty()) {
	_abutbox = _abutbox ^ b->getAbutBox();
      }
    }
    else {
      fatal_error ("What?");
    }
  }
#if 0
  printf ("blob: (%ld,%ld) -> (%ld,%ld); bloat: (%ld,%ld)->(%ld,%ld)\n",
	  llx, lly, urx, ury, bloatllx, bloatlly, bloaturx, bloatury);
#endif
}


void LayoutBlob::_printRect (FILE *fp, TransformMat *mat)
{
  switch (t) {
  case BLOB_BASE:
    if (base.l) {
      base.l->PrintRect (fp, mat);
    }
    break;
    
  case BLOB_LIST:
    for (blob_list *bl = l.hd; bl; q_step (bl)) {
      TransformMat m;
      if (mat) {
	m = *mat;
      }
      m.applyMat (bl->T);
      bl->b->_printRect (fp, &m);
    }
    break;

  case BLOB_MACRO:
    /* nothing to do, it is a macro */
    break;

  case BLOB_CELLS:
    fatal_error ("ARGH!\n");
    break;

  }
}

void LayoutBlob::PrintRect (FILE *fp, TransformMat *mat)
{
  long bllx, blly, burx, bury;
  long x, y;
  Rectangle bloatbox = getBloatBBox ();
  fprintf (fp, "bbox ");
  if (mat) {
    mat->apply (bloatbox.llx(), bloatbox.lly(), &x, &y);
    fprintf (fp, "%ld %ld", x, y);
    mat->apply (bloatbox.urx()+1, bloatbox.ury()+1, &x, &y);
    fprintf (fp, " %ld %ld\n", x, y);
  }
  else {
    fprintf (fp, "%ld %ld %ld %ld\n",
	     bloatbox.llx(), bloatbox.lly(),
	     bloatbox.urx()+1, bloatbox.ury()+1);
  }
  _printRect (fp, mat);
}


list_t *LayoutBlob::search (void *net, TransformMat *m)
{
  TransformMat tmat;
  list_t *tiles;

  if (m) {
    tmat = *m;
  }
  if (t == BLOB_BASE) {
    if (base.l) {
      tiles = base.l->search (net);
      if (list_isempty (tiles)) {
	return tiles;
      }
      else {
	struct tile_listentry *tle;
	NEW (tle, struct tile_listentry);
	tle->m = tmat;
	tle->tiles = tiles;
	tiles = list_new ();
	list_append (tiles, tle);
	return tiles;
      }
    }
    else {
      tiles = list_new ();
      return tiles;
    }
  }
  else if (t == BLOB_LIST) {
    blob_list *bl;
    tiles = list_new ();
    
    for (bl = l.hd; bl; q_step (bl)) {
      if (m) {
	tmat = *m;
      }
      else {
	tmat.mkI();
      }
      tmat.applyMat (bl->T);
      list_t *tmp = bl->b->search (net, &tmat);
      list_concat (tiles, tmp);
      list_free (tmp);
    }
  }
  else if (t == BLOB_MACRO) {
    /* nothing, macro */
    tiles = list_new ();
  }
  else {
    tiles = NULL;
    fatal_error ("New blob?");
  }
  return tiles;
}

list_t *LayoutBlob::search (int type, TransformMat *m)
{
  TransformMat tmat;
  list_t *tiles;

  if (m) {
    tmat = *m;
  }
  if (t == BLOB_BASE) {
    if (base.l) {
      tiles = base.l->search (type);
      if (list_isempty (tiles)) {
	return tiles;
      }
      else {
	struct tile_listentry *tle;
	NEW (tle, struct tile_listentry);
	tle->m = tmat;
	tle->tiles = tiles;
	tiles = list_new ();
	list_append (tiles, tle);
	return tiles;
      }
    }
    else {
      tiles = list_new ();
      return tiles;
    }
  }
  else if (t == BLOB_LIST) {
    blob_list *bl;
    tiles = list_new ();
    
    for (bl = l.hd; bl; q_step (bl)) {
      if (m) {
	tmat = *m;
      }
      else {
	tmat.mkI();
      }
      tmat.applyMat (bl->T);
      list_t *tmp = bl->b->search (type, &tmat);
      list_concat (tiles, tmp);
      list_free (tmp);
    }
  }
  else if (t == BLOB_MACRO) {
    tiles = list_new ();
  }
  else {
    tiles = NULL;
    fatal_error ("New blob?");
  }
  return tiles;
}


LayoutBlob::~LayoutBlob ()
{
  /* XXX do something here! */
}


void LayoutBlob::searchBBox (list_t *slist, long *bllx, long *blly,
			     long *burx, long *bury)
{
  long wllx, wlly, wurx, wury;
  int init = 0;

  listitem_t *tli;
  for (tli = list_first (slist); tli; tli = list_next (tli)) {
    struct tile_listentry *tle = (struct tile_listentry *) list_value (tli);

    /* a transform matrix + list of (layer,tile-list) pairs */
    listitem_t *xi;
    for (xi = list_first (tle->tiles); xi; xi = list_next (xi)) {
      //Layer *name = (Layer *) list_value (xi);
      xi = list_next (xi);
      Assert (xi, "What?");
      
      list_t *actual_tiles = (list_t *) list_value (xi);
      listitem_t *ti;

      for (ti = list_first (actual_tiles); ti; ti = list_next (ti)) {
	long tllx, tlly, turx, tury;
	Tile *tmp = (Tile *) list_value (ti);

	tle->m.apply (tmp->getllx(), tmp->getlly(), &tllx, &tlly);
	tle->m.apply (tmp->geturx(), tmp->getury(), &turx, &tury);

	if (tllx > turx) {
	  long x = tllx;
	  tllx = turx;
	  turx = x;
	}
	  
	if (tlly > tury) {
	  long x = tlly;
	  tlly = tury;
	  tury = x;
	}
	
	if (!init) {
	  wllx = tllx;
	  wlly = tlly;
	  wurx = turx;
	  wury = tury;
	  init = 1;
	}
	else {
	  wllx = MIN(wllx, tllx);
	  wlly = MIN(wlly, tlly);
	  wurx = MAX(wurx, turx);
	  wury = MAX(wury, tury);
	}
      }
    }
  }
  if (!init) {
    *bllx = 0;
    *blly = 0;
    *burx = -1;
    *bury = -1;
  }
  else {
    wurx ++;
    wury ++;
    *bllx = wllx;
    *blly = wlly;
    *burx = wurx;
    *bury = wury;
  }
}

void LayoutBlob::searchFree (list_t *slist)
{
  listitem_t *tli;
  
  for (tli = list_first (slist); tli; tli = list_next (tli)) {
    struct tile_listentry *tle = (struct tile_listentry *) list_value (tli);

    /* a transform matrix + list of (layer,tile-list) pairs */
    listitem_t *xi;
    for (xi = list_first (tle->tiles); xi; xi = list_next (xi)) {
      //Layer *name = (Layer *) list_value (xi);
      xi = list_next (xi);
      Assert (xi, "What?");
      
      list_t *actual_tiles = (list_t *) list_value (xi);
      list_free (actual_tiles);
    }
    list_free (tle->tiles);
    FREE (tle);
  }
  list_free (slist);
}


LayoutBlob *LayoutBlob::delBBox (LayoutBlob *b)
{
  if (!b) return NULL;
  if (b->t == BLOB_MACRO) {
    return b;
  }
  if (b->t == BLOB_BASE) {
    if (b->base.l) {
      return b;
    }
    else {
      delete b;
      return NULL;
    }
  }
  else {
    blob_list *x, *prev;
    prev = NULL;
    x = b->l.hd;
    while (x) {
      x->b = LayoutBlob::delBBox (x->b);
      if (!x->b) {
	q_delete_item (b->l.hd, b->l.tl, prev, x);
	FREE (x);
	if (prev) {
	  x = prev->next;
	}
	else {
	  x = b->l.hd;
	}
      }
      else {
	prev = x;
	x = x->next;
      }
    }
    if (!b->l.hd) {
      delete b;
      return NULL;
    }
    else {
      return b;
    }
  }
}

list_t *LayoutBlob::searchAllMetal (TransformMat *m)
{
  TransformMat tmat;
  list_t *tiles;

  if (m) {
    tmat = *m;
  }
  if (t == BLOB_BASE) {
    if (base.l) {
      tiles = base.l->searchAllMetal();
      if (list_isempty (tiles)) {
	return tiles;
      }
      else {
	struct tile_listentry *tle;
	NEW (tle, struct tile_listentry);
	tle->m = tmat;
	tle->tiles = tiles;
	tiles = list_new ();
	list_append (tiles, tle);
	return tiles;
      }
    }
    else {
      tiles = list_new ();
      return tiles;
    }
  }
  else if (t == BLOB_LIST) {
    blob_list *bl;
    tiles = list_new ();
    
    for (bl = l.hd; bl; q_step (bl)) {
      if (m) {
	tmat = *m;
      }
      else {
	tmat.mkI();
      }
      tmat.applyMat (bl->T);
      list_t *tmp = bl->b->searchAllMetal (&tmat);
      list_concat (tiles, tmp);
      list_free (tmp);
    }
  }
  else {
    tiles = NULL;
    fatal_error ("New blob?");
  }
  return tiles;
}



Rectangle LayoutBlob::getAbutBox()
{
  switch (t) {
  case BLOB_BASE:
    if (base.l) {
      return base.l->getAbutBox();
    }
    else {
      return Rectangle();
    }
    break;
  case BLOB_CELLS:
    return subcell.l->getAbutBox();
  default:
    return Rectangle();
  }
}
