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

#include <tech.h>

#define MIN_VALUE (signed long)(1UL << (8*sizeof(long)-1))
#define MAX_VALUE (signed long)((1UL << (8*sizeof(long)-1))-1)


/* 
   off field in the macro below 
*/
#define FET_OFFSET 0
#define DIFF_OFFSET 1

#define NUM_MINOR_OFFSET 2 // the # of offset values

#define OFFSET_ONE_FLAVOR(type,off) ((type) + NUM_MINOR_OFFSET*(off))

/* 
   flavor = transistor flavor 
   type = EDGE_NFET or EDGE_PFET
   off = offsets defined above: FET/DIFF

Order:

     n/p + 2*(fet/diff) + 4*(flavor)

  [ nfet pfet ndiff pdiff ] flavor0
  [ nfet pfet ndiff pdiff ] flavor1
  ...
*/
#define TOTAL_OFFSET(flavor,type,off) (2*NUM_MINOR_OFFSET*(flavor)+OFFSET_ONE_FLAVOR(type,off))


#define TILE_ATTR_ISROUTE(x) ((x) == 0)
#define TILE_ATTR_NONPOLY(x) ((x) - 1)
#define TILE_ATTR_ISFET(x)   ((TILE_ATTR_NONPOLY(x) % (2*NUM_MINOR_OFFSET)) < 2)
#define TILE_ATTR_ISDIFF(x)  !TILE_ATTR_ISFET(x)


/* FET, DIFF */



class Tile {
 private:
  int idx;
  
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

  void print (FILE *fp = NULL);
  void printall ();

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

  long geturx() { return nextx()-1; }
  long getury() { return nexty()-1; }
  long getllx() { return llx; }
  long getlly() { return lly; }
  
  friend class Layer;
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

 public:
  Layer (Material *);
  ~Layer ();

  void allocOther (int sz);
  void setOther (int idx, Material *m);
  void setDownLink (Layer *x);

  int Draw (long llx, long lly, unsigned long wx, unsigned long wy, void *net, int type = 0);
  int Draw (long llx, long lly, unsigned long wx, unsigned long wy, int type = 0);
  int drawVia (long llx, long lly, unsigned long wx, unsigned long wy, void *net, int type = 0);
  int drawVia (long llx, long lly, unsigned long wx, unsigned long wy, int type = 0);

  /* type = -1 : all! */
  void BBox (int *llx, int *lly, int *urx, int *ury, int type = -1);

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
  Layout ();
  ~Layout();

  int DrawPoly (long llx, long lly, unsigned long wx, unsigned long wy, void *net);
  int DrawDiff (int flavor, int type, long llx, long lly, unsigned long wx, unsigned long wy, void *net);
  int DrawFet (int flavor, int type, long llx, long lly, unsigned long wx, unsigned long wy, void *net);
  int DrawDiffBBox (int flavor, int type, long llx, long lly, unsigned long wx, unsigned long wy);

  /* 0 = metal1, etc. */
  int DrawMetal (int num, long llx, long lly, unsigned long wx, unsigned long wy, void *net);

  /* 0 = base to metal1, 1 = metal1 to metal2, etc. */
  int DrawVia (int num, long llx, long lly, unsigned long wx, unsigned long wy);

  Layer *getLayerPoly () { return base; }
  Layer *getLayerDiff () { return base; }
  Layer *getLayerWell () { return base; }
  Layer *getLayerFet ()  { return base; }
  

  PolyMat *getPoly ();
  FetMat *getFet (int type, int flavor = 0); // type == EDGE_NFET or EDGE_PFET
  DiffMat *getDiff (int type, int flavor = 0);
  WellMat *getWell (int type, int flavor = 0);
  // NOTE: WELL TYPE is NOT THE TYPE OF THE WELL MATERIAL, BUT THE TYPE OF
  // THE FET THAT NEEDS THE WELL.

  void getBBox (long *llx, long *lly, long *urx, long *ury);

private:
  Layer *base;
  Layer **metals;
  int nflavors;
  int nmetals;
};


class LayoutBlob;

struct blob_list {
  LayoutBlob *b;
  long gap;
  struct blob_list *next;
};

class LayoutBlob {
private:
  union {
    struct {
      blob_list *hd, *tl;
    } l;			// a blob list
    struct {
      Layout *l;		// ... layout block
      unsigned int mirror;	// ... mirroring
    } base;
  };
  int type;			// type field: 0 = base, 1 = horiz,
				// 2 = vert

public:
  LayoutBlob (int type);
  ~LayoutBlob ();

  void appendBlob (LayoutBlob *b, long gap);

  Layout *splat();

};


#endif /* __ACT_GEOM_H__ */
