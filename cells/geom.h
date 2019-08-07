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

#include "tech.h"

class Tile {
 private:
  struct {
    Tile *x, *y;
  } ll, ur;
  
  long llx, lly;
  unsigned long xsz, ysz;
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
				// if it is not a space tile
				   
 public:
  Tile ();
  ~Tile ();

  friend class Layer;
};


/* 
   type uses EDGE_PFET/NFET 
*/
#define FET_OFFSET 0
#define DIFF_OFFSET 1
#define NUM_MINOR_OFFSET 2

#define OFFSET_ONE_FLAVOR(type,off) ((type) + NUM_MINOR_OFFSET*(off))
#define TOTAL_OFFSET(flavor,type,off) (2*NUM_MINOR_OFFSET*(flavor)+OFFSET_ONE_FLAVOR(type,off))

class Layer {
protected:
  Material *mat;		/* technology-specific
				   information. routing material for
				   the layer. */
  Tile *hint, *origin;		/* tile containing 0,0 / last lookup
				   */

  Tile *vhint, *vorigin;	// tile layer containing vias to the
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

  int Draw (int llx, int lly, int urx, int ury, void *net, int type = 0);
  int Draw (int llx, int lly, int urx, int ury, int type = 0);

  void BBox (int *llx, int *lly, int *urx, int *ury, int type = 0);

  friend class Layout;
};

class Layout {
public:
  static void Init();
  
  /* 
     The base layer is special as this is where the transistors are
     drawn. It includes poly, fets, diffusion, and virtual diffusion.
  */
  Layer *base;
  Layout ();
  ~Layout();

  PolyMat *getPoly ();
  FetMat *getFet (int type, int flavor = 0); // type == EDGE_NFET or EDGE_PFET
  DiffMat *getDiff (int type, int flavor = 0);
  WellMat *getWell (int type, int flavor = 0);
  // NOTE: WELL TYPE is NOT THE TYPE OF THE WELL MATERIAL, BUT THE TYPE OF
  // THE FET THAT NEEDS THE WELL.
  
};



#endif /* __ACT_GEOM_H__ */
