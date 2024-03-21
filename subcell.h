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

class Subcell {
private:
  LayoutBlob *_b;		//< the actual layout
  TransformMat _m;		//< geometric transformations to get
				// the tiles into global coordinates
  const char *_uid;		//< unique identifier for the subcell
  Rectangle _bbox;

public:
  Subcell (LayoutBlob *b, const char *id, TransformMat *m = NULL) {
    _b = b;
    _uid = id;
    if (m) {
      _m = *m;
    }
    long llx, lly, urx, ury;
    b->getBBox (&llx, &lly, &urx, &ury);
    _bbox.setRectCoords (llx, lly, urx, ury);
  }

  Rectangle &getBBox() { return _bbox; }
};

class LayoutSubcells {
 private:
  LayoutSubcells *_next;	/* linked-list of subcells */
  Subcell *_cell;		/* actual subcell */
 public:
  
  LayoutSubcells (Subcell *c) {
    _cell = c;
    _next = NULL;
  }
  
  ~LayoutSubcells () {
    if (_next) {
      delete _next;
    }
  }

  void append (Subcell *c) {
    LayoutSubcells *tmp = new LayoutSubcells (c);
    tmp->_next = _next;
    _next = tmp;
  }

  void del (Subcell *c) {
    if (c == _cell) {
      LayoutSubcells *tmp = _next;
      if (tmp) {
	_cell = tmp->_cell;
	_next = tmp->_next;
	delete tmp;
      }
      else {
	_cell = NULL;
      }
    }
  }    
};


/*
 * Recursively partition space
 */
class LayerSubcell {

 private:
  unsigned int _splitx:1;   /* 1 if the split is in the x-direction */
  long _splitval:63;	    /* location of split. the split
			       coordinate is in the "_leq" box. */
  Rectangle _bbox;	    /* actual bounding box */
  LayerSubcell *_leq, *_gt; /* split tile */
  LayoutSubcells *_lst;	    /* list of subcells here */
  int _levelcount;	    /* list length */

 public:
  LayerSubcell() {
    _splitx = 0;
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
    _bbox.setRect (MIN_VALUE, MIN_VALUE, MAX_VALUE, MAX_VALUE);
  }

  void addSubcell (Subcell *s) {
    Rectangle &r = s->getBBox ();
    Assert (_bbox.contains (r), "What?");
    if (_leq || _gt) {
      if (_splitx) {
	if (_splitval < r.llx()) {
	  _gt->addSubcell (s);
	}
	else if (r.urx() <= _splitval) {
	  _leq->addSubcell (s);
	}
	else {
	  // add to this level
	  _levelcount++;
	  if (!_lst) {
	    _lst = new LayoutSubcells (s);
	  }
	  else {
	    _lst->append (s);
	  }
	}
      }
      else {
	if (_splitval < r.lly()) {
	  _gt->addSubcell (s);
	}
	else if (r.ury() <= _splitval) {
	  _leq->addSubcell (s);
	}
	else {
	  _levelcount++;
	  if (!_lst) {
	    _lst = new LayoutSubcells (s);
	  }
	  else {
	    _lst->append (s);
	  }
	}
      }
    }
  }
  void delSubcell (Subcell *s) {
    Rectangle &r = s->getBBox ();
    Assert (_bbox.contains (r), "What?");
    if (_leq || _gt) {
      if (_splitx) {
	if (_splitval < r.llx()) {
	  _gt->delSubcell (s);
	}
	else if (r.urx() <= _splitval) {
	  _leq->delSubcell (s);
	}
	else {
	  _levelcount--;
	  Assert (_lst, "What?");
	  _lst->del (s);
	  if (_levelcount == 0) {
	    delete _lst;
	    _lst = NULL;
	  }
	}
      }
      else {
	if (_splitval < r.lly()) {
	  _gt->addSubcell (s);
	}
	else if (r.ury() <= _splitval) {
	  _leq->addSubcell (s);
	}
	else {
	  _levelcount--;
	  Assert (_lst, "What?");
	  _lst->del (s);
	  if (_levelcount == 0) {
	    delete _lst;
	    _lst = NULL;
	  }
	}
      }
    }
  }
};


#endif /* __ACT_LAYOUT_SUBCELL_H__ */
