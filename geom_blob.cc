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
#include <stdio.h>
#include <string.h>
#include <common/list.h>
#include <act/act.h>
#include <act/passes.h>
#include <act/passes/netlist.h>
#include <act/tech.h>
#include <common/qops.h>
#include "geom.h"
#include "subcell.h"

#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif


LayoutBlob::LayoutBlob (ExternMacro *m)
{
    long llx, lly, urx, ury;
    t = BLOB_MACRO;
    macro = m;
    if(macro && macro->isValid()) {
        macro->getBBox (&llx, &lly, &urx, &ury);
        _bbox.setRectCoords (llx, lly, urx, ury);
        _bloatbbox = _bbox;
        /* Should we have some way to get abutment and alignment with
           external macros? Probably not...
        */
    }
    else {
        _bbox.clear ();
        _bloatbbox.clear ();
    }
    _abutbox.clear ();
    _le = new LayoutEdgeAttrib();
}

LayoutBlob::LayoutBlob (blob_type type, Layout *lptr)
{
    t = type;
    readRect = false;

    count = 0;

    switch(t) {
    case BLOB_MACRO:
        macro = NULL;
        warning ("LayoutBlob: create macro using a different constructor!");
        break;

    case BLOB_CELL:
        warning ("LayoutBlob:: constructor called with BLOB_CELL; converting to BLOB_BASE!");
        t = BLOB_BASE;
        type = BLOB_BASE;
    case BLOB_BASE:
        base.l = lptr;
        if(lptr) {
            long llx, lly, urx, ury;
            lptr->getBBox (&llx, &lly, &urx, &ury);
            _bbox.setRectCoords (llx, lly, urx, ury);
            lptr->getBloatBBox (&llx, &lly, &urx, &ury);
            _bloatbbox.setRectCoords (llx, lly, urx, ury);
            _abutbox = lptr->getAbutBox ();
            _le = lptr->getEdgeAttrib ()->Clone ();
#if 0
            printf (" got: ");
            LayoutEdgeAttrib::print (stdout, _le.bot());
            printf ("\n");
#endif
        }
        else {
            _bbox.clear ();
            _bloatbbox.clear ();
            _abutbox.clear ();
            _le = new LayoutEdgeAttrib();
        }
        break;

    case BLOB_LIST:
        l.hd = NULL;
        l.tl = NULL;
        if(lptr) {
            blob_list *bl;
            NEW (bl, blob_list);
            bl->b = new LayoutBlob (BLOB_BASE, lptr);
            bl->next = NULL;
            bl->T.mkI();
            q_ins (l.hd, l.tl, bl);
            _bbox = bl->b->_bbox;
            _bloatbbox = bl->b->_bloatbbox;
            _abutbox = bl->b->_abutbox;
            _le = bl->b->_le->Clone();
        }
        else {
            _bbox.clear ();
            _bloatbbox.clear ();
            _abutbox.clear ();
            _le = new LayoutEdgeAttrib();
        }
        break;
    }
}


LayoutBlob::LayoutBlob (SubcellInst *cell)
{
    t = BLOB_CELL;

    readRect = false;
    count = 0;

    Assert (cell, "What?");

    subcell = cell;
    _bbox = cell->getBBox ();
    _bloatbbox = cell->getBloatBBox ();
    _abutbox = cell->getAbutBox ();
    _le = cell->getLayoutEdgeAttrib ()->Clone();
}

// LayoutBlob::LayoutBlob(const LayoutBlob& other) : 
//   t(other.t),
//   _bbox(other._bbox),
//   _bloatbbox(other._bloatbbox),
//   _abutbox(other._abutbox),
//   _le(other._le ? new LayoutEdgeAttrib(*other._le) : nullptr),
//   count(other.count),
//   readRect(other.readRect)
//   {
//     // Handle the union based on the blob type
//   switch (t) {
//     case BLOB_BASE:
//       // no deep copy
//       base.l = other.base.l
//       break;

//     case BLOB_LIST:
//       // no deep copy, copy list, not content
//       // it might be nessesary to copy the leafs but i dont htink so
//       if (other.l.hd) {
//         // Create a deep copy of the linked list
//         blob_list *copyfrom = nullptr;
//         blob_list *copyto = nullptr;

//         // Iterate through the original list
//         l.hd = new blob_list;
//         l.hd->T = other.l.hd->T;
//         l.hd->b = other.l.hd->b;
//         copyto = l.hd;
//         copyfrom = other.hd.next;
//         while (copyfrom) {
//           copyto->next = new blob_list;
//           copyto = copyto->next;
//           copyto->T = copyfrom->T;
//           copyto->b = copyfrom->b;
//           copyfrom = copyfrom->next;
//         }
//         copyto->next = NULL;
//         l.tl = copyto;
//       }
//       break;

//     case BLOB_CELL:
//       // no deep copy
//       subcell = other.subcell;
//       break;

//     case BLOB_MACRO:
//       // no deep copy
//       macro = other.macro;
//       break;

//     default:
//       fatal_error("tried to copy undefined LayoutBlob type");
//       break;
//   }
//   }


void LayoutBlob::setBBox (long _llx, long _lly, long _urx, long _ury)
{
    if(t == BLOB_MACRO) {
        return;
    }
    if(t != BLOB_BASE || base.l) {
        warning ("LayoutBlob::setBBox(): called on non-empty layout; ignored");
        return;
    }

    Rectangle r;
    r.setRectCoords (_llx, _lly, _urx, _ury);

    if(!_bbox.empty() && !r.contains (_bbox)) {
        fatal_error ("LayoutBlob::setBBox(): shrinking the bounding box is not permitted");
    }
    _bbox = r;
    _bloatbbox = r;
    _abutbox.clear ();
}

// LayoutBlob *LayoutBlob::cloneAndAppendBlob(LayoutBlob *b, blob_compose c, long gap){
//   if (t == BLOB_BASE) {
//     fatal_error ("LayoutBlob::appendBlob() called on BASE; error ignored!");
//     return NULL;
//   }
//   if (t == BLOB_MACRO) {
//     fatal_error ("LayoutBlob::appendBlob() called on MACRO; error ignored!");
//     return NULL;
//   }
//   if (t == BLOB_CELL) {
//     fatal_error ("LayoutBlob::appendBlob() called on CELL; error ignored!");
//     return NULL;
//   }
//   Assert (t == BLOB_LIST, "What?");
//   LayoutBlob *ret = new LayoutBlob()
//   t(other.t),
//   _bbox(other._bbox),
//   _bloatbbox(other._bloatbbox),
//   _abutbox(other._abutbox),
//   _le(other._le ? new LayoutEdgeAttrib(*other._le) : nullptr),
//   count(other.count),
//   readRect(other.readRect)
// }

void LayoutBlob::appendBlob (LayoutBlob *b, blob_compose c, long gap, bool flip)
{
    if(t == BLOB_BASE) {
        warning ("LayoutBlob::appendBlob() called on BASE; error ignored!");
        return;
    }
    if(t == BLOB_MACRO) {
        warning ("LayoutBlob::appendBlob() called on MACRO; error ignored!");
        return;
    }
    if(t == BLOB_CELL) {
        warning ("LayoutBlob::appendBlob() called on CELL; error ignored!");
        return;
    }

    Assert (t == BLOB_LIST, "What?");

    blob_list *bl, *prev;
    NEW (bl, blob_list);
    bl->next = NULL;
    bl->b = b;
    bl->T.mkI();

    if(l.hd) {
        prev = l.tl;
    }
    else {
        prev = NULL;
    }

    q_ins (l.hd, l.tl, bl);

    if (l.hd == l.tl) {
      // the first blob
        _bbox = b->getBBox ();
        _bloatbbox = b->getBloatBBox ();
        _abutbox = b->getAbutBox ();
        if (c == BLOB_HORIZ) {
	  if (flip) {
	    bl->T.mirrorLR();
	  }
	  bl->T.translate (gap, 0);
        }
        else if (c == BLOB_VERT) {
	  if (flip) {
	    bl->T.mirrorTB();
	  }
	  bl->T.translate (0, gap);
        }
        _le = bl->b->getLayoutEdgeAttrib()->Clone (&(bl->T));
        _bbox = bl->T.applyBox (_bbox);
        _bloatbbox = bl->T.applyBox (_bloatbbox);
        _abutbox = bl->T.applyBox (_abutbox);
#if 0
        printf("abut: new:%d old:%d\n", bl->b->getAbutBox().empty(), _abutbox.empty());
#endif
    }
    else {
      int do_merge_attrib = 0;
      Rectangle old_box;
      long merge_x, merge_y;

      // we are actually appending to the blob
      Assert (prev, "What?");

      if (c == BLOB_HORIZ) {
	long damt;
	bool valid;
	/* align! */

	if (flip) {
	  //tmpEdgeAttr->swaplr();
	  bl->T.mirrorLR();
	}
	LayoutEdgeAttrib *tmpEdgeAttr =
	  bl->b->getLayoutEdgeAttrib()->Clone (&(bl->T));

	valid = LayoutEdgeAttrib::align (_le->right(), tmpEdgeAttr->left(), &damt);
	if (!valid) {
	  warning ("appendBlob: no valid alignment, but continuing anyway");
	  damt = 0;
	}
	delete tmpEdgeAttr;

	Rectangle bx_bbox, bx_abutbox, bx_bloatbbox;
	bx_bbox = bl->T.applyBox (b->getBBox());
	bx_bloatbbox = bl->T.applyBox (b->getBloatBBox());
	bx_abutbox = bl->T.applyBox (b->getAbutBox());

#if 0
	printf ("[%d] orig box: ", flip); b->getBBox().print (stdout);
	printf ("\n       transformed: "); bx_bbox.print (stdout);
	printf ("\n");
	printf ("       abut box: "); bx_abutbox.print (stdout);
	printf ("\n");

	printf ("    cur blob bbox: "); _bbox.print (stdout);
	printf ("\n");
	printf ("    cur abut bbox: "); _abutbox.print (stdout);
	printf ("\n");
	
#endif

	int shiftamt = gap;

	if(valid /* acceptable alignment exists */
	   && !bl->b->getAbutBox().empty() /* there is an abutment box */
	   && !_abutbox.empty() /* there is an abutment box */) {

	  shiftamt += _abutbox.urx() - bx_abutbox.llx() + 1;
	}
	else {
	  shiftamt += (_bloatbbox.urx() - _bbox.llx() + 1)
	    /* width, including bloat */
	    + (b->getBBox().llx() - b->getBloatBBox().llx()); /* left bloat */
	}

	//TransformMat t;
	//// flip?
	//t.translate (shiftamt, damt);
	bl->T.translate (shiftamt, damt);

	// now we have shifted the blob; so
	Rectangle r;

	r = bl->T.applyBox (b->getBBox());

	_bbox = _bbox ^ r;

	r = bl->T.applyBox (b->getBloatBBox());
	_bloatbbox = _bloatbbox ^ r;

	r = b->getAbutBox();
	if(!r.empty() && !_abutbox.empty()) {
	  old_box = _abutbox;
	  do_merge_attrib = 1;

	  merge_x = shiftamt;
	  merge_y = damt;

	  r = bl->T.applyBox (b->getAbutBox());
	  _abutbox = _abutbox ^ r;

	}
	else {
	  _abutbox.clear();
	  _le = new LayoutEdgeAttrib();
	}
      }
      else if(c == BLOB_VERT) {
	long damt;
	bool valid;
	/* align! */
	if(flip){
	  bl->T.mirrorTB();
	  // tmpEdgeAttr->swaptb();
	}
	LayoutEdgeAttrib *tmpEdgeAttr = bl->b->getLayoutEdgeAttrib()->Clone(&(bl->T));
	valid = LayoutEdgeAttrib::align (_le->top(), tmpEdgeAttr->bot(), &damt);

	if(!valid) {
	  warning ("appendBlob: no valid alignment, but continuing anyway");
	  damt = 0;
	}
	delete tmpEdgeAttr;

	Rectangle bx_bbox, bx_abutbox, bx_bloatbbox;
	bx_bbox = bl->T.applyBox (b->getBBox());
	bx_bloatbbox = bl->T.applyBox (b->getBloatBBox());
	bx_abutbox = bl->T.applyBox (b->getAbutBox());

	int shiftamt = gap;

	if(valid /* acceptable alignment exists */
	   && !bl->b->getAbutBox().empty() /* there is an abutment box */
	   && !_abutbox.empty() /* there is an abutment box */) {
	  
	  shiftamt += (_abutbox.ury() - bx_abutbox.lly() + 1);
#if 0
	  printf("will use alignment - vertical\n");
#endif
	}
	else {
#if 0
	  printf("will use bbox - vertical - abut: new:%d old:%d\n", bl->b->getAbutBox().empty(), _abutbox.empty());
#endif
	  shiftamt += (_bloatbbox.ury() - _bbox.lly() + 1)
	    /* width, including bloat */
	    + (b->getBBox().lly() - b->getBloatBBox().lly() + 1); /* bot bloat */
	}

	// TransformMat t;
	// t.translate (damt, shiftamt);
	bl->T.translate (damt, shiftamt);

	Rectangle r;

	r = bl->T.applyBox (b->getBBox());

	_bbox = _bbox ^ r;

	r = bl->T.applyBox (b->getBloatBBox ());
	_bloatbbox = _bloatbbox ^ r;

	r = b->getAbutBox();
	if(!r.empty() && !_abutbox.empty()) {
	  old_box = _abutbox;
	  do_merge_attrib = 1;
	  r = bl->T.applyBox (b->getAbutBox());
	  _abutbox = _abutbox ^ r;
	  merge_x = damt;
	  merge_y = shiftamt;
	}
	else {
	  _abutbox.clear();
	  _le = new LayoutEdgeAttrib();
	}
      }
      else if(c == BLOB_MERGE) {
	Assert(!flip, "flip does not yet work with merge");
	_bbox = _bbox ^ b->getBBox();
	_bloatbbox = _bloatbbox ^ b->getBloatBBox();
	if(!b->getAbutBox().empty() && !_abutbox.empty()) {
	  old_box = _abutbox;
	  _abutbox = _abutbox ^ b->getAbutBox();
	  do_merge_attrib = 1;
	  merge_x = 0;
	  merge_y = 0;
	}
      }
      else {
	fatal_error ("What?");
      }

      /* now merge attributes if we used abutment */
      if(do_merge_attrib) {
	//get the already transformed edge attibutes
	LayoutEdgeAttrib *tmpEdgeAttr = bl->b->getLayoutEdgeAttrib()->Clone(&(bl->T));
	Rectangle r = bl->T.applyBox (b->getAbutBox());
	if(r.llx() == _abutbox.llx()) {
	  if(old_box.llx() == r.llx()) {
	    // merge!
	    //_le->mergeleft (tmpEdgeAttr->left(), merge_y);
	    _le->mergeleft (tmpEdgeAttr->left());

	  }
	  else {
	    // shift left aligh by damt
	    //_le->setleft (tmpEdgeAttr->left(), merge_y);
	    _le->setleft (tmpEdgeAttr->left());
	  }
	}
	if(r.urx() == _abutbox.urx()) {
	  if(old_box.urx() == r.urx()) {
	    // merge!
	    //_le->mergeright (tmpEdgeAttr->right(), merge_y);
	    _le->mergeright (tmpEdgeAttr->right());
	  }
	  else {
	    // shift left aligh by damt
	    //_le->setright (tmpEdgeAttr->right(), merge_y);
	    _le->setright (tmpEdgeAttr->right());
	  }
	}
	if(r.lly() == _abutbox.lly()) {
	  if(old_box.lly() == r.lly()) {
	    // merge!
	    //_le->mergebot (tmpEdgeAttr->bot(), merge_x);
	    _le->mergebot (tmpEdgeAttr->bot());
	  }
	  else {
	    // shift left aligh by damt
	    //_le->setbot (tmpEdgeAttr->bot(), merge_x);
	    _le->setbot (tmpEdgeAttr->bot());
	  }
	}
	if(r.ury() == _abutbox.ury()) {
	  if(old_box.ury() == r.ury()) {
	    // merge!
	    //_le->mergetop (tmpEdgeAttr->top(), merge_x);
	    _le->mergetop (tmpEdgeAttr->top());
	  }
	  else {
	    // shift left aligh by damt
	    //_le->settop (tmpEdgeAttr->top(), merge_x);
	    _le->settop (tmpEdgeAttr->top());
	  }
	}
	delete tmpEdgeAttr;
      }
    }
#if 0
    printf ("blob: (%ld,%ld) -> (%ld,%ld); bloat: (%ld,%ld)->(%ld,%ld)\n",
        llx, lly, urx, ury, bloatllx, bloatlly, bloaturx, bloatury);
#endif
}


void LayoutBlob::_printRect (FILE *fp, TransformMat *mat, bool istopcell)
{
    switch(t) {
    case BLOB_BASE:
        if(base.l) {
            base.l->PrintRect (fp, mat, istopcell);
        }
        break;

    case BLOB_LIST:
        for(blob_list *bl = l.hd; bl; q_step (bl)) {
            TransformMat m;
            if(mat) {
                m = *mat;
            }
            m.applyMat (bl->T);
            bl->b->_printRect (fp, &m, false);
        }
        break;

    case BLOB_MACRO:
      /* nothing to do, it is a macro */
        break;

    case BLOB_CELL:
    {
        TransformMat m;
        if(mat) {
            m = *mat;
        }
        subcell->PrintRect (fp, &m, istopcell);
    }
    break;

    }
}


void LayoutBlob::PrintRect (FILE *fp, TransformMat *mat, bool istopcell)
{
    long bllx, blly, burx, bury;
    long x, y;
    Rectangle bloatbox = getBloatBBox ();
    Rectangle abutbox = getAbutBox ();
    fprintf (fp, "bbox ");
    if(mat) {
        mat->apply (bloatbox.llx(), bloatbox.lly(), &x, &y);
        fprintf (fp, "%ld %ld", x, y);
        mat->apply (bloatbox.urx()+1, bloatbox.ury()+1, &x, &y);
        fprintf (fp, " %ld %ld\n", x, y);
        // mat->apply (abutbox.llx(), abutbox.lly(), &x, &y);
        // fprintf (fp, "rect # $align %ld %ld", x, y);
        // mat->apply (abutbox.urx()+1, abutbox.ury()+1, &x, &y);
        // fprintf (fp, " %ld %ld\n", x, y);
    }
    else {
        fprintf (fp, "%ld %ld %ld %ld\n",
            bloatbox.llx(), bloatbox.lly(),
            bloatbox.urx()+1, bloatbox.ury()+1);

        if(istopcell) {
            if(!_abutbox.empty()) {
                fprintf (fp, "rect # $align %ld %ld %ld %ld\n", _abutbox.llx(),
                    _abutbox.lly(), _abutbox.urx()+1, _abutbox.ury()+1);
            }
            LayoutEdgeAttrib::attrib_list *l;

            long llx, lly, urx, ury;

            if(_abutbox.empty()) {
	      llx = 0;
	      lly = 0;
	      urx = 1;
	      ury = 1;
            }
            else {
	      llx = _abutbox.llx();
	      lly = _abutbox.lly();
	      urx = _abutbox.urx() + 1;
	      ury = _abutbox.ury() + 1;
            }

            if(_le) {
	      for(l = _le->left(); l; l = l->next) {
		fprintf (fp, "rect $l:%s $align %ld %ld %ld %ld\n",
			 l->name, llx, l->offset, llx, l->offset);
	      }
	      for(l = _le->right(); l; l = l->next) {
		fprintf (fp, "rect $r:%s $align %ld %ld %ld %ld\n",
			 l->name, urx, l->offset, urx, l->offset);
	      }
	      for(l = _le->top(); l; l = l->next) {
		fprintf (fp, "rect $t:%s $align %ld %ld %ld %ld\n",
			 l->name, l->offset, ury, l->offset, ury);
	      }
	      for(l = _le->bot(); l; l = l->next) {
		fprintf (fp, "rect $b:%s $align %ld %ld %ld %ld\n",
			 l->name, l->offset, lly, l->offset, lly);
	      }
            }
        }
    }
    if(_abutbox.empty()){
        _printRect (fp, mat, istopcell);
    }
    else {
        _printRect (fp, mat, false);
    }
}


list_t *LayoutBlob::search (void *net, TransformMat *m)
{
    TransformMat tmat;
    list_t *tiles;

    if(m) {
        tmat = *m;
    }
    if(t == BLOB_BASE) {
        if(base.l) {
            tiles = base.l->search (net);
            if(list_isempty (tiles)) {
                return tiles;
            }
            else {
                struct tile_listentry *tle;
                NEW (tle, struct tile_listentry);
                tle->m = tmat;
                tle->tiles = tiles;
                tiles = list_new ();
                list_append (tiles, tle);
                return tiles;
            }
        }
        else {
            tiles = list_new ();
            return tiles;
        }
    }
    else if(t == BLOB_LIST) {
        blob_list *bl;
        tiles = list_new ();

        for(bl = l.hd; bl; q_step (bl)) {
            if(m) {
                tmat = *m;
            }
            else {
                tmat.mkI();
            }
            tmat.applyMat (bl->T);
            list_t *tmp = bl->b->search (net, &tmat);
            list_concat (tiles, tmp);
            list_free (tmp);
        }
    }
    else if(t == BLOB_MACRO) {
      /* nothing, macro */
        tiles = list_new ();
    }
    else {
        tiles = NULL;
        fatal_error ("New blob?");
    }
    return tiles;
}

list_t *LayoutBlob::search (int type, TransformMat *m)
{
    TransformMat tmat;
    list_t *tiles;

    if(m) {
        tmat = *m;
    }
    if(t == BLOB_BASE) {
        if(base.l) {
            tiles = base.l->search (type);
            if(list_isempty (tiles)) {
                return tiles;
            }
            else {
                struct tile_listentry *tle;
                NEW (tle, struct tile_listentry);
                tle->m = tmat;
                tle->tiles = tiles;
                tiles = list_new ();
                list_append (tiles, tle);
                return tiles;
            }
        }
        else {
            tiles = list_new ();
            return tiles;
        }
    }
    else if(t == BLOB_LIST) {
        blob_list *bl;
        tiles = list_new ();

        for(bl = l.hd; bl; q_step (bl)) {
            if(m) {
                tmat = *m;
            }
            else {
                tmat.mkI();
            }
            tmat.applyMat (bl->T);
            list_t *tmp = bl->b->search (type, &tmat);
            list_concat (tiles, tmp);
            list_free (tmp);
        }
    }
    else if(t == BLOB_MACRO) {
        tiles = list_new ();
    }
    else {
        tiles = NULL;
        fatal_error ("New blob?");
    }
    return tiles;
}


LayoutBlob::~LayoutBlob ()
{
  /* XXX do something here! */
}


void LayoutBlob::searchBBox (list_t *slist, long *bllx, long *blly,
    long *burx, long *bury)
{
    long wllx, wlly, wurx, wury;
    int init = 0;

    listitem_t *tli;
    for(tli = list_first (slist); tli; tli = list_next (tli)) {
        struct tile_listentry *tle = (struct tile_listentry *)list_value (tli);

        /* a transform matrix + list of (layer,tile-list) pairs */
        listitem_t *xi;
        for(xi = list_first (tle->tiles); xi; xi = list_next (xi)) {
          //Layer *name = (Layer *) list_value (xi);
            xi = list_next (xi);
            Assert (xi, "What?");

            list_t *actual_tiles = (list_t *)list_value (xi);
            listitem_t *ti;

            for(ti = list_first (actual_tiles); ti; ti = list_next (ti)) {
                long tllx, tlly, turx, tury;
                Tile *tmp = (Tile *)list_value (ti);

                tle->m.apply (tmp->getllx(), tmp->getlly(), &tllx, &tlly);
                tle->m.apply (tmp->geturx(), tmp->getury(), &turx, &tury);

                if(tllx > turx) {
                    long x = tllx;
                    tllx = turx;
                    turx = x;
                }

                if(tlly > tury) {
                    long x = tlly;
                    tlly = tury;
                    tury = x;
                }

                if(!init) {
                    wllx = tllx;
                    wlly = tlly;
                    wurx = turx;
                    wury = tury;
                    init = 1;
                }
                else {
                    wllx = MIN(wllx, tllx);
                    wlly = MIN(wlly, tlly);
                    wurx = MAX(wurx, turx);
                    wury = MAX(wury, tury);
                }
            }
        }
    }
    if(!init) {
        *bllx = 0;
        *blly = 0;
        *burx = -1;
        *bury = -1;
    }
    else {
        wurx ++;
        wury ++;
        *bllx = wllx;
        *blly = wlly;
        *burx = wurx;
        *bury = wury;
    }
}

void LayoutBlob::searchFree (list_t *slist)
{
    listitem_t *tli;

    for(tli = list_first (slist); tli; tli = list_next (tli)) {
        struct tile_listentry *tle = (struct tile_listentry *)list_value (tli);

        /* a transform matrix + list of (layer,tile-list) pairs */
        listitem_t *xi;
        for(xi = list_first (tle->tiles); xi; xi = list_next (xi)) {
          //Layer *name = (Layer *) list_value (xi);
            xi = list_next (xi);
            Assert (xi, "What?");

            list_t *actual_tiles = (list_t *)list_value (xi);
            list_free (actual_tiles);
        }
        list_free (tle->tiles);
        FREE (tle);
    }
    list_free (slist);
}


LayoutBlob *LayoutBlob::delBBox (LayoutBlob *b)
{
    if(!b) return NULL;
    if(b->t == BLOB_MACRO) {
        return b;
    }
    if(b->t == BLOB_BASE) {
        if(b->base.l) {
            return b;
        }
        else {
            delete b;
            return NULL;
        }
    }
    else {
        blob_list *x, *prev;
        prev = NULL;
        x = b->l.hd;
        while(x) {
            x->b = LayoutBlob::delBBox (x->b);
            if(!x->b) {
                q_delete_item (b->l.hd, b->l.tl, prev, x);
                FREE (x);
                if(prev) {
                    x = prev->next;
                }
                else {
                    x = b->l.hd;
                }
            }
            else {
                prev = x;
                x = x->next;
            }
        }
        if(!b->l.hd) {
            delete b;
            return NULL;
        }
        else {
            return b;
        }
    }
}

list_t *LayoutBlob::searchAllMetal (TransformMat *m)
{
    TransformMat tmat;
    list_t *tiles;

    if(m) {
        tmat = *m;
    }
    if(t == BLOB_BASE) {
        if(base.l) {
            tiles = base.l->searchAllMetal();
            if(list_isempty (tiles)) {
                return tiles;
            }
            else {
                struct tile_listentry *tle;
                NEW (tle, struct tile_listentry);
                tle->m = tmat;
                tle->tiles = tiles;
                tiles = list_new ();
                list_append (tiles, tle);
                return tiles;
            }
        }
        else {
            tiles = list_new ();
            return tiles;
        }
    }
    else if(t == BLOB_LIST) {
        blob_list *bl;
        tiles = list_new ();

        for(bl = l.hd; bl; q_step (bl)) {
            if(m) {
                tmat = *m;
            }
            else {
                tmat.mkI();
            }
            tmat.applyMat (bl->T);
            list_t *tmp = bl->b->searchAllMetal (&tmat);
            list_concat (tiles, tmp);
            list_free (tmp);
        }
    }
    else {
        tiles = NULL;
        fatal_error ("New blob?");
    }
    return tiles;
}



Rectangle LayoutBlob::getAbutBox()
{
    switch(t) {
    case BLOB_BASE:
        if(base.l) {
            return base.l->getAbutBox();
        }
        else {
            return Rectangle();
        }
        break;
    case BLOB_CELL:
        return subcell->getAbutBox();
    case BLOB_LIST:
        return _abutbox;
    default:
#if 0
        warning("return empty abut box, type not implemented");
#endif
        return Rectangle();
    }
}
