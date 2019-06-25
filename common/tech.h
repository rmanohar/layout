/*************************************************************************
 *
 *  This file is part of the ACT library
 *
 *  Copyright (c) 2018-2019 Rajit Manohar
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
#ifndef __TECHFILE_H__
#define __TECHFILE_H__

#include <stdio.h>

class Layer;

class RangeTable {
 public:
  RangeTable (int _s, int *_tab) {
    sz = _s;
    table = _tab;
    minval = -1;
  }
  int min() {
    if (minval >= 0) return minval;
    for (int i=1; i < sz; i += 2) {
      if (minval == -1) {
	minval = table[i];
      }
      else if (table[i] < minval) {
	minval = table[i];
      }
    }
    return minval;
  }
  int operator[](int idx);
 protected:
  int sz;
  int *table;
  int minval;
};

class Contact;

class Material {
 public:
  Material() {
    name = NULL;
    gds[0] = -1;
    gds[1] = -1;
    width = NULL;
    spacing = NULL;
    minarea = 0;
    maxarea = 0;
    xgrid = 0;
    ygrid = 0;
    viaup = NULL;
    viadn = NULL;
    l = NULL;
  }

  const char *getName() { return name; }
  
protected:
  const char *name;		/* drawing name in magic */
  int gds[2];			/* gds ids */

  RangeTable *width;		/* min width */
  RangeTable *spacing;		/* min spacing */

  int minarea;			/* 0 means no constraint */
  int maxarea;			/* 0 means no constraint */
  int xgrid, ygrid;		/* 0,0 if not on a grid */

  Contact *viaup, *viadn;

  Layer *l;

  friend class Technology;
};

struct RoutingRules {
  int endofline;		/* end of line extension */
  int endofline_width;		/* width for extension */
  int minturn;

  unsigned int routex:1;	/* can be used for x routing */
  unsigned int routey:1;	/* can be used for y routing */
};

class RoutingMat : public Material {
public:
  RoutingMat (char *s) { name = s; }
 protected:
  RoutingRules r;

  friend class Technology;
};


class PolyMat : public RoutingMat {
 public:
  PolyMat (char *s) : RoutingMat(s) { }
  int getOverhang (int w) { return (*overhang)[w]; }
  int getNotchOverhang (int w) { return (*notch_overhang)[w]; }
 protected:
  int width;

  RangeTable *overhang;		/* poly overhang beyond diffusion */
  RangeTable *notch_overhang;	/* overhang for a notch */
  int *via_n;		      /* spacing of poly via to n-type diff */
  int *via_p;		      /* spacing of poly via to p-type diff */

  friend class Technology;
};

class FetMat : public Material {
 public:
  FetMat (char *s) { name = s; }
  int getSpacing (int w) {
    return (*spacing)[w];
  }
protected:
  int width;
  int num_dummy;		/* # of dummy poly needed */

  friend class Technology;
};


class WellMat : public Material {
 public:
  WellMat (char *s) { name = s; }
  int getOverhang () { return overhang; }
protected:
  int width;
  int *spacing;	      /* to other wells of the same type */
  int *oppspacing;    /* to other wells of a different type */
  int overhang;	      /* overhang from diffusion */

  friend class Technology;
};


class DiffMat : public Material {
 public:
  DiffMat (char *s) { name = s; }

  /* return diffusion overhang, given width of fet and whether or not
     the edge of the diffusion has a contact or not */
  int effOverhang(int w, int hasvia = 0);
  int viaSpaceEdge ();
  int viaSpaceMid ();
  int getPolySpacing () { return polyspacing; }
  int getNotchSpacing () { return notchspacing; }
  int getOppDiffSpacing (int flavor) { return oppspacing[flavor]; }
protected:
  int width;
  int *spacing;
  int *oppspacing;
  int polyspacing;
  int notchspacing;
  RangeTable *overhang;
  int via_edge;
  int via_fet;

  friend class Technology;
};

class Contact : public Material {
 public:
  Contact (char *s) {
    name = s;
  }
  int width() { return width_int; }
protected:
  int width_int, spacing;
  Material *lower, *upper;
  int sym_surround;
  int asym_surround;
  int asym_opp;

  friend class Technology;
};


class Technology {
 public:
  static Technology *T;
  static void Init (const char *techfilename);
  
  const char *name;		/* name and date */
  const char *date;		/* string */

  double scale;			/* scale factor to get nanometers from
				   the integer units */

  int nmetals;			/* # of metal layers */

  unsigned int stk_contacts:1;	/* stacked contacts? */

  /* indexed by EDGE_PFET, EDGE_NFET */
  int num_devs;			/* # of device types */
  DiffMat **diff[2];		/* diffusion for p/n devices */
  WellMat **well[2];		/* device wells */
  FetMat **fet[2];		/* transistors for each type */

  PolyMat *poly;

  RoutingMat **metal;

  Contact *pc, **mc;		/* poly, metal contacts */
  Contact **wc[2];		/* well */
  Contact **dc[2];		/* diffusion */
};
  
  


#endif /* __TECHFILE_H__ */
