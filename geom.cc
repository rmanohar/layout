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
#include <common/list.h>
#include <act/act.h>
#include <act/passes.h>
#include <act/passes/netlist.h>
#include <act/tech.h>
#include <common/qops.h>
#include "geom.h"


bool Layout::_initdone = false;
double Layout::_leak_adjust = 0.0;
  
void Layout::Init()
{
  if (_initdone) return;
  _initdone = true;

  if (!Technology::T) {
    Technology::Init ();
  }
  
  if (config_exists ("net.leakage_adjust")) {
    _leak_adjust = config_get_real ("net.leakage_adjust");
  }
  else {
    _leak_adjust = 0;
  }
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
  _readrect = false;

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

    if (Technology::T->welldiff[EDGE_NFET][i]) {
      NEW (lp, struct layermap);
      lp->l = base;
      lp->etype = EDGE_NFET;
      lp->flavor = i;
      lp->lcase = LMAP_WDIFF;

      b = hash_add (lmap, Technology::T->welldiff[EDGE_NFET][i]->getName());
      b->v = lp;

      if (Technology::T->welldiff[EDGE_NFET][i]->getUpC()) {
	if (!hash_lookup (lmap, Technology::T->welldiff[EDGE_NFET][i]->getUpC()->getName())) {
	  NEW (lp, struct layermap);
	  lp->l = base;
	  lp->etype = EDGE_NFET;
	  lp->flavor = i;
	  lp->lcase = LMAP_VIA;

	  b = hash_add (lmap, Technology::T->welldiff[EDGE_NFET][i]->getUpC()->getName());
	  b->v = lp;
	}
      }
    }

    base->setOther (TOTAL_OFFSET(i, EDGE_PFET, WDIFF_OFFSET),
		    Technology::T->welldiff[EDGE_PFET][i]);

    if (Technology::T->welldiff[EDGE_PFET][i]) {
      NEW (lp, struct layermap);
      lp->l = base;
      lp->etype = EDGE_PFET;
      lp->flavor = i;
      lp->lcase = LMAP_WDIFF;

      b = hash_add (lmap, Technology::T->welldiff[EDGE_PFET][i]->getName());
      b->v = lp;

      if (Technology::T->welldiff[EDGE_PFET][i]->getUpC()) {
	if (!hash_lookup (lmap, Technology::T->welldiff[EDGE_PFET][i]->getUpC()->getName())) {
	  NEW (lp, struct layermap);
	  lp->l = base;
	  lp->etype = EDGE_PFET;
	  lp->flavor = i;
	  lp->lcase = LMAP_VIA;

	  b = hash_add (lmap, Technology::T->welldiff[EDGE_PFET][i]->getUpC()->getName());
	  b->v = lp;
	}
      }
    }
  }

  Layer *prev = base;

  if (!hash_lookup (lmap, Technology::T->poly->getUpC()->getName())) {
    NEW (lp, struct layermap);
    lp->l = base;
    lp->etype = -1;
    lp->flavor = 0;
    lp->lcase = LMAP_VIA;

    b = hash_add (lmap, Technology::T->poly->getUpC()->getName());
    b->v = lp;
  }

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


  _rect_inpath = NULL;
  if (config_exists ("lefdef.rect_inpath")) {
    _rect_inpath = path_init ();
    path_add (_rect_inpath, config_get_string ("lefdef.rect_inpath"));
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

  if (_rect_inpath) {
    path_free (_rect_inpath);
    _rect_inpath = NULL;
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
  if (!_rbox.empty()) {
    fprintf (fp, "sbox %ld %ld %ld %ld\n", _rbox.llx(),
	     _rbox.lly(), _rbox.urx()+1, _rbox.ury()+1);
  }
  if (!_abutbox.empty()) {
    fprintf (fp, "rect # $align %ld %ld %ld %ld\n", _abutbox.llx(),
	     _abutbox.lly(), _abutbox.urx()+1, _abutbox.ury()+1);
  }
  LayoutEdgeAttrib::attrib_list *l;
  
  long x, y;

  if (_abutbox.empty()) {
    x = 0;
    y = 0;
  }
  else {
    x = _abutbox.llx();
    y = _abutbox.lly();
  }
  
  for (l = _le.left(); l; l = l->next) {
    fprintf (fp, "rect $l:%s $align %ld %ld %ld %ld\n",
	     l->name, x, l->offset, x, l->offset);
  }
  for (l = _le.right(); l; l = l->next) {
    fprintf (fp, "rect $r:%s $align %ld %ld %ld %ld\n",
	     l->name, x, l->offset, x, l->offset);
  }
  for (l = _le.top(); l; l = l->next) {
    fprintf (fp, "rect $t:%s $align %ld %ld %ld %ld\n",
	     l->name, l->offset, y, l->offset, y);
  }
  for (l = _le.bot(); l; l = l->next) {
    fprintf (fp, "rect $b:%s $align %ld %ld %ld %ld\n",
	     l->name, l->offset, y, l->offset, y);
  }
}

void Layout::markPins ()
{
  if (!N || !N->bN) return;

  for (int i=0; i < A_LEN (N->bN->ports); i++) {
    node_t *n;
    if (N->bN->ports[i].omit) continue;
    n = ActNetlistPass::connection_to_node (N, N->bN->ports[i].c);
#if 0
    printf ("%cpin: ", N->bN->ports[i].input ? 'i' : 'o');
    ActNetlistPass::emit_node (N, stdout, n, NULL, NULL, 0);
    printf ("\n");
#endif    
    for (int j=0; j < nmetals; j++) {
      metals[j]->markPins (n, N->bN->ports[i].input);
    }
  }
}


void Layout::ReadRect (Process *p, int raw_mode)
{
  char cname[10240];
  int len;

  if (!p) {
    snprintf (cname, 10240, "toplevel");
  }
  else {
    ActNamespace::Act()->msnprintfproc (cname, 10240, p);
  }
  len = strlen (cname);
  snprintf (cname + len, 10240 - len, ".rect");

  FILE *fp;
  char *tmpname;

  if (_rect_inpath) {
    tmpname = path_open (_rect_inpath, cname, NULL);
    fp = fopen (tmpname, "r");
  }
  else {
    tmpname = NULL;
    fp = fopen (cname, "r");
  }

  if (!fp) {
    fatal_error ("Looking for .rect files for %s; not found!", cname);
  }
  fclose (fp);

  if (tmpname) {
    ReadRect (tmpname, raw_mode);
    FREE (tmpname);
  }
  else {
    ReadRect (cname, raw_mode);
  }
}
  

void Layout::ReadRect (const char *fname, int raw_mode)
{
  FILE *fp;
  char buf[10240];
  int rtype = 0;
  int offset;
  char *net;
  Process *p;

  if (raw_mode == 0 && (!N || !N->bN || !N->bN->p)) {
    warning ("Layout::ReadRect() skipped; no netlist specified for layout");
    return;
  }
  /* the process */
  if (raw_mode == 0) {
    p = N->bN->p;
    Assert (p->isExpanded(), "What?");
  }
  else {
    p = NULL;
  }
  _readrect = true;
  _rbox.clear();
  fp = fopen (fname, "r");
  if (!fp) {
    fatal_error ("Could not open `%s' rect file", fname);
  }
  while (fgets (buf, 10240, fp)) {
#if 0
    printf ("BUF: %s", buf);
#endif    
    offset = 0;
    for (int bx=0; bx < 10240 && buf[bx]; bx++) {
      if (!isspace (buf[bx])) { break; }
      offset++;
    }
    if (buf[offset] == '\0') continue;
    if (strncmp (buf+offset, "inrect ", 7) == 0) {
      offset += 7;
      rtype = 1;
    }
    else if (strncmp (buf+offset, "outrect ", 8) == 0) {
      offset += 8;
      rtype = 2;
    }
    else if (strncmp (buf+offset, "rect ", 5) == 0) {
      offset += 5;
      rtype = 0;
    }
    else if (strncmp (buf+offset, "bbox ", 5) == 0) {
      // this is auto-generated, so ignore it.
      continue;
    }
    else if (strncmp (buf+offset, "sbox ", 5) == 0) {
      // this overrides the bbox definition, so keep it
      long rlx, rly, rux, ruy;
      sscanf (buf+5, "%ld %ld %ld %ld", &rlx, &rly, &rux, &ruy);
      _rbox.setRect (rlx, rly, rux - rlx, ruy - rly);
      continue;
    }
    else if (strncmp (buf+offset, "cell ", 5) == 0) {
      Assert (0, "FIXME: add support for subcells!");
      /* celltype id swap? flipx? flipy? dx dy llx lly urx ury */
    }
    else {
      fatal_error ("Line: %s\nNeeds inrect, outrect, rect, bbox, sbox, or cell", buf);
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

    char *material;
    material = buf+offset;

    while (buf[offset] && buf[offset] != ' ') {
      offset++;
    }
    Assert (buf[offset], "Long line");
    buf[offset] = '\0';
    offset++;

    if (net && (raw_mode == 0) && (strcmp (material, "$align") != 0)) {
      n = ActNetlistPass::string_to_node (N, net);
      if (!n) {
	warning ("Could not find signal `%s' in netlist!", net);
      }
      //printf ("signal %s [node 0x%lx]\n", net, (unsigned long)n);
    }


    long rllx, rlly, rurx, rury;
    sscanf (buf+offset, "%ld %ld %ld %ld", &rllx, &rlly, &rurx, &rury);

#if 0
    printf ("[%s] rtype=%d, net=%s, (%ld, %ld) -> (%ld, %ld)\n", material,
	    rtype, net ? net : "-none-", rllx, rlly, rurx, rury);
#endif

    if (rllx >= rurx || rlly >= rury) {
      warning ("[%s] Empty rectangle (%ld,%ld) -> (%ld,%ld); skipped",
	       material, rllx, rlly, rurx, rury);
      continue;
    }

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
	/*--- draw metal ---*/
	l--;
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
    else if (strcmp (material, "$align") == 0) {
      LayoutEdgeAttrib::attrib_list *l;
      NEW (l, LayoutEdgeAttrib::attrib_list);
      l->next = NULL;
      /* alignment information! */
      if (!net) {
	/* abutbox */
	_abutbox.setRect (rllx, rlly, rurx - rllx, rury - rlly);
      }
      else if (strncmp (net, "$l:", 3) == 0) {
	l->name = Strdup (net+3);
	l->offset = rlly; // left alignment: lower left corner y coord
	_le.mergeleft (l);
      }
      else if (strncmp (net, "$r:", 3) == 0) {
	l->name = Strdup (net+3);
	l->offset = rlly; // right alignment: lower left corner y coord
	_le.mergeright (l);
      }
      else if (strncmp (net, "$t:", 3) == 0) {
	l->name = Strdup (net+3);
	l->offset = rllx; // top alignment: lower left corner x coord
	_le.mergetop (l);
      }
      else if (strncmp (net, "$b:", 3) == 0) {
	l->name = Strdup (net+3);
	l->offset = rllx; // bot alignment: lower left corner x coord
	_le.mergebot (l);
      }
      else {
	warning ("Invalid alignment layer directive: `%s'; skipped", net);
      }
      FREE (l); // don't free name: that gets used by the merge
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
	int iswell = 0;
	for (int i=0; i < Technology::T->num_devs; i++) {
	  for (int j=0; j < 2; j++) {
	    if (Technology::T->well[j][i]) {
	      if (strcmp (material, Technology::T->well[j][i]->getName()) == 0) {
		iswell = 1;
		break;
	      }
	    }
	    if (iswell) {
	      break;
	    }
	  }
	}
	if (!iswell) {
	  warning ("Unknown material `%s'; skipped", material);
	}
	/* skip wells! */
      }
    }
  }
  fclose (fp);
}


void Layout::getBBox (long *llx, long *lly, long *urx, long *ury)
{
  long a, b, c, d;
  int set;

  if (_readrect && !_rbox.empty()) {
    /* override, use bbox from .rect file */
    *llx = _rbox.llx();
    *lly = _rbox.lly();
    *urx = _rbox.urx();
    *ury = _rbox.ury();
    return;
  }

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

  if (_readrect && !_rbox.empty()) {
    /* override, use bbox from .rect file */
    *llx = _rbox.llx();
    *lly = _rbox.lly();
    *urx = _rbox.urx();
    *ury = _rbox.ury();
    return;
  }

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

static void printtile (FILE *fp, Tile *t)
{
  fprintf (fp, "(%ld, %ld) -> (%ld, %ld)\n", t->getllx(), t->getlly(),
	  t->geturx(), t->getury());
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
	    Tile *neighbors[4];
	    neighbors[0] = t->llxTile();
	    neighbors[1] = t->llyTile();
	    neighbors[2] = t->urxTile();
	    neighbors[3] = t->uryTile();
	    if (t->getNet()) {
	      for (int k=0; k < 4; k++) {
		if (Tile::isConnected (L, t, neighbors[k])) {
		  if (neighbors[k]->getNet()) {
		    if (t->getNet() != neighbors[k]->getNet()) {
		      //warning ("[%s] Layer::propagateNet(): Tile at (%ld,%d)\n\t connected neighbor (dir=%d) with different net", N->bN->p->getName(),
		      //t->getllx(), t->getlly(), k);
		      warning ("[%s] Net propagation detected two nets are shorted.", N->bN->p->getName());
		      fprintf (stderr, "\tnet1: ");
		      ActNetlistPass::emit_node (N, stderr, (node_t *)t->getNet(), NULL, NULL);
		      fprintf (stderr, "; net2: ");
		      ActNetlistPass::emit_node (N, stderr, (node_t *)neighbors[k]->getNet(), NULL, NULL);
		      fprintf (stderr, "\n");
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
	    else {
	      for (int k=0; k < 4; k++) {
		if (Tile::isConnected (L, t, neighbors[k])) {
		  if (neighbors[k]->getNet()) {
		    t->setNet (neighbors[k]->getNet());
		    flag = 1;
		    rep = 1;
		    break;
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
	  Assert (L->up, "What?");
	  Assert (L, "What?");
	  up = L->up->find (t->getllx(), t->getlly());
	  dn = L->find (t->getllx(), t->getlly());

	  if (up->isSpace()) {
	    warning ("[%s] Missing upper metal %d layer at (%ld,%ld)?",
		     N->bN->p->getName(),
		     (i+1)/2, t->getllx(), t->getlly ());
	    continue;
	  }
	  if (dn->isSpace()) {
	    if (i == 1) {
	      warning ("[%s] Missing lower base layer at (%ld,%ld)?",
		       N->bN->p->getName(),
		       t->getllx(), t->getlly());
	    }
	    else {
	      warning ("[%s] Missing lower metal %d layer at (%ld,%ld)?",
		       N->bN->p->getName(),
		       (i-1)/2, t->getllx(), t->getlly ());
	    }
	    continue;
	  }

#define CHK_EQUAL(x,y)  ((x)->getNet() == (y)->getNet())
	  
#if 0
	  fprintf (stderr, "[%s] here %d, via=%d, up=%d, dn=%d ", 
		   N->bN->p->getName(), i,
		   t->getNet() ? 1 : 0,
		   up->getNet() ? 1 : 0,
		   dn->getNet() ? 1 : 0);
	  printtile (stderr, t);
#endif

	  if (!(CHK_EQUAL(dn,t) && CHK_EQUAL (up,t) && CHK_EQUAL (up,dn))) {
	    void *propnet = NULL;
	    if (dn->getNet()) {
	      propnet = dn->getNet();
	    }
	    if (up->getNet()) {
	      if (propnet && propnet != up->getNet()) {
		//warning ("[%s] Layer::propagateNet(): Tile at (%ld,%d) has connected neighbor (dir=up) with different net", N->bN->p->getName(),
		//t->getllx(), t->getlly());
		warning ("[%s] Net propagation detected two nets are shorted across layers.", N->bN->p->getName());
		fprintf (stderr, "\tnet1: ");
		ActNetlistPass::emit_node (N, stderr, (node_t *)propnet, NULL, NULL);
		fprintf (stderr, "; net2: ");
		ActNetlistPass::emit_node (N, stderr, (node_t *)up->getNet(), NULL, NULL);
		fprintf (stderr, "\n");
	      }
	      else {
		propnet = up->getNet();
	      }
	    }
	    if (t->getNet()) {
	      if (propnet && propnet != t->getNet()) {
		//warning ("[%s] Layer::propagateNet(): Via tile at (%ld,%d) has connected neighbor with different net", N->bN->p->getName(),
		//t->getllx(), t->getlly());
		warning ("[%s] Net propagation detected two nets are shorted.", N->bN->p->getName());
		fprintf (stderr, "\tnet1: ");
		ActNetlistPass::emit_node (N, stderr, (node_t *)propnet, NULL, NULL);
		fprintf (stderr, "; net2: ");
		ActNetlistPass::emit_node (N, stderr, (node_t *)t->getNet(), NULL, NULL);
		fprintf (stderr, "\n");
	      }
	      else {
		propnet = t->getNet();
	      }
	    }

	    if (propnet) {
#if 0	      
	      fprintf (stderr, "[%s] propnet: ", N->bN->p->getName());
	      ActNetlistPass::emit_node (N, stderr, (node_t *)propnet, NULL, NULL);
#endif	      
	      if (!dn->getNet()) {
#if 0		
		fprintf (stderr, " dn");
#endif
		dn->setNet(propnet);
		rep = 1;
	      }
	      if (!t->getNet()) {
#if 0		
		fprintf (stderr, " via");
#endif		
		t->setNet (propnet);
		rep = 1;
	      }
	      if (!up->getNet()) {
#if 0		
		fprintf (stderr, " up");
#endif		
		up->setNet (propnet);
		rep = 1;
	      }
#if 0	      
	      fprintf (stderr, "\n");
#endif	      
	    }
	  }
	  else {
#if 0	    
	    fprintf (stderr, "  -> nothing to do\n");
	    if (t->getNet()) {
	      fprintf (stderr, "  via:");
	      ActNetlistPass::emit_node (N, stderr, (node_t *)t->getNet(), NULL, NULL);
	      printtile (stderr, t);
	    }
	    if (up->getNet()) {
	      fprintf (stderr, " up:");
	      ActNetlistPass::emit_node (N, stderr, (node_t *)up->getNet(), NULL, NULL);
	      printtile (stderr, up);
	    }
	    if (dn->getNet()) {
	      fprintf (stderr, " dn:");
	      ActNetlistPass::emit_node (N, stderr, (node_t *)dn->getNet(), NULL, NULL);
	      printtile (stderr, dn);
	    }
	    fprintf (stderr, "\n");
#endif	    
	  }
	}
	L = L->up;
      }
    }
  } while (rep);

#if 1
  for (int i=0; i < nmetals; i++) {
    for (listitem_t *li = list_first (tl[2*i+2]); li; li = list_next (li)) {
      Tile *t = (Tile *) list_value (li);
      if (!t->getNet()) {
	fprintf (stderr, "[%s] metal %d : no net for ", N->bN->p->getName(), i+1);
	printtile (stderr, t);
      }
    }
  }
#endif  

  for (int i=0; i < 2*nmetals + 1; i++) {
    list_free (tl[i]);
  }
  FREE (tl);
}

list_t *Layout::searchAllMetal ()
{
  list_t *ret = list_new ();
  Layer *L;
  listitem_t *li;

  for (int i=0; i < nmetals; i++) {
    list_t *l = metals[i]->allNonSpaceMat ();
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
 * Transformation Matrix for coordinate transformations
 *
 *   Given (x,y): 
 *
 *   1. If flipx, flip the sign of x
 *   2. If flipy, flip the sign of y
 *  Then
 *   3. If swap, flip x and y
 *  Then:
 *   4. Translate by _dx, _dy
 *
 *  fx = +1 for no flip, -1 for flip
 *  fy = +1 for no flip, -1 for flip[
 *
 *  if (swap) {
 *     xnew = fy*y + dx
 *     ynew = fx*x + dy
 *  } else {
 *     xnew = fx*x + dx
 *     ynew = fy*y + dy
 *  }
 *
 *  Any new transformations are applied *on* the (xnew, ynew)
 *
 *  So:
 *
 *   mirrorLR
 *     dx updated to -dx
 *     fx flipped if noswap, fy fliiped if swap
 *
 *  mirrorTB
 *     dy updated to -dy
 *     fy flipped if noswap, fx fliiped if swap
 *
 *  translate
 *     dx,dy updated
 *
 *
 *  LEF/DEF and OpenAccess rotations. NOTE: this requires knowledge of
 *  the bounding box of the cell. The lower left corner of the LEF
 *  bounding box is always at (0,0), so the shift amount in the
 *  transformation matrix must be computed based on the LEF bounding
 *  box of the original cell.
 *
 *  N  = R0   = no flips/swaps
 *  S  = R180 = flipx + flipy
 *  FS = MX   = flipy
 *  FN = MY   = flipx
 *  W  = R90  = flipy + swap
 *  E  = R270 = flipx + swap
 *  FW = MX90 = swap
 *  FE = MY90 = flipx + flipy + swap
 *
 */
void TransformMat::mkI ()
{
  _dx = 0;
  _dy = 0;
  _flipx = 0;
  _flipy = 0;
  _swap = 0;
}


TransformMat::TransformMat()
{
  mkI();
}

void TransformMat::mirrorLR()
{
  _dx = -_dx;
  if (_swap) {
    _flipy = 1-_flipy;
  }
  else {
    _flipx = 1-_flipx;
  }
}


void TransformMat::mirrorTB()
{
  _dy = -_dy;
  if (_swap) {
    _flipx = 1-_flipx;
  }
  else {
    _flipy = 1-_flipy;
  }
}

void TransformMat::mirror45()
{
  long tmp;
  _swap = 1 - _swap;

  _flipx = 1 - _flipx;
  _flipy = 1 - _flipy;

  tmp = _dx;
  _dx = _dy;
  _dy = tmp;
}

void TransformMat::translate (long dx, long dy)
{
  _dx += dx;
  _dy += dy;
}

void TransformMat::apply (long inx, long iny, long *outx, long *outy) const
{
  long z;
  if (_swap) {
    *outx = (_flipy ? -iny : iny) + _dx;
    *outy = (_flipx ? -inx : inx) + _dy;
  }
  else {
    *outx = (_flipx ? -inx : inx) + _dx;
    *outy = (_flipy ? -iny : iny) + _dy;
  }
}

void TransformMat::applyMat (const TransformMat &t)
{
  if (t._swap) {
    mirror45();
  }
  if (t._flipx) {
    mirrorLR();
  }
  if (t._flipy) {
    mirrorTB();
  }
  _dx += t._dx;
  _dy += t._dy;
}

Rectangle TransformMat::applyBox (const Rectangle &r) const
{
  long llx, lly, urx, ury;
  Rectangle ret;

  if (r.empty()) {
    return ret;
  }

  apply (r.llx(), r.lly(), &llx, &lly);
  apply (r.urx(), r.ury(), &urx, &ury);

  if (llx > urx) {
    long tmp = llx;
    llx = urx;
    urx = tmp;
  }
  if (lly > ury) {
    long tmp = lly;
    lly = ury;
    ury = lly;
  }
  ret.setRect (llx, lly, urx - llx + 1, ury - lly + 1);
  return ret;
}

void TransformMat::Print (FILE *fp) const
{
  fprintf (fp, "{");
  if (_swap) {
    fprintf (fp, " sw");
  }
  if (_flipx) {
    fprintf (fp, " fx");
  }
  if (_flipy) {
    fprintf (fp, " fy");
  }
  fprintf (fp, " dx=%ld dy=%ld }", _dx, _dy);
}
