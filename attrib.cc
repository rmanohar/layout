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
#include "geom.h"

LayoutEdgeAttrib::attrib_list *LayoutEdgeAttrib::dup (attrib_list *l, long adj)
{
  if (!l) return NULL;
  LayoutEdgeAttrib::attrib_list *ret, *cur;
  NEW (ret, LayoutEdgeAttrib::attrib_list);
  ret->name = l->name;
  ret->offset = l->offset + adj;
  ret->next = NULL;
  cur = ret;
  while (l->next) {
    NEW (cur->next, attrib_list);
    cur = cur->next;
    l = l->next;
    cur->name = l->name;
    cur->offset = l->offset + adj;
    cur->next = NULL;
  }
  return ret;
}

/*
  Merge y into x.
  Both lists have to be sorted by offset.
*/
bool LayoutEdgeAttrib::merge (LayoutEdgeAttrib::attrib_list **x,
			      LayoutEdgeAttrib::attrib_list *y)
{
  if (!*x) {
    *x = y;
    return true;
  }
  attrib_list *prev, *cur;
  prev = NULL;
  cur = *x;
  while (y) {
    while (cur && cur->offset < y->offset) {
      prev = cur;
      cur = cur->next;
    }
    if (!cur) {
      prev->next = y;
      cur = y;
      y = y->next;
    }
    else {
      if (!prev) {
	prev = y;
	y = y->next;
	prev->next = cur;
	*x = prev;
      }
      else {
	prev->next = y;
	y = y->next;
	prev->next->next = cur;
	prev = prev->next;
      }
    }
  }
  return true;
}

void LayoutEdgeAttrib::freelist (LayoutEdgeAttrib::attrib_list *l) {
  LayoutEdgeAttrib::attrib_list *tmp;
  while (l) {
    tmp = l;
    l = l->next;
    FREE (tmp);
  }
}


void LayoutEdgeAttrib::print (FILE *fp, attrib_list *l)
{
  bool out = false;
  if (l) {
    out = true;
    fprintf (fp, " >[");
  }
  while (l) {
    fprintf (fp, " (%s %ld)", l->name, l->offset);
    l = l->next;
  }
  if (out) {
    fprintf (fp, " ]<");
  }
}


bool LayoutEdgeAttrib::align (attrib_list *l1, attrib_list *l2, long *amt)
{
  // no attributes: works with offset 0
  if (!l1 && !l2) {
    *amt = 0;
    return true;
  }
#if 0
  printf ("l1: ");
  print (stdout, l1);
  printf ("\n");
  printf ("l2: "); print (stdout, l2);
  printf ("\n\n");
#endif
  bool shift_computed = false;
  long shiftamt;

  while (l1 && l2) {
    if (strcmp (l1->name, l2->name) != 0) {
      return false;
    }
    if (shift_computed) {
      if (l2->offset + shiftamt != l1->offset) {
	return false;
      }
    }
    else {
      shiftamt = l1->offset - l2->offset;
      shift_computed = true;
    }
    l1 = l1->next;
    l2 = l2->next;
  }
  if (l1 || l2) return false;
  *amt = shiftamt;
  return true;
}


LayoutEdgeAttrib *LayoutEdgeAttrib::Clone()
{
  LayoutEdgeAttrib *ret = new LayoutEdgeAttrib();
  if (_left) {
    ret->_left = dup (_left);
  }
  if (_right) {
    ret->_right = dup (_right);
  }
  if (_top) {
    ret->_top = dup (_top);
  }
  if (_bot) {
    ret->_bot = dup (_bot);
  }
  return ret;
}


LayoutEdgeAttrib::attrib_list *
LayoutEdgeAttrib::flipsign (LayoutEdgeAttrib::attrib_list *x)
{
  if (!x) return NULL;
  attrib_list *prev, *cur;
  prev = NULL;
  cur = x;
  while (cur) {
    attrib_list *tmp;

    cur->offset = -cur->offset;
    
    tmp = cur->next;
    cur->next = prev;
    prev = cur;
    cur = cur->next;
  }
  return prev;
}

void LayoutEdgeAttrib::adjust (LayoutEdgeAttrib::attrib_list *x, long adj)
{
  while (x) {
    x->offset += adj;
    x = x->next;
  }
}

void LayoutEdgeAttrib::swaplr ()
{
  attrib_list *x;
  x = _left; _left = _right; _right = x;
  // flip sign of top/bot attribs
  _top = flipsign (_top);
  _bot = flipsign (_bot);
}
  
void LayoutEdgeAttrib::swaptb ()
{
  attrib_list *x;
  x = _top; _top = _bot; _bot = x;
  // flip sign of left/right attrib
  _left = flipsign (_left);
  _right = flipsign (_right);
}

void LayoutEdgeAttrib::swap45()
{
  attrib_list *x;
  x = _left; _left = _bot; _bot = x;
  x = _right; _right = _top; _top = x;
}


/*
 * Clone the attributes, and adjust coordinates based on the
 * transformation matrix
 */
LayoutEdgeAttrib *LayoutEdgeAttrib::Clone (TransformMat *m)
{
  LayoutEdgeAttrib *le;
  // apply transform matrix to see what happens to the orientation
  // of a box!
  long dx, dy, px, py;

  le = Clone ();

  if (m) {
    // We transform a canonical rectangle; that way the code can stay
    // the same even if the representation of m changes.
    m->apply (0, 0, &dx, &dy); // point 0 0 -> x y is the translation
    m->apply (1, 2, &px, &py); // look for rotations/flips
  }
  else {
    dx = 0;
    dy = 0;
    px = 1;
    py = 2;
  }
  
  px -= dx;
  py -= dy;

  if ((px < 0 ? -px : px) != 1) {
    // x and y have been swapped
    le->swap45 ();
  }
  // we just have mirrors
  if (px < 0) {
    le->swaplr();
  }
  if (py < 0) {
    le->swaptb();
  }

  // now adjust list with dx and dy
  if (dx != 0) {
    le->adjust (_top, dx);
    le->adjust (_bot, dx);
  }
  if (dy != 0) {
    le->adjust (_left, dy);
    le->adjust (_right, dy);
  }
  
  return le;
}
