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
#include <stdio.h>
#include <common/list.h>
#include <common/misc.h>
#include "tile.h"
#include "geom.h"

//static int tcnt = 0;

Tile::Tile ()
{
  //idx = tcnt++;
  
  /*-- default tile is a space tile that is infinitely large --*/
  ll.x = NULL;
  ll.y = NULL;
  ur.x = NULL;
  ur.y = NULL;
  llx = MIN_VALUE;
  lly = MIN_VALUE;
  
  //up = NULL;
  //down = NULL;
  space = 1;
  virt = 0;
  attr = 0;
  net = NULL;
}

Tile::~Tile()
{
  ll.x = NULL;
  ll.y = NULL;
  ur.x = NULL;
  ur.y = NULL;
  llx = MIN_VALUE;
  lly = MIN_VALUE;
  
  //up = NULL;
  //down = NULL;
  space = 1;
  virt = 0;
  attr = 0;
  net = NULL;
}
  

#define SCALE 8
#define WINDOW 100
#define OFFSET 10

#if 0
void Tile::print (FILE *fp)
{


  if (fp) {
    int mllx, mlly, murx, mury;

    mllx =  (WINDOW)/2 +
      (getllx() == MIN_VALUE ? -(WINDOW)/2 : getllx());
    mlly =  (WINDOW)/2 + 
      (getlly() == MIN_VALUE ? -(WINDOW)/2 : getlly());

    murx =  (WINDOW)/2 +
      (geturx() == MAX_VALUE-1 ? (WINDOW)/2 : geturx());
    mury =  (WINDOW)/2 +
      (getury() == MAX_VALUE-1 ? (WINDOW)/2 : getury());

    fprintf (fp, "<rect x=%d y=%d width=%d height=%d stroke=green stroke-width=1 fill=white />\n",
	     OFFSET/2 + SCALE*mllx, OFFSET/2 + SCALE*(WINDOW - mury), SCALE*(murx - mllx), SCALE*(mury - mlly));

    fprintf (fp, "<text x=%d y=%d font-size=50%% text-anchor=middle> %d </text>\n",
	     OFFSET/2 + SCALE*(mllx + murx)/2, OFFSET/2 + SCALE*(WINDOW-(mlly+mury)/2), idx);

  }
  
  printf ("[#%d|a=%d,sp=%d] (", this->idx, this->attr, this->space);
  if (getllx() == MIN_VALUE) {
    printf ("-inf");
  }
  else {
    printf ("%ld", getllx());
  }
  printf (",");
  if (getlly() == MIN_VALUE) {
    printf ("-inf");
  }
  else {
    printf ("%ld", getlly());
  }
  printf (") -> (");
  if (geturx() == MAX_VALUE-1) {
    printf ("+inf");
  }
  else {
    printf ("%ld", geturx());
  }
  printf (",");
  if (getury() == MAX_VALUE-1) {
    printf ("+inf");
  }
  else {
    printf ("%ld", getury());
  }
  printf (") : [llx=%d, lly=%d; urx=%d, ury=%d]\n",
	  ll.x ? ll.x->idx : -1,
	  ll.y ? ll.y->idx : -1,
	  ur.x ? ur.x->idx : -1,
	  ur.y ? ur.y->idx : -1);
}
#endif

Tile *Tile::find (long x, long y)
{
#if 0
  printf ("find: (%ld, %ld)\n", x, y);
#endif
  
  Tile *t = this;
  do {
    if (x < t->llx) {
      while (x < t->llx) {
	t = t->ll.x;
      }
      Assert (t->xmatch (x), "Invariant failed");
    }
    else if (!t->xmatch (x)) {
      while (x > t->geturx()) {
	t = t->ur.x;
      }
      Assert (t->xmatch (x), "Invariant failed");
    }

    if (y < t->lly) {
      while (y < t->lly) {
	t = t->ll.y;
      }
      Assert (t->ymatch (y), "Invariant failed");
    }
    else if (!t->ymatch (y)) {
      while (y > t->getury()) {
	t = t->ur.y;
      }
      Assert (t->ymatch (y), "Invariant failed");
    }
  } while (!t->xmatch (x));
  return t;
}


//static int debug_apply = 0;

/*
  Returns a list of all the tiles that overlap with the specified region
*/
void Tile::applyTiles (long _llx, long _lly, unsigned long wx, unsigned long wy,
		       void *cookie, void (*f) (void *, Tile *))

{
  Tile *t;
  list_t *frontier;
  long _urx, _ury;

  _urx = _llx + (signed long)wx - 1;
  _ury = _lly + (signed long)wy - 1;

#if 0
  if (debug_apply) {
    printf (" search: (%ld,%ld) -> (%ld,%ld)\n", _llx, _lly, _urx, _ury);
  }
#endif  

  t = find (_llx, _lly);

  frontier = list_new ();
  list_append (frontier, t);

  /* 1. create vertical wavefront */
  while (t->getury() < _ury) {
    t = t->find (_llx, t->getury() + 1);
    list_append (frontier, t);
  }
  
  while (!list_isempty (frontier)) {
    Tile *tmp;
    t = (Tile *) list_delete_tail (frontier);

#if 0
    if (debug_apply) {
      printf ("  :: "); t->print();
    }
#endif    
    /* traverse right edge downward. 
       if this tile might be added by someone else on the frontier,
       done.
       
    */
    tmp = t->ur.x;
    /* right edge downward traversal */
    while (tmp) {
      if (_llx <= tmp->llx && tmp->llx <= _urx &&
	  !(tmp->getury() < _lly || tmp->lly > _ury)) {
	/* another tile might add this one if:
	   1. it goes below t->lly
	   2. t->lly is not at the bottom limit
	*/
	if (tmp->getlly() < t->lly && t->lly > _lly)
	  break;
	
#if 0
	if (debug_apply) {
	  printf ("     -> add: "); tmp->print();
	}
#endif
	list_append_head (frontier, tmp);
      }
      else {
	if (!(_llx <= tmp->llx && tmp->llx <= _urx))
	  break;
      }

      if (tmp->getlly() > t->getlly()) {
	tmp = tmp->ll.y;
      }
      else {
	tmp = NULL;
      }
    }
    (*f) (cookie, t); 		/* apply function */
  }
  list_free (frontier);
}

static void append_tile (void *cookie, Tile *t)
{
  list_t *l = (list_t *) cookie;
  list_append (l, t);
}

/*
  Returns a list of all the tiles that overlap with the specified region
*/
list_t *Tile::collectRect (long _llx, long _lly,
			   unsigned long wx, unsigned long wy)
{
  list_t *l;

  l = list_new ();
  applyTiles (_llx, _lly, wx, wy, l, append_tile);
  
  return l;
}


#if 0
static int pall = 0;
void Tile::printall ()
{
  list_t *l;

#if 1
  char buf[100];
  sprintf (buf, "debug.%d.html", pall++);
  FILE *fp = fopen (buf, "w");
  fprintf (fp, "<html>\n<body>\n");
  fprintf (fp, "<svg width=%d height=%d>\n", WINDOW*SCALE + OFFSET, WINDOW*SCALE + OFFSET);
#endif  

  l  = list_new ();
  list_append (l, this);
  this->virt = 1;
  while (!list_isempty (l)) {
    Tile *tmp = (Tile *) stack_pop (l);
    if (!tmp) continue;
#if 1
    //tmp->print();
    tmp->print (fp);
#endif
    
    if (tmp->ll.x && !tmp->ll.x->virt) {
      tmp->ll.x->virt = 1;
      list_append (l, tmp->ll.x);
    }
    if (tmp->ll.y && !tmp->ll.y->virt) {
      tmp->ll.y->virt = 1;
      list_append (l, tmp->ll.y);
    }
    if (tmp->ur.x && !tmp->ur.x->virt) {
      tmp->ur.x->virt = 1;
      list_append (l, tmp->ur.x);
    }
    if (tmp->ur.y && !tmp->ur.y->virt) {
      tmp->ur.y->virt = 1;
      list_append (l, tmp->ur.y);
    }
  }
  this->virt = 0;
  list_append (l, this);
  while (!list_isempty (l)) {
    Tile *tmp = (Tile *) stack_pop (l);
    if (!tmp) continue;
    if (tmp->ll.x && tmp->ll.x->virt) {
      tmp->ll.x->virt = 0;
      list_append (l, tmp->ll.x);
    }
    if (tmp->ll.y && tmp->ll.y->virt) {
      tmp->ll.y->virt = 0;
      list_append (l, tmp->ll.y);
    }
    if (tmp->ur.x && tmp->ur.x->virt) {
      tmp->ur.x->virt = 0;
      list_append (l, tmp->ur.x);
    }
    if (tmp->ur.y && tmp->ur.y->virt) {
      tmp->ur.y->virt = 0;
      list_append (l, tmp->ur.y);
    }
  }
  list_free (l);

#if 1
  fprintf (fp, "</svg>\n");
  fprintf (fp, "</body>\n</html>\n");
  fclose (fp);
#endif  
}
#endif

Tile *Tile::addRect (long _llx, long _lly, unsigned long wx, unsigned long wy,
		     bool force)
{
#if 0
  printf ("addrect @ %d: (%ld, %ld) -> (%ld, %ld)\n", pall, _llx, _lly,
	  _llx + wx - 1, _lly + wy - 1);
  //printall();
  fflush (stdout);
#endif

  if (wx == 0 || wy == 0) {
    return NULL;
  }
  
  /*
    we first collect all the tiles within the region.
  */
  list_t *l = collectRect (_llx, _lly, wx, wy);

  list_t *ml;
  listitem_t *li;

  if (list_isempty (l)) {
    fatal_error ("Tile::collectRect() failed!");
  }

  Tile *t = (Tile *) list_value (list_first (l));
  void *tnet = NULL;

#if 0
  printf ("   Region has %d tiles\n", list_length (l));
  for (li = list_first (l); li; li = list_next (li)) {
    Tile *tmp = (Tile *) list_value (li);
    printf ("  -> "); tmp->print ();
  }
  fflush (stdout);
#endif  

  /* 
     check that all the tile types match
  */
  if (!force) {
    for (li = list_first (l); li; li = list_next (li)) {
      Tile *tmp = (Tile *) list_value (li);
      if (t->space != tmp->space || t->virt != tmp->virt || t->attr != tmp->attr) {
	warning ("Tile::addRect() failed; inconsistent tile types being merged");
	list_free (l);
	return NULL;
      }
      if (tmp->net && tnet && tnet != tmp->net) {
	warning ("Tile::addRect() failed; inconsistent nets being merged");
	list_free (l);
	return NULL;
      }
      if (!tnet && tmp->net) {
	tnet = tmp->net;
      }
    }
  }

  ml = list_new ();

  /* create new rectangle */
  Tile *rt = new Tile();
  rt->net = tnet;
  rt->space = t->space;
  rt->virt = t->virt;
  rt->attr = t->attr;
    
  rt->llx = _llx;
  rt->lly = _lly;
  rt->ur.x = NULL;
  rt->ur.y = NULL;
  rt->ll.x = NULL;
  rt->ll.y = NULL;

  /* 
     walk through all the tiles with overlap, and prune them so that
     we have created a region that is the specified rectangle 
  */
  
  while (!list_isempty (l)) {
    t = (Tile *) list_delete_tail (l);

#if 0
    printf ("   Tile: "); t->print();
#endif
    
    if (t->llx < _llx) {
      t = t->splitX (_llx);	/* left edge prune */ 
#if 0
      printf ("   splitX -> ");
      t->print ();
#endif      
   }
    if (t->lly < _lly) {
      t = t->splitY (_lly);	/* bottom edge prune */
#if 0
      printf ("   splitY -> ");
      t->print();
#endif      
    }

    if (t->nextx() > _llx + (signed long)wx) {
      Tile *tmp;
      tmp = t->splitX (_llx+(signed long)wx);	/* right edge prune */
#if 0
      printf ("   splitX => ");
      tmp->print ();
#endif      
    }
    if (t->nexty() > _lly + (signed long)wy) {
      Tile *tmp;
      tmp = t->splitY (_lly + (signed long)wy);	/* top edge prune */
#if 0
      printf ("   splitY => ");
      tmp->print();
#endif      
    }

#if 0
    printf ("   final chunk: "); t->print();
    fflush (stdout);
#endif    
    list_append (ml, t);
  }

#if 0
  printf ("--- %d intermediate: collected region + split rects ---\n", pall);
  printall ();
  printf ("---- end split\n");
  fflush (stdout);
#endif

  int flag = 0;

  while (!list_isempty (ml)) {
    /* repair stitches */
    Tile *tmp = (Tile *) list_delete_tail (ml);

    if (tmp->llx == _llx && tmp->lly == _lly) {
      /* ll corner tile; import stitches */
      rt->ll.x = tmp->ll.x;
      rt->ll.y = tmp->ll.y;
      flag |= 1;
    }

    if ((tmp->geturx() == _llx + (signed long)wx - 1) && (tmp->getury() == _lly + (signed long)wy-1)) {
      /* ur corner; import stitches */
      rt->ur.x = tmp->ur.x;
      rt->ur.y = tmp->ur.y;
      flag |= 2;
    }

    /* left edge */
    if (tmp->llx == _llx) {
      Tile *x = tmp->ll.x;
      while (x && (x->getury() <= _lly + (signed long)wy - 1)) {
	if (x->ur.x == rt) break; //-- done this already
	x->ur.x = rt;
	x = x->ur.y;
      }
    }

    /* right edge */
    if (tmp->geturx() == _llx + (signed long)wx - 1) {
      Tile *x = tmp->ur.x;
      while (x && (x->lly >= _lly)) {
	if (x->ll.x == rt) break;
	x->ll.x = rt;
	x = x->ll.y;
      }
    }

    /* bottom edge */
    if (tmp->lly == _lly) {
      Tile *x = tmp->ll.y;
      while (x && (x->geturx() <= _llx + (signed long)wx - 1)) {
	if (x->ur.y == rt) break;
	x->ur.y = rt;
	x = x->ur.x;
      }
    }

    /* top edge */
    if (tmp->getury() == _lly + (signed long)wy - 1) {
      Tile *x = tmp->ur.y;
      while (x && (x->llx >= _llx)) {
	if (x->ll.y == rt) break;
	x->ll.y = rt;
	x = x->ll.x;
      }
    }
    list_append (l, tmp);
  }
  list_free (ml);

  if (flag != 3) {
    warning ("new tile link error: ll = %d ; ur = %d", flag & 1, (flag >> 1));
    //rt->print();
  }
#if 0
  printf ("-- new tile: ");
  rt->print ();
  printf ("-- all below --\n");
  printall ();
  printf ("--- end all ---\n");
  fflush (stdout);
#endif
  
  for (li = list_first (l); li; li = list_next (li)) {
    Tile *tmp = (Tile *) list_value (li);
#if 0
    printf ("delete #%d\n", tmp->idx);
    fflush (stdout);
#endif
    delete tmp;
  }
  list_free (l);

#if 0
  printf ("--- after ----\n");
  printall();
  printf ("\n");
  fflush (stdout);
#endif  

  return rt;
}


int Tile::addVirt (int flavor, int type,
		   long _llx, long _lly, unsigned long wx, unsigned long wy)

{
  /*
    collect all tiles
  */
  list_t *l = collectRect (_llx, _lly, wx, wy);

  if (list_isempty (l)) {
    fatal_error ("Tile::collectRect() failed!");
  }

  /* 
     walk through all the tiles with overlap, and prune them so that
     we have created a region that is the specified rectangle 
  */
  
  while (!list_isempty (l)) {
    Tile *t;
    int new_attr;
    t = (Tile *) list_delete_tail (l);

    /* check virt flag */
    if (t->virt) return 0; /* failure! */

    if (t->isSpace()) {
      new_attr = TILE_FLGS_TO_ATTR(flavor, type, DIFF_OFFSET);
    }
    else {
      if (TILE_ATTR_ISROUTE(t->attr)) {
	/* route turns to fet */
	new_attr = TILE_FLGS_TO_ATTR(flavor, type, FET_OFFSET);
      }
      else if (TILE_ATTR_ISFET(t->attr) || TILE_ATTR_ISDIFF (t->attr)) {
	/* check it matches */
	if (flavor != TILE_ATTR_TO_FLAV (t->attr)) return 0;
	if (type != TILE_ATTR_TO_TYPE (t->attr)) return 0;
	continue;
      }
      else {
	Assert (0, "What?");
      }
    }

    if (t->llx < _llx) {
      t = t->splitX (_llx);	/* left edge prune */ 
    }
    if (t->lly < _lly) {
      t = t->splitY (_lly);	/* bottom edge prune */
    }
    if (t->nextx() > _llx + (signed long)wx) {
      t->splitX (_llx+(signed long)wx);	/* right edge prune */
    }
    if (t->nexty() > _lly + (signed long)wy) {
      t->splitY (_lly + (signed long)wy);	/* top edge prune */
    }
    t->virt = 1;
    t->space = 0;
    t->attr = new_attr;
  }
  return 1;
}


/*
 *  Split a tile at X coordinate specified. Returns the new tile.
 */
Tile *Tile::splitX (long x)
{
#if 0
  printf ("--- split X @ %ld ---------------------\n", x);
  printf ("TILE: ");
  print();
  printall ();
  printf ("---\n");
#endif  
  
  Assert (llx < x && xmatch (x), "What?");

  Tile *t = new Tile ();

  t->space = space;
  t->virt = virt;
  t->attr = attr;
  //t->up = up;
  //t->down = down;
  t->net = net;

  t->llx = x;
  t->lly = lly;

  t->ur.x = ur.x;
  t->ur.y = ur.y;
  t->ll.x = this;

  /* find t->ll.y */
  if (lly == MIN_VALUE) {
    t->ll.y = NULL;
  }
  else {
    t->ll.y = find (x, lly-1);
  }

  if (ur.y) {
    ur.y = find (x-1, nexty());
  }
  ur.x = t;


  /* XXX: fix stiches on the top edge and bottom edge */
  Tile *tmp = t->ur.y;
  while (tmp && tmp->llx >= x) {
    tmp->ll.y = t;
    tmp = tmp->ll.x;
  }

  tmp = ll.y;
  while (tmp && tmp->geturx() <= t->geturx()) {
    if (tmp->geturx() >= x) {
      tmp->ur.y = t;
    }
    tmp = tmp->ur.x;
  }

  /* fix right edge too! */
  tmp = t->ur.x;
  while (tmp && tmp->lly >= lly) {
    tmp->ll.x = t;
    tmp = tmp->ll.y;
  }
  

#if 0
  printf ("new tiles:\n");
  printall();
  printf ("----end split X @ %ld --------------------\n", x);
  fflush (stdout);
#endif  
  
  return t;
}


/*
 * Split a tile at the y-coordinate specified
 */
Tile *Tile::splitY (long y)
{
#if 0
  printf ("----- split Y @ %ld -------------------\n", y);
  printf ("TILE: ");
  print();
  printall();
  printf ("--\n");
#endif
  
  Assert (lly < y && ymatch (y), "What?");

  Tile *t = new Tile ();

  t->space = space;
  t->virt = virt;
  t->attr = attr;
  //t->up = up;
  //t->down = down;
  t->net = net;

  t->llx = llx;
  t->lly = y;

  t->ur.x = ur.x;
  t->ur.y = ur.y;
  t->ll.y = this;

  /* find t->ll.x */
  if (llx == MIN_VALUE) {
    t->ll.x = NULL;
  }
  else {
    t->ll.x = find (llx-1, y);
  }

  if (ur.x) {
    ur.x = find (nextx(), y-1);
  }
  ur.y = t;

  /* XXX: fix stiches on the left edge and right edge */
  Tile *tmp = ll.x;
  while (tmp && tmp->getury() <= t->getury()) {
    if (tmp->getury() >= y) {
      tmp->ur.x = t;
    }
    tmp = tmp->ur.y;
  }

  tmp = t->ur.x;
  while (tmp && tmp->lly >= y) {
    tmp->ll.x = t;
    tmp = tmp->ll.y;
  }

  /* fix top edge too! */
  tmp = t->ur.y;
  while (tmp && tmp->llx >= llx) {
    tmp->ll.y = t;
    tmp = tmp->ll.x;
  }

#if 0
  printf ("new tiles:\n");
  printall();
  printf ("----- end split Y @ %ld -------------------\n", y);
  fflush (stdout);
#endif

  return t;
}


int Tile::isConnected (Layer *l, Tile *t1, Tile *t2)
{
  unsigned int a1, a2;

  a1 = t1->getAttr();
  a2 = t2->getAttr();

  if (t1->isSpace() || t2->isSpace()) {
    return 0;
  }
  
  if (l->isMetal()) {
    /* for metal layers */
    if (t1->isVirt() || t2->isVirt()) {
      /* virtual metal, ignore */
      return 0;
    }
    /* all metals are connected */
    return 1;
  }
  else {
    if ((t1->isVirt () && TILE_ATTR_ISDIFF (a1))
	|| (t2->isVirt () && TILE_ATTR_ISDIFF (a2))) {
      /* actually a space tile! */
      return 0;
    }
    if (a1 == a2) {
      /* exactly the same */
      return 1;
    }
    /* base layer */
    if ((TILE_ATTR_ISROUTE (a1) || TILE_ATTR_ISFET (a1)) &&
	(TILE_ATTR_ISROUTE (a2) || TILE_ATTR_ISFET (a2))) {
      return 1;
    }
    if ((TILE_ATTR_ISDIFF (a1) || TILE_ATTR_ISWDIFF (a1)) &&
	(TILE_ATTR_ISDIFF (a2) || TILE_ATTR_ISWDIFF (a2))) {
      return 1;
    }
  }
  return 0;
}
