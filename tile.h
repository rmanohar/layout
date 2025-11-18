/*************************************************************************
 *
 *  Copyright (c) 2020 Rajit Manohar
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
#ifndef __ACT_TILE_H__
#define __ACT_TILE_H__


#define MIN_VALUE (signed long)(1UL << (8*sizeof(long)-1))
#define MAX_VALUE (signed long)((1UL << (8*sizeof(long)-1))-1)


/*------------------------------------------------------------------------
 *
 *  Coordinate system:
 *
 *    - integer grid points
 *    - a tile has llx, lly: these points are *in* the tile
 *    - a tile has urx, ury: these points are also *in* the tile
 *
 *  So the total dimensions of the tile will be:
 *       (urx - llx + 1) by (ury - lly + 1)
 *
 *  An empty tile/bbox is specified by: (0,0) to (-1,-1)
 *
 *  Example: (0,0) to (0,0) will be a tile that has one grid point in
 *  it
 *
 *------------------------------------------------------------------------
 */

/* 
   off field in the macro below 
*/
#define FET_OFFSET 0
#define DIFF_OFFSET 1
#define WDIFF_OFFSET 2

#define NUM_MINOR_OFFSET 3 // the # of offset values

#define OFFSET_ONE_FLAVOR(type,off) ((type) + 2*(off))

/* 
   flavor = transistor flavor 
   type = EDGE_NFET or EDGE_PFET
   off = offsets defined above: FET/DIFF

Order:

     n/p + 2*(fet/diff/wdiff) + 6*(flavor)

  (ndiff has pwelldiff associated with it)

  [ nfet pfet ndiff pdiff pwdiff nwdiff ] flavor0
  [ nfet pfet ndiff pdiff pwdiff nwdiff ] flavor1
  ...
*/

#define ATTR_TYPE_PFET EDGE_PFET
#define ATTR_TYPE_NFET EDGE_NFET

#define TOTAL_OFFSET(flavor,type,off) (2*NUM_MINOR_OFFSET*(flavor)+OFFSET_ONE_FLAVOR(type,off))

#define TILE_FLGS_TO_ATTR(flavor,type,off)  (1+TOTAL_OFFSET(flavor,type,off))
#define TILE_ATTR_NONPOLY(x) ((x)-1)

#define TILE_ATTR_TO_FLAV(attr)   (TILE_ATTR_NONPOLY(attr)/(2*NUM_MINOR_OFFSET))
#define TILE_ATTR_TO_OFF(attr) ((TILE_ATTR_NONPOLY(attr) % (2*NUM_MINOR_OFFSET))/2)
#define TILE_ATTR_TO_TYPE(attr)  (TILE_ATTR_NONPOLY(attr) % 2)

/* pins are only on metal layers */
#define TILE_ATTR_ISPIN(x)  ((x) & 4)
#define TILE_ATTR_MKPIN(x)  ((x) |= 4)
#define TILE_ATTR_ISOUTPUT(x)  ((x) & 8)
#define TILE_ATTR_MKOUTPUT(x) ((x) |= 8)
#define TILE_ATTR_CLRPIN(x) ((x) & ~(4|8))

#define TILE_ATTR_ISFET(x)   (TILE_ATTR_TO_OFF(x) == FET_OFFSET)
#define TILE_ATTR_ISDIFF(x)  (TILE_ATTR_TO_OFF(x) == DIFF_OFFSET)
#define TILE_ATTR_ISWDIFF(x) (TILE_ATTR_TO_OFF(x) == WDIFF_OFFSET)
#define TILE_ATTR_ISROUTE(x) ((x) == 0)

class Layer;

#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

class Rectangle {
private:
  long _llx, _lly;
  unsigned long _wx, _wy;

public:
  /* initialize with the empty rectangle */
  Rectangle() {
    clear();
  }

  void clear() {
    _llx = 0;
    _lly = 0;
    _wx = 0;
    _wy = 0;
  }

  int operator !=(const Rectangle&r) const {
    if (_llx != r._llx || _lly != r._lly ||
	_wx != r._wx || _wy != r._wy) {
      return true;
    }
    return false;
  }

  bool empty() const { return (_wx == 0) && (_wy == 0); }

  long llx() const { return _llx; }
  long lly() const { return _lly; }
  unsigned long wx() const { return _wx; }
  unsigned long wy() const { return _wy; }
  long urx() const { return _llx + _wx - 1; }
  long ury() const { return _lly + _wy - 1; }
  bool inXRange(long x) const {
    return (llx() <= x) && (x <= urx());
  }
  bool inYRange (long y) const {
    return (lly() <= y) && (y <= ury());
  }
  bool inRect(long x, long y) const {
    return inXRange(x) && inYRange(y);
  }

  void print(FILE *fp) {
    fprintf (fp, "(%ld,%ld) -> (%ld,%ld)", llx(), lly(), urx(), ury());
  }

  bool contains (const Rectangle &r) const {
    if (llx() <= r.llx() && lly() <= r.lly() &&
	r.urx() <= urx() && r.ury() <= ury()) {
      return true;
    }
    return false;
  }

  void setXMax (long xval) {
    Assert (xval >= _llx, "What?");
    _wx = xval - _llx + 1;
  }
  
  void setXMin (long xval) {
    Assert (xval <= urx(), "What?");
    _wx = _wx - (xval - _llx);
    _llx = xval;
  }

  void setYMax (long yval) {
    Assert (yval >= _lly, "What?");
    _wy = yval - _lly + 1;
  }
  
  void setYMin (long yval) {
    Assert (yval <= ury(), "What?");
    _wy = _wy - (yval - _lly);
    _lly = yval;
  }
  
  void setRect (long lx, long ly, unsigned long wx, unsigned long wy) {
    _llx = lx;
    _lly = ly;
    _wx = wx;
    _wy = wy;
  }

  void shiftx (long dx) {
    if (empty()) return;
    _llx += dx;
  }

  void shifty (long dy) {
    if (empty()) return;
    _lly += dy;
  }

  void setRectCoords (long lx, long ly, long ux, long uy) {
    Assert (lx <= ux || (lx == 0 && ux == -1), "What?");
    Assert (ly <= uy || (ly == 0 && uy == -1), "What?");
    if (lx == 0 && ux == -1 || ly == 0 && uy == -1) {
      Assert (lx == 0 && ux == -1 && ly == 0 && uy == -1, "What?");
    }
    _llx = lx;
    _lly = ly;
    _wx = (ux - lx + 1);
    _wy = (uy - ly + 1);
  }

  // max operator
  Rectangle operator^(const Rectangle& r) {
    Rectangle t = *this;
    if (r.empty()) return t;
    if (t.empty()) return r;

    long turx, tury;
    turx = MAX(urx(), r.urx());
    tury = MAX(ury(), r.ury());

    t._llx = MIN(t._llx, r._llx);
    t._lly = MIN(t._lly, r._lly);
    t._wx = turx - t._llx + 1;
    t._wy = tury - t._lly + 1;
    return t;
  }
}
;

class Tile {
 private:
  //int idx;
  
  struct {
    Tile *x, *y;
  } ll, ur;
  long llx, lly;		// lower left corner
  //Tile *up, *down;
  unsigned int space:1;		/* 1 if this is a space tile */
  unsigned int virt:1;		// virtual tile: used to *add* spacing
				// constraints
  unsigned int attr:6;		/* up to 6 bits of "attributes" 
				   0 = routing layer
				   1 to 63 used for other
				   things---subtract 1 to find out
				   which Material * it corresponds to
				   in the Layer structure that
				   contains this tile.
				 */

  void *net;			// the net associated with this tile,
				// if it is not a space tile. NULL = no net

  Tile *find (long x, long y);
  Tile *splitX (long x);
  Tile *splitY (long y);
  list_t *collectRect (long _llx, long _lly,
		       unsigned long wx, unsigned long wy);
  list_t *collectRect (Rectangle &r) { return collectRect (r.llx(), r.lly(),
							   r.wx(), r.wy()); }
  
  int xmatch (long x) { return (llx <= x) && (!ur.x || (x < ur.x->llx)); }
  int ymatch (long y) { return (lly <= y) && (!ur.y || (y < ur.y->lly)); }
  long nextx() { return ur.x ? ur.x->llx : MAX_VALUE; }
  long nexty() { return ur.y ? ur.y->lly : MAX_VALUE; }


  void applyTiles (long _llx, long _lly, unsigned long wx, unsigned long wy,
		   void *cookie, void (*f) (void *, Tile *));
  void applyTiles (Rectangle &r, void *cookie, void (*f)(void *, Tile *)) {
    applyTiles (r.llx(), r.lly(), r.wx(), r.wy(), cookie, f);
  }

  int isPin() { return TILE_ATTR_ISPIN(attr); }

 public:
  Tile ();
  ~Tile ();

  /*
    Cuts tiles and returns a tile with this precise shape
    If it would involve two different tiles of different types, then
    it will flag it as an error.
  */
  Tile *addRect (long _llx, long _lly,
		 unsigned long wx, unsigned long wy,
		 bool force = false);
  Tile *addRect (Rectangle &r, bool force = false) {
    return addRect (r.llx(), r.lly(), r.wx(), r.wy(), force);
  }
  
  int addVirt (int flavor, int type,
	       long _llx, long _lly,
	       unsigned long wx, unsigned long wy);
  int addVirt (int flavor, int type, Rectangle &r) {
    return addVirt (flavor, type, r.llx(), r.lly(), r.wx(), r.wy());
  }

  Tile *llxTile() { return ll.x; }
  Tile *urxTile() { return ur.x; }
  Tile *llyTile() { return ll.y; }
  Tile *uryTile() { return ur.y; }

  long geturx() { return nextx()-1; }
  long getury() { return nexty()-1; }
  long getllx() { return llx; }
  long getlly() { return lly; }
  int isSpace() { return space; }
  unsigned int getAttr() { return attr; }
  unsigned int isVirt() { return virt; }
  void *getNet ()  { return net; }
  void setNet (void *n) { net = n; }
  int isBaseSpace() { return isSpace() || (isVirt() && TILE_ATTR_ISDIFF(attr)); }
  int isFet() { return !virt && !TILE_ATTR_ISROUTE(attr) && TILE_ATTR_ISFET(attr); }
  int isPoly() { return TILE_ATTR_ISROUTE(attr) || (virt && TILE_ATTR_ISFET(attr)); }
  int isDiff() { return !virt && !TILE_ATTR_ISROUTE(attr) && TILE_ATTR_ISDIFF(attr); }

  static int isConnected (Layer *l, Tile *t1, Tile *t2);
  
  friend class Layer;
};


#endif /* __ACT_TILE_H__ */
