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
  unsigned int attr:7;		/* up to 7 bits of "attributes" */

  void *net;			// the net associated with this tile,
				// if it is not a space tile
				   
 public:
  Tile ();
  ~Tile ();

  friend class Layer;
};
  
class Layer {
public:
  static Layer *base;
protected:
  Material *mat;		/* technology-specific
				   information. routing material for
				   the layer. */
  Tile *hint, *origin;		/* tile containing 0,0 / last lookup
				   */

  Tile *vhint, *vorigin;	// tile layer containing vias to the
				// next (upper) layer
  
  Layer *up, *down;		/* layer above and below */
  
  Material **other;	       // for the base layer, wells/diff/fets
  int nother;
 public:
  Layer (Material *);
  ~Layer ();

  void allocOther (int sz);
  void setOther (int idx, Material *m);
  void setDownLink (Layer *x);
};

class Geometry {
public:
  static void Init();

};

#endif /* __ACT_GEOM_H__ */
