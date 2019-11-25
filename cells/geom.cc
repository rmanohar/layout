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
#include <stdio.h>
#include <list.h>
#include <config.h>
#include <act/act.h>
#include <act/passes.h>
#include <act/passes/netlist.h>
#include <act/tech.h>
#include <qops.h>
#include "geom.h"


#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif


#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

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
  
  up = NULL;
  down = NULL;
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
  
  up = NULL;
  down = NULL;
  space = 1;
  virt = 0;
  attr = 0;
  net = NULL;
}
  

Layer::Layer (Material *m, netlist_t *_n)
{
  mat = m;
  N = _n;

  up = NULL;
  down = NULL;
  other = NULL;
  nother = 0;

  hint = new Tile();
  vhint = new Tile();

  hint->up = vhint;
  vhint->down = hint;
}

Layer::~Layer()
{
  /* XXX: delete all tiles! */
  
  
}

void Layer::allocOther (int sz)
{
  Assert (other == NULL, "Hmm");
  Assert (sz > 0, "Hmm");
  MALLOC (other, Material *, sz);
  for (int i=0; i < sz; i++) {
    other[i] = NULL;
  }
  nother = sz;
  Assert (nother <= 64, "attr field is not big enough?");
}

void Layer::setOther (int idx, Material *m)
{
  Assert (other[idx] == NULL, "What?");
  other[idx] = m;
}

void Layer::setDownLink (Layer *x)
{
  down = x;
  x->up = this;

  /* link the tiles as well */
  x->vhint->up = hint;
  hint->down = x->vhint;
}

bool Layout::_initdone = false;
  
void Layout::Init()
{
  if (_initdone) return;
  _initdone = true;
  Technology::Init ("layout.conf");
}

Layout::Layout(netlist_t *_n)
{
  Layout::Init();
  
  /*-- create all the layers --*/
  Assert (Technology::T, "Initialization error");

  N = _n;

  /* 1. base layer for diff, well, fets */
  base = new Layer (Technology::T->poly, _n);

  /* 2. Also has #flavors*6 materials! */
  int sz = config_get_table_size ("act.dev_flavors");
  Assert (sz > 0, "Hmm");

  nflavors = sz;
  nmetals = Technology::T->nmetals;

  base->allocOther (sz*4);
  for (int i=0; i < sz; i++) {
    base->setOther (TOTAL_OFFSET(i, EDGE_NFET, FET_OFFSET),
		    Technology::T->fet[EDGE_NFET][i]);
    base->setOther (TOTAL_OFFSET(i, EDGE_PFET, FET_OFFSET),
		    Technology::T->fet[EDGE_PFET][i]);

    base->setOther (TOTAL_OFFSET(i, EDGE_NFET, DIFF_OFFSET),
		    Technology::T->diff[EDGE_NFET][i]);
    base->setOther (TOTAL_OFFSET(i, EDGE_PFET, DIFF_OFFSET),
		    Technology::T->diff[EDGE_PFET][i]);
  }

  Layer *prev = base;

  MALLOC (metals, Layer *, Technology::T->nmetals);

  /* create metal layers */
  for (int i=0; i < Technology::T->nmetals; i++) {
    metals[i] = new Layer (Technology::T->metal[i], _n);
    metals[i]->setDownLink (prev);
    prev = metals[i];
  }
}

Layout::~Layout()
{
  Layer *t, *l;

  t = base;

  while (t) {
    l = t;
    t = t->up;
    delete l;
  }
}


FetMat *Layout::getFet (int type, int flavor)
{
  FetMat *f;
  Material *m;
  m = base->other[TOTAL_OFFSET(flavor,type,FET_OFFSET)];
  f = Technology::T->fet[type][flavor];
  Assert (f == m, "Eh?");
  return f;
}

DiffMat *Layout::getDiff (int type, int flavor)
{
  DiffMat *d;
  Material *m;
  m = base->other[TOTAL_OFFSET(flavor,type,DIFF_OFFSET)];
  d = Technology::T->diff[type][flavor];
  Assert (d == m, "Eh?");
  return d;
}

WellMat *Layout::getWell (int type, int flavor)
{
  return Technology::T->well[type][flavor];
}


PolyMat *Layout::getPoly ()
{
  return Technology::T->poly;
}



void printtile (void *x, Tile *t)
{
  printf ("(%ld, %ld) -> (%ld, %ld)\n", t->getllx(), t->getlly(),
	  t->geturx(), t->getury());
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

  list_t *ml;
  listitem_t *li;

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
  t->up = up;
  t->down = down;
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
  t->up = up;
  t->down = down;
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


int Layer::drawVia (long llx, long lly, unsigned long wx, unsigned long wy,
		    void *net, int attr)
{
  Tile *x;

  x = vhint->addRect (llx, lly, wx, wy);
  if (!x) return 0;

  if (!x->space) {
    /* overwriting a net */
    if (x->net && net && x->net != net) {
      return 0;
    }
    if (x->attr != attr) {
      return 0;
    }
  }
  x->space = 0;
  x->attr = attr;
  if (net) {
    x->net = net;
  }
  return 1;
}

int Layer::isMetal ()
{
  if (nother > 0) return 0;
  return 1;
}

int Layer::drawVia (long llx, long lly, unsigned long wx, unsigned long wy,
		    int type)
{
  return drawVia (llx, lly, wx, wy, NULL, type);
}


int Layer::Draw (long llx, long lly, unsigned long wx, unsigned long wy,
		 void *net, int attr)
{
  Tile *x;

  x = hint->addRect (llx, lly, wx, wy);
  if (!x) return 0;

  if (!x->space) {
    /* overwriting a net */
    if (x->net && net && x->net != net) {
      return 0;
    }
    if (x->attr != attr) {
      return 0;
    }
  }
  x->space = 0;
  x->attr = attr;
  if (net) {
    x->net = net;
  }
  return 1;
}

int Layer::DrawVirt (int flavor, int type,
		     long llx, long lly, unsigned long wx, unsigned long wy)
{
  return hint->addVirt (flavor, type, llx, lly, wx, wy);
}

int Layer::Draw (long llx, long lly, unsigned long wx, unsigned long wy,
		 int type)
{
  return Draw (llx, lly, wx, wy, NULL, type);
}


int Layout::DrawPoly (long llx, long lly, unsigned long wx, unsigned long wy,
		      void *net)
{
  return base->Draw (llx, lly, wx, wy, net, 0);
}


int Layout::DrawDiff (int flavor /* fet flavor */, int type /* n or p */,
		      long llx, long lly, unsigned long wx, unsigned long wy,
		      void *net)
{
  int attr = 1 + TOTAL_OFFSET (flavor, type, DIFF_OFFSET);
  
  if (flavor < 0 || flavor >= nflavors) return 0;

#if 0  
  Assert (TILE_ATTR_TO_TYPE (attr) == type, "what?");
  Assert (TILE_ATTR_TO_OFF (attr) == DIFF_OFFSET, "What?");
  Assert (TILE_ATTR_TO_FLAV (attr) == flavor, "What?");
#endif  

  return base->Draw (llx, lly, wx, wy, net, attr);
}


int Layout::DrawFet (int flavor, int type,
		     long llx, long lly, unsigned long wx, unsigned long wy,
		     void *net)
{
  int attr =  1 + TOTAL_OFFSET (flavor, type, FET_OFFSET);
  if (flavor < 0 || flavor >= nflavors) return 0;

#if 0  
  Assert (TILE_ATTR_TO_TYPE (attr) == type, "what?");
  Assert (TILE_ATTR_TO_OFF (attr) == FET_OFFSET, "What?");
  Assert (TILE_ATTR_TO_FLAV (attr) == flavor, "What?");
#endif  
  
  return base->Draw (llx, lly, wx, wy, net, attr);
}

int Layout::DrawDiffBBox (int flavor, int type,
			  long llx, long lly, unsigned long wx, unsigned long wy)
{
  if (flavor < 0 || flavor >= nflavors) return 0;
  return base->DrawVirt (flavor, type, llx, lly, wx, wy);
}

  /* 0 = metal1, etc. */
int Layout::DrawMetal (int num, long llx, long lly, unsigned long wx, unsigned long wy, void *net)
{
  if (num < 0 || num >= nmetals) return 0;
  /* XXX: colors? */
  return metals[num]->Draw (llx, lly, wx, wy, net, 0);
}

int Layout::DrawMetalPin (int num, long llx, long lly, unsigned long wx, unsigned long wy, void *net, int dir)
{
  /* XXX: colors? */
  int attr = 0;
  if (num < 0 || num >= nmetals) return 0;

  TILE_ATTR_MKPIN (attr);
  if (dir) {
    TILE_ATTR_MKOUTPUT (attr);
  }
  return metals[num]->Draw (llx, lly, wx, wy, net, attr);
}

/* 0 = base to metal1, 1 = metal1 to metal2, etc. */
int Layout::DrawVia (int num, long llx, long lly, unsigned long wx, unsigned long wy)
{
  if (num < 0 || num > nmetals) return 0;
  /* XXX: colors? */
  if (num == 0) {
    return base->drawVia (llx, lly, wx, wy, 0);
  }
  else {
    return metals[num-1]->drawVia (llx, lly, wx, wy, 0);
  }
}


void Layout::PrintRect (FILE *fp, TransformMat *t)
{
  base->PrintRect (fp, t);
  for (int i=0; i < nmetals; i++) {
    metals[i]->PrintRect (fp, t);
  }
}


static void append_nonspacetile (void *cookie, Tile *t)
{
  list_t *l = (list_t *) cookie;

  if (!t->isSpace()) {
    list_append (l, t);
  }
}

static void dump_node (FILE *fp, netlist_t *N, node_t *n)
{
  if (n->v) {
    ActId *tmp = n->v->v->id->toid();
    tmp->Print (fp);
    delete tmp;
  }
  else {
    if (n == N->Vdd) {
      fprintf (fp, "Vdd");
    }
    else if (n == N->GND) {
      fprintf (fp, "GND");
    }
    else {
      fprintf (fp, "#%d", n->i);
    }
  }
}

void Layer::PrintRect (FILE *fp, TransformMat *t)
{
  list_t *l;

  l = list_new ();

  //debug_apply = 1;
  
  hint->applyTiles (MIN_VALUE, MIN_VALUE,
		    (unsigned long)MAX_VALUE - (MIN_VALUE + 1), (unsigned long)MAX_VALUE - (MIN_VALUE + 1),
		    l, append_nonspacetile);

  //hint->printall();
  
  //debug_apply = 0;


  while (!list_isempty (l)) {
    Tile *tmp = (Tile *) list_delete_tail (l);

    if (tmp->virt && TILE_ATTR_ISDIFF (tmp->getAttr())) {
      /* this is actually a space tile (virtual diff) */
      continue;
    }

    if (mat != Technology::T->poly && tmp->isPin()) {
      if (TILE_ATTR_ISOUTPUT(tmp->attr)) {
	fprintf (fp, "outrect ");
      }
      else {
	fprintf (fp, "inrect ");
      }
    }
    else {
      fprintf (fp, "rect ");
    }

    if (tmp->net) {
      dump_node (fp, N, (node_t *)tmp->net);
    }
    else {
      fprintf (fp, "#");
    }

    if ((tmp->virt && TILE_ATTR_ISFET(tmp->getAttr()))) {
      fprintf (fp, " %s", mat->getName());
    }
    else if (TILE_ATTR_ISROUTE(tmp->getAttr()) || (nother == 0)) {
      fprintf (fp, " %s", mat->getName());
    }
    else {
      fprintf (fp, " %s", other[TILE_ATTR_NONPOLY(tmp->getAttr())]->getName());
    }
    
    long llx, lly, urx, ury;

    if (t) {
      t->apply (tmp->getllx(), tmp->getlly(), &llx, &lly);
      t->apply (tmp->geturx(), tmp->getury(), &urx, &ury);

      if (llx > urx) {
	long x = llx;
	llx = urx;
	urx = x;
      }
      if (lly > ury) {
	long x = lly;
	lly = ury;
	ury = x;
      }
    }
    else {
      llx = tmp->getllx();
      lly = tmp->getlly();
      urx = tmp->geturx();
      ury = tmp->getury();
    }
    
    fprintf (fp, " %ld %ld %ld %ld\n", llx, lly, urx+1, ury+1);
  }    
  list_free (l);
}



LayoutBlob::LayoutBlob (blob_type type, Layout *lptr)
{
  t = type;

  edges[0] = NULL;
  edges[1] = NULL;
  edges[2] = NULL;
  edges[3] = NULL;
  
  switch (t) {
  case BLOB_BASE:
    base.l = lptr;
    lptr->getBBox (&llx, &lly, &urx, &ury);
    /* XXX: set edge attributes */
    break;

  case BLOB_HORIZ:
  case BLOB_VERT:
  case BLOB_MERGE:
    l.hd = NULL;
    l.tl = NULL;
    if (lptr) {
      blob_list *bl;
      NEW (bl, blob_list);
      bl->b = new LayoutBlob (BLOB_BASE, lptr);
      bl->next = NULL;
      bl->gap = 0;
      bl->shift = 0;
      bl->mirror = MIRROR_NONE;
      q_ins (l.hd, l.tl, bl);
      llx = bl->b->llx;
      lly = bl->b->lly;
      urx = bl->b->urx;
      ury = bl->b->ury;
    }
    else {
      llx = 0;
      lly = 0;
      urx = -1;
      ury = -1;
    }
    break;

  default:
    fatal_error ("What is this?");
    break;
  }
}


void LayoutBlob::appendBlob (LayoutBlob *b, long gap, mirror_type m)
{
  if (t == BLOB_BASE) {
    warning ("LayoutBlob::appendBlob() called on BASE; error ignored!");
    return;
  }
  
  blob_list *bl;
  NEW (bl, blob_list);
  bl->next = NULL;
  bl->b = b;
  bl->gap = gap;
  bl->shift = 0;
  bl->mirror = m;
  q_ins (l.hd, l.tl, bl);

  if (l.hd == l.tl) {
    llx = b->llx;
    lly = b->lly;
    urx = b->urx;
    ury = b->ury;
  }
  else {
    if (t == BLOB_HORIZ) {
      int d1, d2;
      int aret;
      /* align! */
      aret = GetAlignment (l.tl->b->edges[LAYOUT_EDGE_RIGHT], b->edges[LAYOUT_EDGE_LEFT], &d1, &d2);
      if (aret == 0) {
	warning ("appendBlob: no valid alignment, but continuing anyway");
	d1 = 0;
      }
      else if (aret == 1) {
	d1 = 0;
      }
      else if (aret == 2) {
	d1 = (d1 + d2)/2;
      }
      bl->shift = d1;
      
      lly = MIN (lly, b->lly + bl->shift);
      ury = MAX (ury, b->ury + bl->shift);

      urx = urx + gap + (b->urx - b->llx + 1);
    }
    else if (t == BLOB_VERT) {
      int d1, d2;
      int aret;
      /* align! */
      aret = GetAlignment (l.tl->b->edges[LAYOUT_EDGE_TOP], b->edges[LAYOUT_EDGE_BOTTOM], &d1, &d2);
      if (aret == 0) {
	warning ("appendBlob: no valid alignment, but continuing anyway");
	d1 = 0;
      }
      else if (aret == 1) {
	d1 = 0;
      }
      else if (aret == 2) {
	d1 = (d1 + d2)/2;
      }
      bl->shift = d1;

      llx = MIN (llx, b->llx + bl->shift);
      urx = MAX (urx, b->urx + bl->shift);

      ury = ury + gap + (b->ury - b->lly + 1);
    }
    else if (t == BLOB_MERGE) {
      llx = MIN (llx, b->llx);
      lly = MIN (lly, b->lly);
      urx = MAX (urx, b->urx);
      ury = MAX (ury, b->ury);
    }
    else {
      fatal_error ("What?");
    }
  }
}


void LayoutBlob::PrintRect (FILE *fp, TransformMat *mat)
{
  if (t == BLOB_BASE) {
    base.l->PrintRect (fp, mat);
  }
  else if (t == BLOB_HORIZ || t == BLOB_VERT) {
    TransformMat m;
    if (mat) {
      m = *mat;
    }
    for (blob_list *bl = l.hd; bl; q_step (bl)) {
      long llx, lly, urx, ury;
      if (bl != l.hd) {
	if (t == BLOB_HORIZ) {
	  m.applyTranslate (bl->gap, bl->shift);
	}
	else {
	  m.applyTranslate (bl->shift, bl->gap);
	}
      }
      bl->b->PrintRect (fp, &m);
      if (t == BLOB_HORIZ) {
	m.applyTranslate (bl->b->urx - bl->b->llx + 1, 0);
      }
      else {
	m.applyTranslate (0, bl->b->ury - bl->b->lly + 1);
      }
    }
  }
  else if (t == BLOB_MERGE) {
    TransformMat m;
    if (mat) {
      m = *mat;
    }
    for (blob_list *bl = l.hd; bl; q_step (bl)) {
      bl->b->PrintRect (fp, &m);
    }
  }
  else {
    fatal_error ("Unknown blob\n");
  }
}

void TransformMat::mkI ()
{
  int i, j;
  for (i=0; i < 3; i++)
    for (j=0; j < 3; j++)
      m[i][j] = (i == j) ? 1 : 0;
}


TransformMat::TransformMat()
{
  mkI();
}

static void print_mat (long m[3][3])
{
  printf ("mat: [ [%ld %ld %ld] [%ld %ld %ld] [%ld %ld %ld] ]\n",
	  m[0][0], m[0][1], m[0][2],
	  m[1][0], m[1][1], m[1][2],
	  m[2][0], m[2][1], m[2][2]);
}

static void mat_mult (long res[3][3], long a[3][3], long b[3][3])
{
  long tmp[3][3];
  int i, j, k;

  for (i=0; i < 3; i++)
    for (j=0; j < 3; j++) {
      tmp[i][j] = 0;
    }
  
  for (i=0; i < 3; i++)
    for (j = 0; j < 3; j++)
      for (k = 0; k < 3; k++) {
	tmp[i][j] += a[i][k]*b[k][j];
      }
  
  for (i=0; i < 3; i++)
    for (j=0; j < 3; j++) {
      res[i][j] = tmp[i][j];
    }
}

void TransformMat::applyRot90()
{
  long rotmat[3][3] = { { 0, -1, 0 },
			{ 1, 0, 0 },
			{ 0, 0, 1 } };

  mat_mult (m, rotmat, m);
}

void TransformMat::mirrorLR()
{
  long mirror[3][3] = { { -1, 0, 0 },
			{ 0, 1, 0 },
			{ 0, 0, 1 } };

  mat_mult (m, mirror, m);
}



void TransformMat::mirrorTB()
{
  long mirror[3][3] = { { 1, 0, 0 },
			{ 0, -1, 0 },
			{ 0, 0, 1 } };

  mat_mult (m, mirror, m);
}

void TransformMat::applyTranslate (long dx, long dy)
{
  long translate[3][3] = { { 1, 0, 0 },
			   { 0, 1, 0 },
			   { 0, 0, 1 } };

  translate[0][2] = dx;
  translate[1][2] = dy;

  mat_mult (m, translate, m);
}


void TransformMat::apply (long inx, long iny, long *outx, long *outy)
{
  long z;
  *outx = inx*m[0][0] + iny*m[0][1] + m[0][2];
  *outy = inx*m[1][0] + iny*m[1][1] + m[1][2];
  z = inx*m[2][0] + iny*m[2][1] + m[2][2];
  Assert (z == 1, "What?");
}

void Layer::BBox (long *llx, long *lly, long *urx, long *ury, int type)
{
  list_t *l;
  long xllx, xlly, xurx, xury;
  int first = 1;

  l = list_new ();
  
  hint->applyTiles (MIN_VALUE+1, MIN_VALUE+1,
		    (unsigned long)MAX_VALUE - (MIN_VALUE + 1), (unsigned long)MAX_VALUE - (MIN_VALUE + 1),
		    l, append_nonspacetile);

  while (!list_isempty (l)) {
    Tile *tmp = (Tile *) list_delete_tail (l);

    if (tmp->virt && TILE_ATTR_ISDIFF (tmp->getAttr())) {
      /* this is actually a space tile (virtual diff) */
      continue;
    }

    if (type != -1) {
      int attr;
      attr = tmp->getAttr ();
      if (tmp->virt) {
	if (TILE_ATTR_ISFET (tmp->getAttr())) {
	  attr = 0; /* routing */
	}
      }
      if (type != attr) continue;
    }
    
    if (first) {
      xllx = tmp->getllx ();
      xlly = tmp->getlly ();
      xurx = tmp->geturx ();
      xury = tmp->getury ();
      first = 0;
    }
    else {
      xllx = MIN(xllx, tmp->getllx ());
      xlly = MIN(xlly, tmp->getlly ());
      xurx = MAX(xurx, tmp->geturx ());
      xury = MAX(xury, tmp->getury ());
    }
  }
  if (!first) {
    *llx = xllx;
    *lly = xlly;
    *urx = xurx;
    *ury = xury;
  }
  else {
    *llx = 0;
    *lly = 0;
    *urx = -1;
    *ury = -1;
  }
}


void Layout::getBBox (long *llx, long *lly, long *urx, long *ury)
{
  long a, b, c, d;
  int set;

  set = 0;
  base->BBox (&a, &b, &c, &d);
  if (a <= c && b <= d) {
    *llx = a;
    *lly = b;
    *urx = c;
    *ury = d;
    set = 1;
  }
  for (int i=0; i < nmetals; i++) {
    metals[i]->BBox (&a, &b, &c, &d);
    if (a <= c && b <= d) {
      if (set) {
	*llx = MIN (*llx, a);
	*lly = MIN (*lly, b);
	*urx = MAX (*urx, c);
	*ury = MAX (*ury, d);
      }
      else {
	*llx = a;
	*lly = b;
	*urx = c;
	*ury = d;
	set = 1;
      }
    }
  }
  if (!set) {
    *llx = 0;
    *lly = 0;
    *urx = -1;
    *ury = -1;
  }
}


void LayoutBlob::getBBox (long *llxp, long *llyp,
			  long *urxp, long *uryp)
{
  *llxp = llx;
  *llyp = lly;
  *urxp = urx;
  *uryp = ury;
}


static void *_searchnet = NULL;
static void appendnet (void *cookie, Tile *t)
{
  list_t *l = (list_t *)cookie;
  if (t->getNet () == _searchnet) {
    list_append (l, t);
  }
}

list_t *Layer::search (void *net)
{
  list_t *l = list_new ();
  _searchnet = net;
  hint->applyTiles (MIN_VALUE, MIN_VALUE,
		    (unsigned long)MAX_VALUE + -(MIN_VALUE + 1),
		    (unsigned long)MAX_VALUE + -(MIN_VALUE + 1), l, appendnet);
  _searchnet = NULL;
  return l;
}

/*
  Returns a list with alternating  (Layer, listoftiles)
*/
list_t *Layout::search (void *net)
{
  list_t *ret = list_new ();
  list_t *tmp;

  tmp = base->search (net);
  if (list_isempty (tmp)) {
    list_free (tmp);
  }
  else {
    list_append (ret, base);
    list_append (ret, tmp);
  }
  
  for (int i=0; i < nmetals; i++) {
    list_t *l = metals[i]->search (net);
    if (list_isempty (l)) {
      list_free (l);
    }
    else {
      list_append (ret, metals[i]);
      list_append (ret, l);
    }
  }
  return ret;
}

list_t *LayoutBlob::search (void *net, TransformMat *m)
{
  TransformMat tmat;
  list_t *tiles;

  if (m) {
    tmat = *m;
  }
  if (t == BLOB_BASE) {
    tiles = base.l->search (net);
    if (list_isempty (tiles)) {
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
  else if (t == BLOB_MERGE || t == BLOB_HORIZ || t == BLOB_VERT) {
    blob_list *bl;
    tiles = list_new ();
    
    for (bl = l.hd; bl; q_step (bl)) {
      if (bl == l.hd) {
	/* no change to tmat */
      }
      else {
	if (t == BLOB_MERGE) {
	  /* no change to tmat */
	}
	else if (t == BLOB_HORIZ) {
	  tmat.applyTranslate (bl->gap, bl->shift);
	}
	else if (t == BLOB_VERT) {
	  tmat.applyTranslate (bl->shift, bl->gap);
	}
	else {
	  fatal_error ("What is this?");
	}
      }
      list_t *tmp = bl->b->search (net, &tmat);
      list_concat (tiles, tmp);
      list_free (tmp);
    }
  }
  else {
    fatal_error ("New blob?");
  }
  return tiles;
}



static unsigned long snap_to (unsigned long w, unsigned long pitch)
{
  if (w % pitch != 0) {
    w += pitch - (w % pitch);
  }
  return w;
}


/*
  Returns LEF boundary in blob coordinate system
*/
void LayoutBlob::calcBoundary (long *bllx, long *blly,
			       long *burx, long *bury)
{
  if (llx > urx || lly > ury) {
    *bllx = 0;
    *blly = 0;
    *burx = -1;
    *bury = -1;
    return;
  }

  Assert (Technology::T->nmetals >= 3, "Hmm");

  long padx = 0, pady = 0;

  RoutingMat *m1 = Technology::T->metal[0];
  RoutingMat *m2 = Technology::T->metal[1];
  RoutingMat *m3 = Technology::T->metal[2];

#if 0  
  if (Technology::T->nmetals < 5) {
    padx = 2*m2->getPitch();
    pady = 2*m3->getPitch();
    pady = snap_to (pady, m1->getPitch());
  }
#endif

  /* calculate diff spacing */
  int diff_spc = Technology::T->getMaxDiffSpacing();

  *burx = snap_to (urx - llx + 1 + diff_spc + 2*padx, m2->getPitch());
  *bury = snap_to (ury - lly + 1 + diff_spc + 2*pady, m1->getPitch());

  *bllx = llx - padx;
  *blly = lly - pady;
  *burx = *burx + llx - padx;
  *bury = *bury + lly - pady;
}


int LayoutBlob::GetAlignment (LayoutEdgeAttrib *a1, LayoutEdgeAttrib *a2,
		    int *d1, int *d2)
{
  if (!a1 && !a2) {
    return 1;
  }
  if ((a1 && !a2) || (!a1 && a2)) {
    return 0;
  }
  /* XXX: now check! */
  *d1 = 0;
  *d2 = 0;
  return 2;
}
