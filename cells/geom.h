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
#ifndef __ACT_GEOM_H__
#define __ACT_GEOM_H__

#include <act/tech.h>

#define MIN_VALUE (signed long)(1UL << (8*sizeof(long)-1))
#define MAX_VALUE (signed long)((1UL << (8*sizeof(long)-1))-1)


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

#define TILE_ATTR_ISFET(x)   (TILE_ATTR_TO_OFF(x) == FET_OFFSET)
#define TILE_ATTR_ISDIFF(x)  (TILE_ATTR_TO_OFF(x) == DIFF_OFFSET)
#define TILE_ATTR_ISWDIFF(x) (TILE_ATTR_TO_OFF(x) == WELL_OFFSET)
#define TILE_ATTR_ISROUTE(x) ((x) == 0)


class Tile {
 private:
  //int idx;
  
  struct {
    Tile *x, *y;
  } ll, ur;
  long llx, lly;		// lower left corner
  Tile *up, *down;
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

  int xmatch (long x) { return (llx <= x) && (!ur.x || (x < ur.x->llx)); }
  int ymatch (long y) { return (lly <= y) && (!ur.y || (y < ur.y->lly)); }
  long nextx() { return ur.x ? ur.x->llx : MAX_VALUE; }
  long nexty() { return ur.y ? ur.y->lly : MAX_VALUE; }


  void applyTiles (long _llx, long _lly, unsigned long wx, unsigned long wy,
		   void *cookie, void (*f) (void *, Tile *));


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
  int addVirt (int flavor, int type,
	       long _llx, long _lly,
	       unsigned long wx, unsigned long wy);

  long geturx() { return nextx()-1; }
  long getury() { return nexty()-1; }
  long getllx() { return llx; }
  long getlly() { return lly; }
  int isSpace() { return space; }
  unsigned int getAttr() { return attr; }
  unsigned int isVirt() { return virt; }
  void *getNet ()  { return net; }
  
  friend class Layer;
};


class TransformMat {
  long m[3][3];
public:
  TransformMat ();

  void mkI();

  void applyRot90 ();
  void applyTranslate (long dx, long dy);
  void mirrorLR ();
  void mirrorTB ();

  void apply (long inx, long iny, long *outx, long *outy);
};


class Layer {
protected:
  Material *mat;		/* technology-specific
				   information. routing material for
				   the layer. */
  Tile *hint;			/* tile containing 0,0 / last lookup */

  Tile *vhint;			// tile layer containing vias to the
				// next (upper) layer
  
  Layer *up, *down;		/* layer above and below */
  
  Material **other;	       // for the base layer, fet + diff
  int nother;

  netlist_t *N;

  unsigned int bbox:1;		// 1 if bbox below is valid
  long _llx, _lly, _urx, _ury;

 public:
  Layer (Material *, netlist_t *);
  ~Layer ();

  void allocOther (int sz);
  void setOther (int idx, Material *m);
  void setDownLink (Layer *x);

  int Draw (long llx, long lly, unsigned long wx, unsigned long wy, void *net, int type = 0);
  int Draw (long llx, long lly, unsigned long wx, unsigned long wy, int type = 0);
  int DrawVirt (int flavor, int type, long llx, long lly, unsigned long wx, unsigned long wy);

  int drawVia (long llx, long lly, unsigned long wx, unsigned long wy, void *net, int type = 0);
  int drawVia (long llx, long lly, unsigned long wx, unsigned long wy, int type = 0);

  int isMetal ();
  
  list_t *search (void *net);
  list_t *search (int attr);

  /* type = -1 : all! */
  void BBox (long *llx, long *lly, long *urx, long *ury, int type = -1);

  void PrintRect (FILE *fp, TransformMat *t = NULL);

  const char *getRouteName() { return mat->getName(); }

  friend class Layout;
};


class Layout {
public:
  static bool _initdone;
  static void Init();
  
  /* 
     The base layer is special as this is where the transistors are
     drawn. It includes poly, fets, diffusion, and virtual diffusion.
  */
  Layout (netlist_t *);
  ~Layout();

  int DrawPoly (long llx, long lly, unsigned long wx, unsigned long wy, void *net);
  int DrawDiff (int flavor, int type, long llx, long lly, unsigned long wx, unsigned long wy, void *net);
  int DrawFet (int flavor, int type, long llx, long lly, unsigned long wx, unsigned long wy, void *net);
  int DrawDiffBBox (int flavor, int type, long llx, long lly, unsigned long wx, unsigned long wy);

  /* 0 = metal1, etc. */
  int DrawMetal (int num, long llx, long lly, unsigned long wx, unsigned long wy, void *net);
  
  int DrawMetalPin (int num, long llx, long lly,
		    unsigned long wx, unsigned long wy,
		    void *net, int dir); /* dir 0 = input, 1 = output */

  /* 0 = base to metal1, 1 = metal1 to metal2, etc. */
  int DrawVia (int num, long llx, long lly, unsigned long wx, unsigned long wy);

  Layer *getLayerPoly () { return base; }
  Layer *getLayerDiff () { return base; }
  Layer *getLayerWell () { return base; }
  Layer *getLayerFet ()  { return base; }
  Layer *getLayerMetal (int n) { return metals[n]; }
  

  PolyMat *getPoly ();
  FetMat *getFet (int type, int flavor = 0); // type == EDGE_NFET or EDGE_PFET
  DiffMat *getDiff (int type, int flavor = 0);
  WellMat *getWell (int type, int flavor = 0);
  // NOTE: WELL TYPE is NOT THE TYPE OF THE WELL MATERIAL, BUT THE TYPE OF
  // THE FET THAT NEEDS THE WELL.

  void getBBox (long *llx, long *lly, long *urx, long *ury);

  void PrintRect (FILE *fp, TransformMat *t = NULL);

  list_t *search (void *net);
  list_t *search (int attr);
  
private:
  Layer *base;
  Layer **metals;
  int nflavors;
  int nmetals;
  netlist_t *N;
};


class LayoutBlob;

enum blob_type { BLOB_BASE, BLOB_HORIZ, BLOB_VERT, BLOB_MERGE };
enum mirror_type { MIRROR_NONE, MIRROR_LR, MIRROR_TB, MIRROR_BOTH };
   // do we want to support 90 degree rotation?

struct blob_list {
  LayoutBlob *b;
  long gap;
  long shift;			// shift in the other direction
  mirror_type mirror;
  struct blob_list *next;
};

struct tile_listentry {
  TransformMat m;  /**< the coordinate transformation matrix */
  list_t *tiles;   /**< a list alternating between Layer pointer and a
		      list of tiles  */
};

class LayoutEdgeAttrib {

private:
  // well attributes
  struct well_info {
    int offset;
    unsigned int plugged:1;
    unsigned int flavor:7;
  } *wells;
  int wellcnt;

  struct mat_info {
    int offset;
    int width;
    Material *m;
  } *mats;
  int matcnt;

public:
  int getWellCount () { return wellcnt; }

};

class LayoutBlob {
private:
  union {
    struct {
      blob_list *hd, *tl;
    } l;			// a blob list
    struct {
      Layout *l;		// ... layout block
    } base;
  };
  blob_type t;			// type field: 0 = base, 1 = horiz,
				// 2 = vert

  long llx, lly, urx, ury;	// bounding box

#define LAYOUT_EDGE_LEFT 0  
#define LAYOUT_EDGE_RIGHT 2
#define LAYOUT_EDGE_TOP 1
#define LAYOUT_EDGE_BOTTOM 3

  LayoutEdgeAttrib *edges[4];	// 0 = l, 1 = t, 2 = r, 3 = b
  
public:
  LayoutBlob (blob_type type, Layout *l = NULL);
  ~LayoutBlob ();

  void appendBlob (LayoutBlob *b, long gap = 10, mirror_type m = MIRROR_NONE);

  void PrintRect (FILE *fp, TransformMat *t = NULL);

  /**
   * Computes the actual bounding box of the layout blob
   *
   * @param llxp, llyp, urxp, uryp are used to return the boundary.
   */
  void getBBox (long *llxp, long *llyp, long *urxp, long *uryp);

  /**
   * Returns a list of tiles in the layout that match the net
   *  @param net is the net pointer (a node_t)
   *  @param m should not be used at the top-level, but provides the
   *  current transformatiom matrix used by the recursive call to the
   *  search function.
   *  @return a list_t of tile_listentry tiles.
   */
  list_t *search (void *net, TransformMat *m = NULL);
  list_t *search (int type, TransformMat *m = NULL); // this is for
						     // base layers

  /**
   *  Calculate the placement boundary for the LayoutBlob; this
   *  includes padding on the edges, as well as alignment
   *  constraints: the x-dimension is aligned to the m2 pitch, and the
   *  y-dimension is aligned to the m1 pitch.
   *
   *  If there are 3 or fewer metal layers, space for 2 wires on all
   *  sides of the layout are added.
   *
   *  @param bllx, blly, burx, bury are used to return the boundary
   *  coordinates.
   */
  void calcBoundary (long *bllx, long *blly, long *burx, long *bury);

  /**
   *  Calculate the edge alignment between two edge atttributes
   *  @return 0 if there is no possible alignment,
   *          1 if any alignment is fine,
   *	      2 if the alignment is specified by the range d1 to d2
  */
  int GetAlignment (LayoutEdgeAttrib *a1, LayoutEdgeAttrib *a2,
		    int *d1, int *d2);
  
};


#endif /* __ACT_GEOM_H__ */
