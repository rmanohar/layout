/*************************************************************************
 *
 *  Copyright (c) 2024 Rajit Manohar
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
#ifndef __ACT_LAYOUT_SUBCELL_H__
#define __ACT_LAYOUT_SUBCELL_H__

#include "geom.h"

class SubcellInst {
private:
  LayoutBlob *_b;		//< the actual layout (subcell)
  TransformMat _m;		//< geometric transformations to get
				// the tiles into global coordinates
  const char *_uid;		//< unique identifier for the subcell
  int _nx, _ny;			//< array size

public:
  SubcellInst (LayoutBlob *b, const char *id, TransformMat *m = NULL) {
    _nx = 1;
    _ny = 1;
    _b = b;
    _uid = id;
    if (m) {
      _m = *m;
    }
  }

  void mkArray (int nx, int ny) {
    _nx = nx;
    _ny = ny;
  }

  LayoutEdgeAttrib *getLayoutEdgeAttrib () {
    LayoutEdgeAttrib *le;

    le = _b->getLayoutEdgeAttrib();
    if (le) {
      // clone, and apply transformation matrix
      le = le->Clone (&_m);
    }
    return le;
  }

  Rectangle getBBox() {
    Rectangle r;
    if (!_b) {
      return r;
    }
    r = _b->getBBox ();
    Rectangle a = _b->getAbutBox ();
    if (a.empty()) {
      a = r;
    }

    long fringex = (a.llx() - r.llx()) + (r.urx() - a.urx());
    long fringey = (a.lly() - r.lly()) + (r.ury() - a.ury());
    
    r.setRectCoords (r.llx(), r.lly(), r.llx() + a.wx()*_nx + fringex,
		     r.lly() + a.wy()*_ny + fringey);
    
    return r;
  }

  Rectangle getBloatBBox() {
    Rectangle r;
    if (!_b) {
      return r;
    }
    r = _b->getBBox ();
    Rectangle a = _b->getAbutBox ();
    if (a.empty()) {
      a = r;
    }
    r = _b->getBloatBBox ();

    long fringex = (a.llx() - r.llx()) + (r.urx() - a.urx());
    long fringey = (a.lly() - r.lly()) + (r.ury() - a.ury());
    
    r.setRectCoords (r.llx(), r.lly(), r.llx() + a.wx()*_nx + fringex,
		     r.lly() + a.wy()*_ny + fringey);

    return r;
  }

  Rectangle getAbutBox () {
    Rectangle r;
    if (!_b) {
      return r;
    }
    r = _b->getAbutBox ();
    if (r.empty()) {
      return getBBox();
    }
    r.setRect (r.llx(), r.lly(), r.wx()*_nx, r.wy()*_ny);
    return r;
  }

  void PrintRect (FILE *fp, TransformMat *mat, bool istopcell=true) {
    TransformMat m;
    if (mat) {
      m = *mat;
    }
    _b->_printRect (fp, mat, istopcell);
  }
};

class SubcellList {
 private:
  SubcellList *_next;		/* linked-list of subcells */
  SubcellInst *_cell;		/* actual subcell */
 public:
  
  SubcellList (SubcellInst *c) {
    _cell = c;
    _next = NULL;
  }
  
  ~SubcellList () {
    if (_next) {
      delete _next;
    }
  }

  void append (SubcellInst *c, bool sort_x) {
    SubcellList *tmp = new SubcellList (c);
    SubcellList *prev,  *cur;
    
    prev = NULL;
    cur = this;
    while (cur) {
      bool gonext;
      if (sort_x) {
	if (cur->_cell->getBBox().urx() < c->getBBox().llx()) {
	  gonext = true;
	}
	else {
	  gonext = false;
	}
      }
      else {
	if (cur->_cell->getBBox().ury() < c->getBBox().lly()) {
	  gonext = true;
	}
	else {
	  gonext = false;
	}
      }
      if (gonext) {
	prev = cur;
	cur = cur->_next;
      }
      else {
	if (!prev) {
	  SubcellInst *x;
	  tmp->_next = _next;
	  _next = tmp;
	  tmp->_cell = _cell;
	  _cell = c;
	  return;
	}
	else {
	  prev->_next = tmp;
	  tmp->_next = cur;
	  return;
	}
      }
    }
    prev->_next = tmp;
  }

  SubcellList *del (SubcellInst *c) {
    SubcellList *prev, *cur;
    
    prev = NULL;
    cur = this;
    while (cur) {
      if (cur->_cell == c) {
	break;
      }
      else {
	prev = cur;
	cur = cur->_next;
      }
    }
    if (cur) {
      if (prev) {
	prev->_next = cur->_next;
	delete cur;
	return this;
      }
      else {
	cur = cur->_next;
	delete this;
	return cur;
      }
    }
    return this;
  }

  SubcellList *getNext() { return _next; }
  SubcellInst *getCell() { return _cell; }
  void clearCell() { _cell = NULL; }
  SubcellList *flushClear();
};


/*
 * Recursively partition space
 */
class LayerSubcell {

 private:
  unsigned int _splitx:1;   /* 1 if my split was in the x-direction */
  long _splitval:62;	    /* location of split. the split
			       coordinate is in the "_leq" box. */
  Rectangle _region;	    /* owned region */
  Rectangle _bbox;	    /* actual bounding box */
  Rectangle _bloatbbox;	    /* bloated bbox */
  Rectangle _abutbox;	    /* abutment box */
  LayerSubcell *_leq, *_gt; /* split tile */
  SubcellList *_lst;	    /* list of subcells here */
  int _levelcount;	    /* list length */


  void _computeBBox();

 public:

  static int subcell_level_threshold; // if you exceed this threshold,
				      // then add sub-trees
  
  static int subcell_recompute_threshold; // if you exceed this
					  // threshold, then
					  // re-compute the entire subtree!
  
  LayerSubcell(bool sort_x = true) {
    Assert (subcell_recompute_threshold  > subcell_level_threshold, "What?");
    _splitx = sort_x ? 1 : 0;
    _splitval = 0;
    _leq = NULL;
    _gt = NULL;
    _lst = NULL;
    _levelcount = 0;
  }

  ~LayerSubcell() {
    if (_leq) {
      delete _leq;
    }
    if (_gt) {
      delete _gt;
    }
    if (_lst) {
      delete _lst;
    }
  }

  void initGlobal() {
    if (_leq || _gt || _lst) {
      fatal_error ("LayerSubcell:: initGlobal() called after subcells were added!");
    }
    _region.setRect (MIN_VALUE, MIN_VALUE, MAX_VALUE, MAX_VALUE);
  }

  void setRegion (Rectangle &r) {
    _region = r;
  }

  void addSubcell (SubcellInst *s);
  void delSubcell (SubcellInst *s);

  Rectangle getBBox ();
  Rectangle getBloatBBox ();
  Rectangle getAbutBox();
};


#endif /* __ACT_LAYOUT_SUBCELL_H__ */
