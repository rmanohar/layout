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
#ifndef __ACT_LAYOUT_ATTRIB_H__
#define __ACT_LAYOUT_ATTRIB_H__

class TransformMat;

/*
 * These are used as alignment markers. They can be used for an umber
 * of different purposes, including multi-deck gridded cells.
 *
 * **Currently only using an attrib_list of length at most 1 until we
 *   include multi-deck support**
 */
class LayoutEdgeAttrib {
public:
  struct attrib_list {
    const char *name;
    long offset;		// this offset is relative to assuming
				// the bottom left corner of the
				// actual layout bounding box is (0,0).
    struct attrib_list *next;
  };
private:

  /* at the moment, we have the same attribute for the horizontal edge
     as the vertical edge
  */
  attrib_list *_left, *_right, *_top, *_bot;

  attrib_list *dup (attrib_list *l, long adj = 0);

  /*
    Merge y into x.
    Both lists have to be sorted by offset.
   */
  bool merge (attrib_list **x, attrib_list *y);

  void freelist (attrib_list *l);

public:
  LayoutEdgeAttrib() {
    _left = NULL;
    _right = NULL;
    _top = NULL;
    _bot = NULL;
  }

  ~LayoutEdgeAttrib() {
    clear ();
  }


  void mkCopy (LayoutEdgeAttrib &le) {
    clearleft ();
    clearright ();
    cleartop ();
    clearbot ();
    _left = dup (le._left);
    _right = dup (le._right);
    _top = dup (le._top);
    _bot = dup (le._bot);
  }

  void clearleft() {
    freelist (_left);
    _left = NULL;
  }
  void clearright() {
    freelist (_right);
    _right = NULL;
  }
  void cleartop() {
    freelist (_top);
    _top = NULL;
  }
  void clearbot() {
    freelist (_bot);
    _bot = NULL;
  }

  void clear () {
    clearleft();
    clearright();
    cleartop();
    clearbot();
  }

  attrib_list *left() { return _left; }
  attrib_list *right() { return _right; }
  attrib_list *top() { return _top; }
  attrib_list *bot() { return _bot; }

  static void print (FILE *fp, attrib_list *l);
  
  /* compute alignment between two sets of markers; returns amt that
     should be added to l2 to get to l1's offset */

  /* XXX: when we support multiple independent attributes, we will
     need multiple shift amounts for alignment
  */
  static bool align (attrib_list *l1, attrib_list *l2, long *amt);

  void setleft(attrib_list *x, long adj = 0) {
    clearleft();
    _left = dup (x, adj);
  }
  void setright(attrib_list *x, long adj = 0) {
    clearright();
    _right = dup(x, adj);
  }
  void settop(attrib_list *x, long adj = 0) {
    cleartop();
    _top = dup (x, adj);
  }
  void setbot(attrib_list *x, long adj = 0) {
    clearbot();
    _bot = dup (x, adj);
  }

  bool mergeleft(attrib_list *x, long adj = 0) {
    return merge (&_left, dup (x, adj));
  }
  bool mergeright(attrib_list *x, long adj = 0) {
    return merge (&_right, dup (x, adj));
  }
  bool mergetop(attrib_list *x, long adj = 0) {
    return merge (&_top, dup (x, adj));
  }
  bool mergebot(attrib_list *x, long adj = 0) {
    return merge (&_bot, dup (x, adj));
  }

  void swaplr ();
  void swaptb ();
  void swap45();

  attrib_list *flipsign (attrib_list *x);
  void adjust (attrib_list *x, long adj);

  LayoutEdgeAttrib *Clone();
  LayoutEdgeAttrib *Clone (TransformMat *m);
};


#endif /* __ACT_LAYOUT_ATTRIB_H__ */
