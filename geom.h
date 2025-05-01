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

#include <act/act.h>
#include <act/tech.h>
#include <act/passes/netlist.h>
#include <common/path.h>
#include "tile.h"
#include "attrib.h"


/*
 * Geometry transformation matrix
 */
class TransformMat {
  long _dx, _dy;
  unsigned int _flipx:1;
  unsigned int _flipy:1;
  unsigned int _swap:1;
public:
  TransformMat ();

  void mkI();

  void translate (long dx, long dy);
  void mirrorLR ();
  void mirrorTB ();
  void mirror45 ();

  void apply (long inx, long iny, long *outx, long *outy) const;

  Rectangle applyBox (const Rectangle &r) const;

  void applyMat (const TransformMat &t);

  void Print (FILE *fp) const;
};


/*
 * One abstract layer
 */
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
  long _bllx, _blly, _burx, _bury; // bloated bbox
  /* BBox with spacing on all sides 
     This bloats the bounding box by ceil(minimum spacing/2) on all sides.
  */

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

  int isMetal ();		// 1 if it is a metal layer or a via
				// layer

  void markPins (void *net, int isinput); // mark pin tiles
  
  list_t *searchMat (void *net);
  list_t *searchMat (int attr);
  list_t *searchVia (void *net);
  list_t *searchVia (int attr);
  list_t *allNonSpaceMat ();
  list_t *allNonSpaceVia ();	// looks at "up" vias only

  void getBBox (long *llx, long *lly, long *urx, long *ury);
  void getBloatBBox (long *llx, long *lly, long *urx, long *ury);

  void PrintRect (FILE *fp, TransformMat *t = NULL);

  const char *getRouteName() {
    RoutingMat *rmat = dynamic_cast<RoutingMat *> (mat);
    if (rmat) {
      return rmat->getLEFName();
    }
    else {
      return mat->getName();
    }
  }
  const char *getViaName() { return ((RoutingMat *)mat)->getUpC()->getName(); } 

  Tile *find (long x, long y);

  friend class Layout;
};

class Layout {
public:
  static bool _initdone;
  static void Init();
  static double getLeakAdjust () { return _leak_adjust; }

  /*
     The base layer is special as this is where the transistors are
     drawn. It includes poly, fets, diffusion, and virtual diffusion.
  */
  Layout (netlist_t *);
  ~Layout();

  int DrawPoly (long llx, long lly, unsigned long wx, unsigned long wy, void *net);
  int DrawDiff (int flavor, int type, long llx, long lly, unsigned long wx, unsigned long wy, void *net);
  int DrawWellDiff (int flavor, int type, long llx, long lly, unsigned long wx, unsigned long wy, void *net);
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


  void markPins ();

  
  PolyMat *getPoly ();
  FetMat *getFet (int type, int flavor = 0); // type == EDGE_NFET or EDGE_PFET
  DiffMat *getDiff (int type, int flavor = 0);
  WellMat *getWell (int type, int flavor = 0);
  // NOTE: WELL TYPE is NOT THE TYPE OF THE WELL MATERIAL, BUT THE TYPE OF
  // THE FET THAT NEEDS THE WELL.

  void getBBox (long *llx, long *lly, long *urx, long *ury);
  void getBloatBBox (long *llx, long *lly, long *urx, long *ury);

  void PrintRect (FILE *fp, TransformMat *t = NULL);
  void ReadRect (const char *file, int raw_mode = 0);
  void ReadRect (Process *p, int raw_mode = 0);

  list_t *search (void *net);
  list_t *search (int attr);
  list_t *searchAllMetal ();

  void propagateAllNets();

  bool readRectangles() { return _readrect; }

  void flushBBox() { _rbox.clear(); }

  Rectangle &getAbutBox() { return _abutbox; }
  LayoutEdgeAttrib *getEdgeAttrib() { return _le; }

  double leak_adjust() {
    if (!N->leak_correct) { return 0.0; }
    else { return _leak_adjust; }
  }

  node_t *getVdd() { return N->Vdd; }
  node_t *getGND() { return N->GND; }

private:
  bool _readrect;

  Rectangle _rbox;		// this is from the .rect file, and
				// overrides any ocmputed box and
				// bounding box. If set, it is used as
				// both the box and bbox for the
				// layout

  Rectangle _abutbox;		// abutment information
  LayoutEdgeAttrib *_le;	// alignment information

  Layer *base;
  Layer **metals;
  int nflavors;
  int nmetals;
  netlist_t *N;
  struct Hashtable *lmap;	// map from layer string to base layer
				// name

  path_info_t *_rect_inpath;	// input path for rectangles, if any

  static double _leak_adjust;
};

class LayoutBlob;
class SubcellInst;

enum blob_type { BLOB_BASE,  /* some layout */
		 BLOB_CELL,  /* subcell */
		 BLOB_MACRO, /* macro */
		 BLOB_LIST   /* list of blobs */
};

enum blob_compose {
  BLOB_HORIZ,			// horizontal compositioo
  BLOB_VERT,			// vertical composition
  BLOB_MERGE			// merge paint
};

struct blob_list {
  LayoutBlob *b;

  TransformMat T;		// transformation matrix to bring this
				// blob into the coordinate system of
				// the first blob and the bounding
				// box/etc.

  struct blob_list *next;
};

struct tile_listentry {
  TransformMat m;  /**< the coordinate transformation matrix */
  list_t *tiles;   /**< a list alternating between Layer pointer and a
		      list of tiles  */
};


class LayoutBlob {
private:
  union {
    struct {
      blob_list *hd, *tl;
    } l;			// a blob list
    struct {
      Layout *l;		// ... layout block
                                // ... if this is a NULL pointer and
                                // it is a BLOB_BASE type, this is a
                                // special layout blob that is a pure
                                // bounding box.
    } base;
    SubcellInst *subcell;	// subcell
    ExternMacro *macro;		// external macro
  };
  blob_type t;			// type field: 0 = base, 1 = horiz,
				// 2 = vert, etc.

  Rectangle _bbox;		// the bounding box of all paint
  Rectangle _bloatbbox;		// the bloated bounding box of all paint
  Rectangle _abutbox;		// the abut bounding box of all paint

  LayoutEdgeAttrib *_le;

  unsigned long count;		// for statistics tracking

  bool readRect;

  void _printRect (FILE *fp, TransformMat *t);
  
public:
  LayoutBlob (blob_type type, Layout *l = NULL);
  LayoutBlob (SubcellInst *cell);
  LayoutBlob (ExternMacro *m);
  // LayoutBlob (const LayoutBlob& other);
  ~LayoutBlob ();
  
  bool isSubcell() { return t == BLOB_CELL ? true : false; }
  bool isList() { return t == BLOB_LIST ? true : false; }

  /* macros */
  bool isMacro() { return t == BLOB_MACRO ? true : false; }
  const char *getMacroName() { return macro->getName(); }
  const char *getLEFFile() { return macro->getLEFFile(); }

  void appendBlob (LayoutBlob *b, blob_compose c, long gap = 0, bool flip = false);

  void markRead () { readRect = true; }
  bool getRead() { return readRect; }
  
  void PrintRect (FILE *fp, TransformMat *t = NULL);

  /**
   * Computes the actual bounding box of the layout blob
   *
   * @param llxp, llyp, urxp, uryp are used to return the boundary.
   */
  Rectangle getBBox () const { return _bbox; }
  Rectangle getBloatBBox () const { return _bloatbbox; }

  /**
   * Set bounding box: only applies to BLOB_BASE with no layout 
   */
  void setBBox (long _llx, long _lly, long _urx, long _ury);

  /**
   * Remove any bounding box blobs. Returns updated blob.
   * If it is called on base bbox blob, then it returns NULL after
   * deleting it.
   */
  static LayoutBlob *delBBox (LayoutBlob *b);

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
  list_t *searchAllMetal (TransformMat *m = NULL);

  /* 
   * Uses the return value from the search function and returns its
   * bounding box
   */
  static void searchBBox (list_t *slist, long *bllx, long *blly, long *burx,
			  long *bury);
  static void searchFree (list_t *tiles);

  /**
   * Get abutment box
   */
  Rectangle getAbutBox ();

  /**
   * Get edge attributes!
   */
  LayoutEdgeAttrib *getLayoutEdgeAttrib() { return _le; }

  /**
   * Stats 
   */
  void incCount () { count++; }
  unsigned long getCount () { return count; }

  /**
   * Alignment markers
   */
  LayoutEdgeAttrib::attrib_list *getLeftAlign() {
    return _le->left();
  }
  
  LayoutEdgeAttrib::attrib_list *getRightAlign() {
    return _le->right();
  }
  
  LayoutEdgeAttrib::attrib_list *getTopAlign() {
    return _le->top();
  }
  
  LayoutEdgeAttrib::attrib_list *getBotAlign() {
    return _le->bot();
  }

  /**
   * Print alignment markers
   */
  void printAlign (FILE *fp) {
    fprintf (fp, "l: ");
    LayoutEdgeAttrib::print (fp, _le->left());
    fprintf (fp, "; r: ");
    LayoutEdgeAttrib::print (fp, _le->right());
    fprintf (fp, "; t: ");
    LayoutEdgeAttrib::print (fp, _le->top());
    fprintf (fp, "; b: ");
    LayoutEdgeAttrib::print (fp, _le->bot());
  }

  friend class SubcellInst;
};

#endif /* __ACT_GEOM_H__ */
