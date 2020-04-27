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
#include <string.h>
#include <list.h>
#include <config.h>
#include <act/act.h>
#include <act/passes.h>
#include <act/passes/netlist.h>
#include <act/tech.h>
#include <qops.h>
#include <act/layout/geom.h>

#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif


#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif


Layer::Layer (Material *m, netlist_t *_n)
{
  mat = m;
  N = _n;

  up = NULL;
  down = NULL;
  other = NULL;
  nother = 0;
  bbox = 0;

  hint = new Tile();
  vhint = new Tile();

  //hint->up = vhint;
  //vhint->down = hint;
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
  //x->vhint->up = hint;
  //hint->down = x->vhint;
}

bool Layout::_initdone = false;
  
void Layout::Init()
{
  if (_initdone) return;
  _initdone = true;
  Technology::Init ("layout.conf");
}

#define LMAP_VIA 0
#define LMAP_DIFF 1
#define LMAP_WDIFF 2
#define LMAP_FET 3

struct layermap {
  Layer *l;
  int etype;			/* n/p, if needed */
  int flavor;			/* flavor */
  unsigned int lcase:2;  // LMAP_<what> is it?
};
  

Layout::Layout(netlist_t *_n)
{
  struct layermap *lp;
  hash_bucket_t *b;
  
  Layout::Init();
  
  /*-- create all the layers --*/
  Assert (Technology::T, "Initialization error");

  N = _n;

  lmap = hash_new (8);

  /* 1. base layer for diff, well, fets */
  base = new Layer (Technology::T->poly, _n);

  /* 2. Also has #flavors*6 materials! */
  int sz = config_get_table_size ("act.dev_flavors");
  Assert (sz > 0, "Hmm");

  nflavors = sz;
  nmetals = Technology::T->nmetals;

  base->allocOther (sz*6);
  for (int i=0; i < sz; i++) {
    base->setOther (TOTAL_OFFSET(i, EDGE_NFET, FET_OFFSET),
		    Technology::T->fet[EDGE_NFET][i]);

    NEW (lp, struct layermap);
    lp->l = base;
    lp->etype = EDGE_NFET;
    lp->flavor = i;
    lp->lcase = LMAP_FET;

    b = hash_add (lmap, Technology::T->fet[EDGE_NFET][i]->getName());
    b->v = lp;
    
    
    base->setOther (TOTAL_OFFSET(i, EDGE_PFET, FET_OFFSET),
		    Technology::T->fet[EDGE_PFET][i]);

    NEW (lp, struct layermap);
    lp->l = base;
    lp->etype = EDGE_PFET;
    lp->flavor = i;
    lp->lcase = LMAP_FET;

    b = hash_add (lmap, Technology::T->fet[EDGE_PFET][i]->getName());
    b->v = lp;


    base->setOther (TOTAL_OFFSET(i, EDGE_NFET, DIFF_OFFSET),
		    Technology::T->diff[EDGE_NFET][i]);

    NEW (lp, struct layermap);
    lp->l = base;
    lp->etype = EDGE_NFET;
    lp->flavor = i;
    lp->lcase = LMAP_DIFF;

    b = hash_add (lmap, Technology::T->diff[EDGE_NFET][i]->getName());
    b->v = lp;

    /* add via */
    if (Technology::T->diff[EDGE_NFET][i]->getUpC()) {

      if (!hash_lookup (lmap, Technology::T->diff[EDGE_NFET][i]->getUpC()->getName())) {
	NEW (lp, struct layermap);
	lp->l = base;
	lp->etype = EDGE_NFET;
	lp->flavor = i;
	lp->lcase = LMAP_VIA;

	b = hash_add (lmap, Technology::T->diff[EDGE_NFET][i]->getUpC()->getName());
	b->v = lp;
      }
    }

    
    base->setOther (TOTAL_OFFSET(i, EDGE_PFET, DIFF_OFFSET),
		    Technology::T->diff[EDGE_PFET][i]);

    NEW (lp, struct layermap);
    lp->l = base;
    lp->etype = EDGE_PFET;
    lp->flavor = i;
    lp->lcase = LMAP_DIFF;

    b = hash_add (lmap, Technology::T->diff[EDGE_PFET][i]->getName());
    b->v = lp;

    if (Technology::T->diff[EDGE_PFET][i]->getUpC()) {
      if (!hash_lookup (lmap,
			Technology::T->diff[EDGE_PFET][i]->getUpC()->getName())) {
      
	NEW (lp, struct layermap);
	lp->l = base;
	lp->etype = EDGE_PFET;
	lp->flavor = i;
	lp->lcase = LMAP_VIA;

	b = hash_add (lmap, Technology::T->diff[EDGE_PFET][i]->getUpC()->getName());
	b->v = lp;
      }
    }

    

    base->setOther (TOTAL_OFFSET(i, EDGE_NFET, WDIFF_OFFSET),
		    Technology::T->welldiff[EDGE_NFET][i]);

    if (Technology::T->welldiff[EDGE_NFET]) {
      NEW (lp, struct layermap);
      lp->l = base;
      lp->etype = EDGE_NFET;
      lp->flavor = i;
      lp->lcase = LMAP_WDIFF;

      b = hash_add (lmap, Technology::T->welldiff[EDGE_NFET][i]->getName());
      b->v = lp;

      if (Technology::T->welldiff[EDGE_NFET][i]->getUpC()) {
	if (!hash_lookup (lmap, Technology::T->diff[EDGE_NFET][i]->getUpC()->getName())) {
	  NEW (lp, struct layermap);
	  lp->l = base;
	  lp->etype = EDGE_NFET;
	  lp->flavor = i;
	  lp->lcase = LMAP_VIA;

	  b = hash_add (lmap, Technology::T->diff[EDGE_NFET][i]->getUpC()->getName());
	  b->v = lp;
	}
      }
    }

    base->setOther (TOTAL_OFFSET(i, EDGE_PFET, WDIFF_OFFSET),
		    Technology::T->welldiff[EDGE_PFET][i]);

    if (Technology::T->welldiff[EDGE_PFET]) {
      NEW (lp, struct layermap);
      lp->l = base;
      lp->etype = EDGE_PFET;
      lp->flavor = i;
      lp->lcase = LMAP_WDIFF;

      b = hash_add (lmap, Technology::T->welldiff[EDGE_PFET][i]->getName());
      b->v = lp;

      if (Technology::T->welldiff[EDGE_PFET][i]->getUpC()) {
	if (!hash_lookup (lmap, Technology::T->diff[EDGE_PFET][i]->getUpC()->getName())) {
	  NEW (lp, struct layermap);
	  lp->l = base;
	  lp->etype = EDGE_PFET;
	  lp->flavor = i;
	  lp->lcase = LMAP_VIA;

	  b = hash_add (lmap, Technology::T->diff[EDGE_PFET][i]->getUpC()->getName());
	  b->v = lp;
	}
      }
    }
  }

  Layer *prev = base;

  MALLOC (metals, Layer *, Technology::T->nmetals);

  /* create metal layers */
  for (int i=0; i < Technology::T->nmetals; i++) {
    metals[i] = new Layer (Technology::T->metal[i], _n);
    metals[i]->setDownLink (prev);
    prev = metals[i];

    if (Technology::T->metal[i]->getUpC()) {
      NEW (lp, struct layermap);
      lp->l = metals[i];
      lp->etype = -1;
      lp->flavor = 0;
      lp->lcase = LMAP_VIA;

      b = hash_add (lmap, Technology::T->metal[i]->getUpC()->getName());
      b->v = lp;
    }
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

  hash_free (lmap);
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




int Layer::drawVia (long llx, long lly, unsigned long wx, unsigned long wy,
		    void *net, int attr)
{
  Tile *x;

  bbox = 0;

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

  bbox = 0;

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
  bbox = 0;
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

int Layout::DrawWellDiff (int flavor /* fet flavor */, int type /* n or p */,
			  long llx, long lly, unsigned long wx, unsigned long wy,
			  void *net)
{
  int attr = 1 + TOTAL_OFFSET (flavor, type, WDIFF_OFFSET);
  
  if (flavor < 0 || flavor >= nflavors) return 0;

#if 0  
  Assert (TILE_ATTR_TO_TYPE (attr) == type, "what?");
  Assert (TILE_ATTR_TO_OFF (attr) == WDIFF_OFFSET, "What?");
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

void Layout::ReadRect (const char *fname)
{
  FILE *fp;
  char buf[10240];
  int rtype = 0;
  int offset;
  char *net;
  Process *p;

  if (!N || !N->bN || !N->bN->p) {
    warning ("Layout::ReadRect() skipped; no netlist specified for layout");
    return;
  }
  /* the process */
  p = N->bN->p;
  Assert (p->isExpanded(), "What?");
  

  fp = fopen (fname, "r");
  if (!fp) {
    fatal_error ("Could not open `%s' rect file", fname);
  }
  while (fgets (buf, 10240, fp)) {
#if 0
    printf ("BUF: %s", buf);
#endif    
    offset = 0;
    if (strncmp (buf, "inrect ", 7) == 0) {
      offset = 7;
      rtype = 1;
    }
    else if (strncmp (buf, "outrect ", 8) == 0) {
      offset = 8;
      rtype = 2;
    }
    else if (strncmp (buf, "rect ", 5) == 0) {
      offset = 5;
      rtype = 0;
    }
    else {
      fatal_error ("Line: %s\nNeeds inrect, outrect, or rect", buf);
    }

    if (strncmp (buf+offset, "# ", 2) == 0) {
      net = NULL;
      offset += 2;
    }
    else {
      net = buf+offset;
      while (buf[offset] && buf[offset] != ' ') {
	offset++;
      }
      Assert (buf[offset], "Long line");
      buf[offset] = '\0';
      offset++;
    }

    node_t *n = NULL;

    if (net) {
      n = ActNetlistPass::string_to_node (N, net);
    }

    char *material;
    material = buf+offset;

    while (buf[offset] && buf[offset] != ' ') {
      offset++;
    }
    Assert (buf[offset], "Long line");
    buf[offset] = '\0';
    offset++;

    long rllx, rlly, rurx, rury;
    sscanf (buf+offset, "%ld %ld %ld %ld", &rllx, &rlly, &rurx, &rury);

#if 0
    printf ("[%s] rtype=%d, net=%s, (%ld, %ld) -> (%ld, %ld)\n", material,
	    rtype, net ? net : "-none-", rllx, rlly, rurx, rury);
#endif

    /* now find the material/layer, and draw it */
    if (material[0] == 'm' && isdigit(material[1])) {
      /* m# is a metal layer */
      int l;
      sscanf (material+1, "%d", &l);
#if 0
      printf ("metal %d\n", l);
#endif

      if (l < 1 || l > Technology::T->nmetals) {
	warning ("Technology has %d metal layers; found `%s'; skipped",
		 Technology::T->nmetals, material);
      }
      else {
	l--;
	/*--- draw metal ---*/
	DrawMetal (l, rllx, rlly, rurx - rllx, rury - rlly, n);
      }
    }
    else if (strcmp (material, base->mat->getName()) == 0) {
      /* poly */
#if 0
      printf ("poly\n");
#endif
      /*--- draw poly ---*/
      DrawPoly (rllx, rlly, rurx - rllx, rury - rlly, n);
    }
    else {
      struct layermap *lm;
      hash_bucket_t *b;
      b = hash_lookup (lmap, material);
      if (b) {
	/*--- draw base layer or via ---*/
	lm = (struct layermap *) b->v;
	switch (lm->lcase) {
	case LMAP_DIFF:
	  DrawDiff (lm->flavor, lm->etype, rllx, rlly,
		    rurx - rllx, rury - rlly, n);
	  break;
	  
	case LMAP_FET:
	  DrawFet (lm->flavor, lm->etype, rllx, rlly,
		   rurx - rllx, rury - rlly, n);
	  break;
	case LMAP_WDIFF:
	  DrawWellDiff (lm->flavor, lm->etype, rllx, rlly,
			rurx - rllx, rury - rlly, n);
	  break;
	case LMAP_VIA:
	  lm->l->drawVia (rllx, rlly, rurx - rllx, rury - rlly, n, 0);
	  break;
	default:
	  fatal_error ("Unknown lmap lcase %d?", lm->lcase);
	  break;
	}
      }
      else {
	warning ("Unknown material `%s'; skipped", material);
      }
    }
  }
  fclose (fp);
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
    
    fprintf (fp, " %ld %ld %ld %ld", llx, lly, urx+1, ury+1);

    /*-- now if there is a fet to the right or the left then print it! --*/
    if (tmp->net) {
      Tile *tllx, *turx;
      int fet_left, fet_right;
      tllx = tmp->llxTile();
      turx = tmp->urxTile();
      
      if (tllx && !tllx->isSpace() && TILE_ATTR_ISFET (tllx->getAttr())) {
	fet_left = 1;
      }
      else {
	fet_left = 0;
      }
      if (turx && !turx->isSpace() && TILE_ATTR_ISFET (turx->getAttr())) {
	fet_right = 1;
      }
      else {
	fet_right = 0;
      }

      if (fet_left && fet_right) {
	fprintf (fp, " center");
      }
      else if (fet_right) {
	fprintf (fp, " left");
      }
      else if (fet_left) {
	fprintf (fp, " right");
      }
    }
    fprintf (fp, "\n");
  }    
  list_free (l);

  if (vhint) {
    l = list_new ();
    
    vhint->applyTiles (MIN_VALUE, MIN_VALUE,
		       (unsigned long)MAX_VALUE - (MIN_VALUE + 1), (unsigned long)MAX_VALUE - (MIN_VALUE + 1),
		       l, append_nonspacetile);

    while (!list_isempty (l)) {
      Tile *tmp = (Tile *) list_delete_tail (l);

      fprintf (fp, "rect ");
      if (tmp->net) {
	dump_node (fp, N, (node_t *)tmp->net);
      }
      else {
	fprintf (fp, "#");
      }
      fprintf (fp, " %s", ((RoutingMat *)mat)->getUpC()->getName());

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
}

void LayoutBlob::setBBox (long _llx, long _lly, long _urx, long _ury)
{
  if (t != BLOB_BASE || base.l) {
    warning ("LayoutBlob::setBBox(): called on non-empty layout; ignored");
    return;
  }
  if (llx == 0 && lly == 0 && urx == -1 && ury == -1) {
    llx = _llx;
    lly = _lly;
    urx = _urx;
    ury = _ury;
  }
  else {
    if (_llx > llx || _lly > lly ||
	_urx < urx || _ury < ury) {
      fatal_error ("LayoutBlob::setBBox(): shrinking the bounding box is not permitted");
    }
  }
  llx = _llx;
  lly = _lly;
  urx = _urx;
  ury = _ury;
  bloatllx = _llx;
  bloatlly = _lly;
  bloaturx = _urx;
  bloatury = _ury;
}


LayoutBlob::LayoutBlob (blob_type type, Layout *lptr)
{
  t = type;

  edges[0] = NULL;
  edges[1] = NULL;
  edges[2] = NULL;
  edges[3] = NULL;
  count = 0;

  switch (t) {
  case BLOB_BASE:
    base.l = lptr;
    if (lptr) {
      lptr->getBBox (&llx, &lly, &urx, &ury);
      lptr->getBloatBBox (&bloatllx, &bloatlly, &bloaturx, &bloatury);
    }
    else {
      llx = 0;
      lly = 0;
      urx = -1;
      ury = -1;
      bloatllx = 0;
      bloatlly = 0;
      bloaturx = -1;
      bloatury = -1;
    }
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
      bloatllx = bl->b->bloatllx;
      bloatlly = bl->b->bloatlly;
      bloaturx = bl->b->bloaturx;
      bloatury = bl->b->bloatury;
    }
    else {
      llx = 0;
      lly = 0;
      urx = -1;
      ury = -1;
      bloatllx = 0;
      bloatlly = 0;
      bloaturx = -1;
      bloatury = -1;
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
    bloatllx = b->bloatllx;
    bloatlly = b->bloatlly;
    bloaturx = b->bloaturx;
    bloatury = b->bloatury;
    if (t == BLOB_HORIZ) {
      llx += bl->gap;
      urx += bl->gap;
      bloatllx += bl->gap;
      bloaturx += bl->gap;
    }
    else if (t == BLOB_VERT) {
      lly += bl->gap;
      ury += bl->gap;
      bloatlly += bl->gap;
      bloatury += bl->gap;
    }
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

      bloatlly = MIN (bloatlly, b->bloatlly + bl->shift);
      bloatury = MAX (bloatury, b->bloatury + bl->shift);

      int shiftamt = (bloaturx - llx) + (b->llx - b->bloatllx + 1);

      urx = b->urx + gap + shiftamt;
      bloaturx = b->bloaturx + gap + shiftamt;
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

      bloatllx = MIN (bloatllx, b->bloatllx + bl->shift);
      bloaturx = MAX (bloaturx, b->bloaturx + bl->shift);
      
      int shiftamt = (bloatury - lly) + (b->lly - b->bloatlly + 1);

      ury = b->ury + gap + shiftamt;
      bloatury = b->bloatury + gap + shiftamt;
    }
    else if (t == BLOB_MERGE) {
      llx = MIN (llx, b->llx);
      lly = MIN (lly, b->lly);
      urx = MAX (urx, b->urx);
      ury = MAX (ury, b->ury);
      bloatllx = MIN (bloatllx, b->bloatllx);
      bloatlly = MIN (bloatlly, b->bloatlly);
      bloaturx = MAX (bloaturx, b->bloaturx);
      bloatury = MAX (bloatury, b->bloatury);
    }
    else {
      fatal_error ("What?");
    }
  }
#if 0
  printf ("blob: (%ld,%ld) -> (%ld,%ld); bloat: (%ld,%ld)->(%ld,%ld)\n",
	  llx, lly, urx, ury, bloatllx, bloatlly, bloaturx, bloatury);
#endif
}


void LayoutBlob::PrintRect (FILE *fp, TransformMat *mat)
{
  if (t == BLOB_BASE) {
    if (base.l) {
      base.l->PrintRect (fp, mat);
    }
  }
  else if (t == BLOB_HORIZ || t == BLOB_VERT) {
    TransformMat m;
    if (mat) {
      m = *mat;
    }
#if 0    
    long tllx, tlly;
    m.apply (0, 0, &tllx, &tlly);
    printf ("orig mat: (%ld,%ld)\n", tllx, tlly);
#endif    
    for (blob_list *bl = l.hd; bl; q_step (bl)) {
      if (t == BLOB_HORIZ) {
	m.applyTranslate (bl->gap, bl->shift);
      }
      else {
	m.applyTranslate (bl->shift, bl->gap);
      }
#if 0      
      m.apply (0, 0, &tllx, &tlly);
      printf ("gap: (%ld,%ld); tmat gets you to: (%ld,%ld)\n",
	      bl->gap, bl->shift, tllx, tlly);
      printf ("   (%ld,%ld) -> (%ld,%ld)  :: bloat: (%ld,%ld) -> (%ld,%ld)\n",
	      bl->b->llx, bl->b->lly, bl->b->urx, bl->b->ury,
	      bl->b->bloatllx, bl->b->bloatlly, bl->b->bloaturx, bl->b->bloatury);
#endif      
      bl->b->PrintRect (fp, &m);
      if (t == BLOB_HORIZ) {
	if (bl->next) {
	  int shiftamt = (bl->b->bloaturx - bl->b->llx + 1)
	    + (bl->next->b->llx - bl->next->b->bloatllx + 1);
	  m.applyTranslate (shiftamt, 0);
	}
      }
      else {
	if (bl->next) {
	  int shiftamt = (bl->b->bloatury - bl->b->lly + 1)
	    + (bl->next->b->lly - bl->next->b->bloatlly + 1);
	  m.applyTranslate (0, shiftamt);
	}
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

  Assert (res[2][0] == 0 &&
	  res[2][1] == 0 &&
	  res[2][2] == 1, "Hmm...");
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

void TransformMat::inverse (TransformMat *inp)
{
  /* invert m */
  long tmp[3][3];

  /* 
     Assumes: just rotations and translations.
     This also means the third row is [ 0 0 1 ]
  */

  m[0][0] = inp->m[1][1];
  m[0][1] = -inp->m[0][1];
  m[1][0] = -inp->m[1][0];
  m[1][1] = inp->m[0][0];

  m[0][2] = inp->m[0][1]*inp->m[1][2] - inp->m[0][2]*inp->m[1][1];
  m[1][2] = inp->m[1][0]*inp->m[0][2] - inp->m[0][0]*inp->m[1][2];
  
  m[2][0] = 0;
  m[2][1] = 0;
  m[2][2] = 1;


  /* this assumes the discriminant is 1 */

  /* check */
  mat_mult (tmp, m, inp->m);
  for (int i=0; i < 3; i++) {
    for (int j=0; j < 3; j++) {
      if (j == i) {
	Assert (tmp[i][j] == 1, "Hmm");
      }
      else {
	Assert (tmp[i][j] == 0, "Hmm");
      }
    }
  }
}


void TransformMat::apply (long inx, long iny, long *outx, long *outy)
{
  long z;
  *outx = inx*m[0][0] + iny*m[0][1] + m[0][2];
  *outy = inx*m[1][0] + iny*m[1][1] + m[1][2];
  z = inx*m[2][0] + iny*m[2][1] + m[2][2];
  Assert (z == 1, "What?");
}

void Layer::getBloatBBox (long *llx, long *lly, long *urx, long *ury)
{
  if (!bbox) {
    getBBox (llx, lly, urx, ury);
  }
  *llx = _bllx;
  *lly = _blly;
  *urx = _burx;
  *ury = _bury;
}

void Layer::getBBox (long *llx, long *lly, long *urx, long *ury)
{
  list_t *l;
  long xllx, xlly, xurx, xury;
  long bxllx, bxlly, bxurx, bxury;
  int first = 1;
  long bloat;

  if (bbox) {
    /* cached */
    *llx = _llx;
    *lly = _lly;
    *urx = _urx;
    *ury = _ury;
    return;
  }

  l = list_new ();
  
  hint->applyTiles (MIN_VALUE+1, MIN_VALUE+1,
		    (unsigned long)MAX_VALUE - (MIN_VALUE + 1), (unsigned long)MAX_VALUE - (MIN_VALUE + 1),
		    l, append_nonspacetile);

  xllx = 0;
  xlly = 0;
  xurx = -1;
  xury = -1;
  bxllx = 0;
  bxlly = 0;
  bxurx = -1;
  bxury = -1;

  while (!list_isempty (l)) {
    Tile *tmp = (Tile *) list_delete_tail (l);
    long tllx, tlly, turx, tury;

    if (tmp->virt && TILE_ATTR_ISDIFF (tmp->getAttr())) {
      /* this is actually a space tile (virtual diff) */
      continue;
    }

    tllx = tmp->getllx ();
    tlly = tmp->getlly ();
    turx = tmp->geturx ();
    tury = tmp->getury ();

    /* compute bloat for the tile */
    if (TILE_ATTR_ISROUTE(tmp->getAttr())) {
      bloat = ((RoutingMat *)mat)->minSpacing();
    }
    else if (nother == 0 && TILE_ATTR_ISPIN(tmp->getAttr())) {
      bloat = ((RoutingMat *)mat)->minSpacing();
    }
    else {
      Material *mo;
      Assert (nother > 0, "What?");
      Assert (TILE_ATTR_ISROUTE(tmp->getAttr()) < nother, "What?");
      mo = other[TILE_ATTR_NONPOLY(tmp->getAttr())];
      Assert (mo, "What?");

      if (TILE_ATTR_ISFET (tmp->getAttr())) {
	bloat = ((FetMat *)mo)->getSpacing(0);
      }
      else if (TILE_ATTR_ISDIFF(tmp->getAttr()) ||
	       TILE_ATTR_ISWDIFF(tmp->getAttr())) {
	bloat = Technology::T->getMaxSameDiffSpacing();
      }
      else {
	fatal_error ("Bad attributes?!");
      }
    }

    /* half bloat of min spacing; round up so that you can mirror the
       cells; if mirroring is not allowed during placement, we can
       change this to two different bloats: left/bot could be 
       floor(bloat/2), and right/top could be ceil(bloat/2).
    */
    bloat = (bloat + 1)/2;

    if (first) {
      xllx = tllx;
      xlly = tlly;
      xurx = turx;
      xury = tury;
      bxllx = tllx - bloat + 1;
      bxlly = tlly - bloat + 1;
      bxurx = turx + bloat;
      bxury = tury + bloat;
      first = 0;
    }
    else {
      xllx = MIN(xllx, tllx);
      xlly = MIN(xlly, tlly);
      xurx = MAX(xurx, turx);
      xury = MAX(xury, tury);

      bxllx = MIN(bxllx, tllx - bloat + 1);
      bxlly = MIN(bxlly, tlly - bloat + 1);
      bxurx = MAX(bxurx, turx + bloat);
      bxury = MAX(bxury, tury + bloat);
    }
  }
  
  *llx = xllx;
  *lly = xlly;
  *urx = xurx;
  *ury = xury;

  bbox = 1;
  _llx = xllx;
  _lly = xlly;
  _urx = xurx;
  _ury = xury;
  _bllx = bxllx;
  _blly = bxlly;
  _burx = bxurx;
  _bury = bxury;
}


void Layout::getBBox (long *llx, long *lly, long *urx, long *ury)
{
  long a, b, c, d;
  int set;

  set = 0;
  base->getBBox (&a, &b, &c, &d);
  if (a <= c && b <= d) {
    *llx = a;
    *lly = b;
    *urx = c;
    *ury = d;
    set = 1;
  }
  for (int i=0; i < nmetals; i++) {
    metals[i]->getBBox (&a, &b, &c, &d);
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

void Layout::getBloatBBox (long *llx, long *lly, long *urx, long *ury)
{
  long a, b, c, d;
  int set;

  set = 0;
  base->getBloatBBox (&a, &b, &c, &d);
  if (a <= c && b <= d) {
    *llx = a;
    *lly = b;
    *urx = c;
    *ury = d;
    set = 1;
  }
  for (int i=0; i < nmetals; i++) {
    metals[i]->getBloatBBox (&a, &b, &c, &d);
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

void LayoutBlob::getBloatBBox (long *llxp, long *llyp,
			       long *urxp, long *uryp)
{
  *llxp = bloatllx;
  *llyp = bloatlly;
  *urxp = bloaturx;
  *uryp = bloatury;
}


static void *_searchnet = NULL;
static void appendnet (void *cookie, Tile *t)
{
  list_t *l = (list_t *)cookie;
  if (t->getNet () == _searchnet) {
    list_append (l, t);
  }
}

static int _searchtype = -1;
static void appendtype (void *cookie, Tile *t)
{
  list_t *l = (list_t *)cookie;
  if (t->getAttr() == _searchtype) {
    list_append (l, t);
  }
}

list_t *Layer::searchMat (void *net)
{
  list_t *l = list_new ();
  _searchnet = net;
  hint->applyTiles (MIN_VALUE, MIN_VALUE,
		    (unsigned long)MAX_VALUE + -(MIN_VALUE + 1),
		    (unsigned long)MAX_VALUE + -(MIN_VALUE + 1), l, appendnet);
  _searchnet = NULL;
  return l;
}

list_t *Layer::searchMat (int type)
{
  list_t *l = list_new ();
  _searchtype = type;
  hint->applyTiles (MIN_VALUE, MIN_VALUE,
		    (unsigned long)MAX_VALUE + -(MIN_VALUE + 1),
		    (unsigned long)MAX_VALUE + -(MIN_VALUE + 1), l, appendtype);
  return l;
}

list_t *Layer::searchVia (void *net)
{
  list_t *l = list_new ();
  _searchnet = net;
  vhint->applyTiles (MIN_VALUE, MIN_VALUE,
		    (unsigned long)MAX_VALUE + -(MIN_VALUE + 1),
		    (unsigned long)MAX_VALUE + -(MIN_VALUE + 1), l, appendnet);
  _searchnet = NULL;
  return l;
}

list_t *Layer::searchVia (int type)
{
  list_t *l = list_new ();
  _searchtype = type;
  vhint->applyTiles (MIN_VALUE, MIN_VALUE,
		    (unsigned long)MAX_VALUE + -(MIN_VALUE + 1),
		    (unsigned long)MAX_VALUE + -(MIN_VALUE + 1), l, appendtype);
  return l;
}

list_t *Layer::allNonSpaceMat ()
{
  list_t *l = list_new ();
  hint->applyTiles (MIN_VALUE, MIN_VALUE,
		    (unsigned long)MAX_VALUE + -(MIN_VALUE + 1),
		    (unsigned long)MAX_VALUE + -(MIN_VALUE + 1), l,
		    append_nonspacetile);
  return l;
}

list_t *Layer::allNonSpaceVia ()
{
  list_t *l = list_new ();
  vhint->applyTiles (MIN_VALUE, MIN_VALUE,
		    (unsigned long)MAX_VALUE + -(MIN_VALUE + 1),
		    (unsigned long)MAX_VALUE + -(MIN_VALUE + 1), l,
		    append_nonspacetile);
  return l;
}


/*
  Returns a list with alternating  (Layer, listoftiles)
*/
list_t *Layout::search (void *net)
{
  list_t *ret = list_new ();
  list_t *tmp;

  tmp = base->searchMat (net);
  if (list_isempty (tmp)) {
    list_free (tmp);
  }
  else {
    list_append (ret, base);
    list_append (ret, tmp);
  }
  
  for (int i=0; i < nmetals; i++) {
    list_t *l = metals[i]->searchMat (net);
    if (list_isempty (l)) {
      list_free (l);
    }
    else {
      list_append (ret, metals[i]);
      list_append (ret, l);
    }

    l = metals[i]->searchVia (net);
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

/*
  Returns a list with alternating  (Layer, listoftiles)
*/
list_t *Layout::search (int type)
{
  list_t *ret = list_new ();
  list_t *tmp;

  tmp = base->searchMat (type);
  if (list_isempty (tmp)) {
    list_free (tmp);
  }
  else {
    list_append (ret, base);
    list_append (ret, tmp);
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
    if (base.l) {
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
    else {
      tiles = list_new ();
      return tiles;
    }
  }
  else if (t == BLOB_MERGE || t == BLOB_HORIZ || t == BLOB_VERT) {
    blob_list *bl;
    tiles = list_new ();
    
    for (bl = l.hd; bl; q_step (bl)) {
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
      list_t *tmp = bl->b->search (net, &tmat);
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

list_t *LayoutBlob::search (int type, TransformMat *m)
{
  TransformMat tmat;
  list_t *tiles;

  if (m) {
    tmat = *m;
  }
  if (t == BLOB_BASE) {
    if (base.l) {
      tiles = base.l->search (type);
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
    else {
      tiles = list_new ();
      return tiles;
    }
  }
  else if (t == BLOB_MERGE || t == BLOB_HORIZ || t == BLOB_VERT) {
    blob_list *bl;
    tiles = list_new ();
    
    for (bl = l.hd; bl; q_step (bl)) {
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
      list_t *tmp = bl->b->search (type, &tmat);
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
  for (tli = list_first (slist); tli; tli = list_next (tli)) {
    struct tile_listentry *tle = (struct tile_listentry *) list_value (tli);

    /* a transform matrix + list of (layer,tile-list) pairs */
    listitem_t *xi;
    for (xi = list_first (tle->tiles); xi; xi = list_next (xi)) {
      //Layer *name = (Layer *) list_value (xi);
      xi = list_next (xi);
      Assert (xi, "What?");
      
      list_t *actual_tiles = (list_t *) list_value (xi);
      listitem_t *ti;

      for (ti = list_first (actual_tiles); ti; ti = list_next (ti)) {
	long tllx, tlly, turx, tury;
	Tile *tmp = (Tile *) list_value (ti);

	tle->m.apply (tmp->getllx(), tmp->getlly(), &tllx, &tlly);
	tle->m.apply (tmp->geturx(), tmp->getury(), &turx, &tury);

	if (tllx > turx) {
	  long x = tllx;
	  tllx = turx;
	  turx = x;
	}
	  
	if (tlly > tury) {
	  long x = tlly;
	  tlly = tury;
	  tury = x;
	}
	
	if (!init) {
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
  if (!init) {
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
  
  for (tli = list_first (slist); tli; tli = list_next (tli)) {
    struct tile_listentry *tle = (struct tile_listentry *) list_value (tli);

    /* a transform matrix + list of (layer,tile-list) pairs */
    listitem_t *xi;
    for (xi = list_first (tle->tiles); xi; xi = list_next (xi)) {
      //Layer *name = (Layer *) list_value (xi);
      xi = list_next (xi);
      Assert (xi, "What?");
      
      list_t *actual_tiles = (list_t *) list_value (xi);
      list_free (actual_tiles);
    }
    list_free (tle->tiles);
    FREE (tle);
  }
  list_free (slist);
}


LayoutBlob *LayoutBlob::delBBox (LayoutBlob *b)
{
  if (!b) return NULL;
  if (b->t == BLOB_BASE) {
    if (b->base.l) {
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
    while (x) {
      x->b = LayoutBlob::delBBox (x->b);
      if (!x->b) {
	q_delete_item (b->l.hd, b->l.tl, prev, x);
	FREE (x);
	if (prev) {
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
    if (!b->l.hd) {
      delete b;
      return NULL;
    }
    else {
      return b;
    }
  }
}

/*
  Propagate net labels across the layout
*/
void Layout::propagateAllNets ()
{
  Layer *L;
  listitem_t *li;
  int flag;
  int rep;

  list_t **tl;

  /* 0 = base, 1 = viabase, 2 = metal1, etc.. */

  /* collect *all* tiles on *all* layers */
  MALLOC (tl, list_t *, 1 + 2*nmetals);

  L = base;
  tl[0] = base->allNonSpaceMat ();
  tl[1] = base->allNonSpaceVia ();
  
  for (int i=0; i < nmetals; i++) {
    Assert (L->up, "What?");
    L = L->up;
    tl[2*i+2] = metals[i]->allNonSpaceMat ();
    if (i != nmetals - 1) {
      tl[2*i+3] = metals[i]->allNonSpaceVia ();
    }
    else {
      Assert (L->up == NULL, "what?");
    }
  }

  do {
    rep = 0;
    L = base;
    for (int i=0; i < 2*nmetals + 1; i++) {
      Assert (L, "What?");
      if ((i & 1) == 0) {
	/* a horizontal layer; propagate within the layer */
	do {
	  flag = 0;
	  for (li = list_first (tl[i]); li; li = list_next (li)) {
	    Tile *t = (Tile *) list_value (li);
	    if (t->getNet()) {
	      Tile *neighbors[4];
	      neighbors[0] = t->llxTile();
	      neighbors[1] = t->llyTile();
	      neighbors[2] = t->urxTile();
	      neighbors[3] = t->uryTile();
	      for (int k=0; k < 4; k++) {
		if (Tile::isConnected (L, t, neighbors[k])) {
		  if (neighbors[k]->getNet()) {
		    if (t->getNet() != neighbors[k]->getNet()) {
		      warning ("Layer::propagateNet(): Tile at (%ld,%d) has connected neighbor (dir=%d) with different net", t->getllx(), t->getlly(), k);
		    }
		  }
		  else {
		    neighbors[k]->setNet (t->getNet());
		    flag = 1;
		    rep = 1;
		  }
		}
	      }
	    }
	  }
	} while (flag);
      }
      else {
	/* via layer: look at layers above and below and
	   inherit/propagate labels to directly connected tiles */

	for (li = list_first (tl[i]); li; li = list_next (li)) {
	  Tile *t = (Tile *) list_value (li);
	  Tile *up, *dn;
	  if (L->up) {
	    up = L->findVia (t->getllx(), t->getlly());
	  }
	  else {
	    up = NULL;
	  }
	  if (L->down) {
	    dn = L->down->findVia (t->getllx(), t->getlly());
	  }
	  else {
	    dn = NULL;
	  }

#define CHK_EQUAL(x,y)  (((x) && (y)) ? ((x)->getNet() == (y)->getNet()) : 1)

	  /* t is not NULL, so we have a via here */
	  if (CHK_EQUAL(dn,t) && CHK_EQUAL (up,t) && CHK_EQUAL (up,dn)) {
	    void *propnet;
	    if (dn && dn->getNet()) {
	      propnet = dn->getNet();
	    }
	    else if (up && up->getNet()) {
	      propnet = up->getNet();
	    }
	    else if (t->getNet()) {
	      propnet = t->getNet();
	    }
	    else {
	      /* nothing to do! */
	      propnet = NULL;
	    }
	    if (propnet) {
	      if (dn && !dn->getNet()) {
		dn->setNet(propnet);
		rep = 1;
	      }
	      if (!t->getNet()) {
		t->setNet (propnet);
		rep = 1;
	      }
	      if (up && !up->getNet()) {
		up->setNet (propnet);
		rep = 1;
	      }
	    }
	  }
	}
	L = L->up;
      }
    }
  } while (rep);

  for (int i=0; i < 2*nmetals + 1; i++) {
    list_free (tl[i]);
  }
  FREE (tl);
}




Tile *Layer::findVia (long llx, long lly)
{
  return vhint->find (llx, lly);
}
