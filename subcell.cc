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
#include "subcell.h"

int LayerSubcell::subcell_level_threshold = 10;

// This must be always larger than subcell_level_threshold
int LayerSubcell::subcell_recompute_threshold = 200;

void LayerSubcell::addSubcell (SubcellInst *s)
{
  const Rectangle &r = s->getBBox ();
  Assert (_region.contains (r), "What?");
  if (_bbox.empty()) {
    _computeBBox();
  }
  _bbox = _bbox ^ r;
  if (_leq || _gt) {
    if (_leq->_splitx) {
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
	  _lst = new SubcellList (s);
	}
	else {
	  _lst->append (s, _splitx == 1 ? true : false);
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
	  _lst = new SubcellList (s);
	}
	else {
	  _lst->append (s, _splitx == 1 ? true : false);
	}
      }
    }
    if (_levelcount > subcell_level_threshold) {
      if (!_leq && !_gt) {
	// find an x-split, and find a y-split
	SubcellList *tmp;
	double x_mean, y_mean;
	x_mean = 0;
	y_mean = 0;
	for (tmp = _lst; tmp; tmp = tmp->getNext()) {
	  SubcellInst *c = tmp->getCell();
	  x_mean += (c->getBBox().llx() + c->getBBox().urx())/2;
	  y_mean += (c->getBBox().lly() + c->getBBox().ury())/2;
	}
	x_mean /= _levelcount;
	y_mean /= _levelcount;

	int x_count_left, y_count_left;
	x_count_left = 0;
	y_count_left = 0;

	for (tmp = _lst; tmp; tmp = tmp->getNext()) {
	  SubcellInst *c = tmp->getCell();
	  if (c->getBBox().urx() <= (long)x_mean) {
	    x_count_left++;
	  }
	  if (c->getBBox().ury() <= (long)y_mean) {
	    y_count_left++;
	  }
	}
#ifndef ABS
#define ABS(a) ((a) < 0 ? -(a) : (a))
#endif
	unsigned int x_gap, y_gap;
	x_gap = ABS(_levelcount/2-x_count_left);
	y_gap = ABS(_levelcount/2-y_count_left);


	// create left, right sub-trees
	if (x_gap < y_gap || (x_gap == y_gap && _splitx == 0)) {
	  _leq = new LayerSubcell (true);
	  _gt = new LayerSubcell (true);
	  _splitval = x_mean;

	  // split in the x direction. 
	  Rectangle r = _region;
	  r.setXMax (_splitval);
	  _leq->setRegion (r);
	  r = _region;
	  r.setXMin (_splitval+1);
	  _gt->setRegion (r);
	}
	else {
	  _splitval = y_mean;
	  _leq = new LayerSubcell (false);
	  _gt = new LayerSubcell (false);

	  // split in the y direction
	  Rectangle r = _region;
	  r.setYMax (_splitval);
	  _leq->setRegion (r);
	  r = _region;
	  r.setYMin (_splitval+1);
	  _gt->setRegion (r);
	}
	SubcellList *newlst = NULL;
	for (tmp = _lst; tmp; tmp = tmp->getNext()) {
	  SubcellInst *c = tmp->getCell();
	  if (_leq->_splitx) {
	    if (c->getBBox().urx() <= _splitval) {
	      _leq->addSubcell (c);
	      tmp->clearCell();
	      _levelcount--;
	    }
	    else if (c->getBBox().llx() > _splitval) {
	      _gt->addSubcell (c);
	      tmp->clearCell();
	      _levelcount--;
	    }
	  }
	  else {
	    if (c->getBBox().ury() <= _splitval) {
	      _leq->addSubcell (c);
	      tmp->clearCell();
	      _levelcount--;
	    }
	    else if (c->getBBox().lly() > _splitval) {
	      _gt->addSubcell (c);
	      tmp->clearCell();
	      _levelcount--;
	    }
	  }
	}
	// now delete the subcell that were moved out
	_lst = _lst->flushClear ();
      }
      else if (_levelcount > subcell_recompute_threshold) {
	// XXX: fixme: re-partition data structure
      }
    }
  }
}
  

void LayerSubcell::delSubcell (SubcellInst *s)
{
  const Rectangle &r = s->getBBox ();
  Assert (_region.contains (r), "What?");
  _bbox.clear ();
  _bloatbbox.clear();
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




void LayerSubcell::_computeBBox ()
{
  SubcellList *l;
  _bbox.clear ();
  _bloatbbox.clear ();
  _abutbox.clear ();
  for (l = _lst; l; l = _lst->getNext()) {
    _bbox = _bbox ^ l->getCell()->getBBox ();
    _bloatbbox = _bloatbbox ^ l->getCell()->getBloatBBox();
    _abutbox = _abutbox ^ l->getCell()->getAbutBox ();
  }
  if (_leq) {
    _bbox = _bbox ^ _leq->getBBox();
    _bloatbbox = _bloatbbox ^ _leq->getBloatBBox();
    _abutbox = _abutbox ^ _leq->getAbutBox();
  }
  if (_gt) {
    _bbox = _bbox ^ _gt->getBBox();
    _bloatbbox = _bloatbbox ^ _gt->getBloatBBox();
    _abutbox = _abutbox ^ _gt->getAbutBox();
  }
}

Rectangle LayerSubcell::getBBox ()
{
  if (_bbox.empty()) {
    _computeBBox();
  }
  return _bbox;
}

Rectangle LayerSubcell::getBloatBBox ()
{
  if (_bloatbbox.empty()) {
    _computeBBox();
  }
  return _bloatbbox;
}

Rectangle LayerSubcell::getAbutBox ()
{
  if (_bbox.empty()) {
    _computeBBox();
  }
  return _abutbox;
}


SubcellInst::SubcellInst (LayoutBlob *b, const char *id, const char *name,
			  TransformMat *m)
{
  _nx = 1;
  _ny = 1;
  _b = b;
  _uid = id;
  _name = name;
  if (m) {
    _m = *m;
  }
}

void SubcellInst::mkArray (int nx, int pitchx, int ny, int pitchy)
{
  _nx = nx;
  _ny = ny;
  _px = pitchx;
  _py = pitchy;
}

LayoutEdgeAttrib *SubcellInst::getLayoutEdgeAttrib ()
{
  LayoutEdgeAttrib *le;

  le = _b->getLayoutEdgeAttrib();
  if (le) {
    // clone, and apply transformation matrix
    le = le->Clone (&_m);
  }
  return le;
}

Rectangle SubcellInst::getBBox()
{
  Assert (0, "What?");

  Rectangle r;
  if (!_b) {
    return r;
  }
  r = _b->getBBox ();
  Rectangle a = _b->getAbutBox ();
  if (a.empty()) {
    a = r;
  }

  // XXX: this is wrong

  long fringex = (a.llx() - r.llx()) + (r.urx() - a.urx());
  long fringey = (a.lly() - r.lly()) + (r.ury() - a.ury());

  r.setRectCoords (r.llx(), r.lly(), r.llx() + a.wx()*_nx + fringex,
		   r.lly() + a.wy()*_ny + fringey);

  return r;
}

Rectangle SubcellInst::getBloatBBox()
{
  Assert (0, "What?");

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

  // XXX: this is wrong

  long fringex = (a.llx() - r.llx()) + (r.urx() - a.urx());
  long fringey = (a.lly() - r.lly()) + (r.ury() - a.ury());

  r.setRectCoords (r.llx(), r.lly(), r.llx() + a.wx()*_nx + fringex,
		   r.lly() + a.wy()*_ny + fringey);

  return r;
}


Rectangle SubcellInst::getAbutBox ()
{
  Assert (0, "What?");

  Rectangle r;
  if (!_b) {
    return r;
  }
  r = _b->getAbutBox ();
  if (r.empty()) {
    return getBBox();
  }
  // XXX: this is wrong
  r.setRect (r.llx(), r.lly(), r.wx()*_nx, r.wy()*_ny);
  return r;
}


void SubcellInst::PrintRect (FILE *fp, TransformMat *mat)
{
  TransformMat m = _m;
  fprintf (fp, "cell %s %s ", _name, _uid);
  if (mat) {
    m.applyMat (*mat);
    m.PrintRect (fp);
  }
  else {
    fprintf (fp, "N 0 0");
  }
  if (_nx > 1 || _ny > 1) {
    fprintf (fp, " arr %d %d %d %d",
	     _nx, _px, _ny, _py);
  }
  fprintf (fp, "\n");
}


void SubcellList::append (SubcellInst *c, bool sort_x)
{
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

SubcellList *SubcellList::del (SubcellInst *c)
{
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

SubcellList *SubcellList::flushClear ()
{
  SubcellList *tmp, *prev, *cur;
  prev = NULL;
  cur = this;
  while (cur) {
    if (!cur->_cell) {
      if (prev) {
	prev->_next = cur->_next;
	tmp = cur;
	cur = cur->_next;
	delete tmp;
      }
      else {
	if (cur->_next) {
	  tmp = cur->_next;
	  cur->_cell = tmp->_cell;
	  cur->_next = tmp->_next;
	  delete tmp;
	  cur = cur->_next;
	}
	else {
	  delete this;
	  return NULL;
	}
      }
    }
    else {
      prev = cur;
      cur = cur->_next;
    }
  }
  if (!prev) {
    return NULL;
  }
  return this;
}
