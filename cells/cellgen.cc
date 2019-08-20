#include <stdio.h>
#include <string.h>
#include <list.h>
#include <act/passes/netlist.h>
#include <tech.h>
#include <config.h>
#include "stacks.h"
#include "geom.h"
#include <string>

#ifdef INTEGRATED_PLACER
#include "placer.h"
#endif

#define MIN_TRACKS 18

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

static void dump_edge (netlist_t *N, edge_t *e)
{
  printf ("{");
  if (e->type == EDGE_NFET) {
    printf ("n%d:", e->flavor);
  }
  else {
    printf ("p%d:", e->flavor);
  }
  printf ("[f:%d]", e->nfolds);
  printf ("<%d,%d>", e->w, e->l);
  printf (" g:");
  dump_node (stdout, N, e->g);
  printf (" ");
  dump_node (stdout, N, e->a);
  printf (" ");
  dump_node (stdout, N, e->b);
  printf ("}");
}

#define EDGE_FLAGS_LEFT 0x1
#define EDGE_FLAGS_RIGHT 0x2

#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif


#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

struct BBox {
  int flavor;
  struct {
    int llx, lly, urx, ury;
  } p, n;
};


static int fold_n_width, fold_p_width, min_width;

static int getwidth (int idx, edge_t *e)
{
  if (e->type == EDGE_NFET) {
    if (fold_n_width != 0) {
      return EDGE_WIDTH (e,idx,fold_n_width,min_width);
    }
    else {
      return e->w;
    }
  }
  else {
    if (fold_p_width != 0) {
      return EDGE_WIDTH (e,idx,fold_p_width,min_width);
    }
    else {
      return e->w;
    }
  }
}

/*
  emits rectangles needed upto the FET.
  If it is right edge, also emits the final diffusion of the right edge.
*/
static int locate_fetedge (Layout *L, int dx,
			   unsigned int flags,
			   edge_t *prev, int previdx,
			   node_t *left, edge_t *e, int eidx)
{
  DiffMat *d;
  FetMat *f;
  PolyMat *p;
  int rect;
  int fet_type; /* -1 = downward notch, +1 = upward notch, 0 = same
		   width */

  int e_w = getwidth (0, e);

  /* XXX: THIS CODE IS COPIED FROM emit_rectangle!!!!! */

  d = L->getDiff (e->type, e->flavor);
  f = L->getFet (e->type, e->flavor);
  p = L->getPoly ();

  rect = 0;
  if (flags & EDGE_FLAGS_LEFT) {
    fet_type = 0;
    /* actual overhang rule */
    rect = d->effOverhang (e_w, left->contact);
  }
  else {
    Assert (prev, "Hmm");
    int prev_w = getwidth (previdx, prev);

    if (prev_w == e_w) {
      fet_type = 0;
      rect = f->getSpacing (e_w);
      if (left->contact) {
	rect = MAX (rect, d->viaSpaceMid());
      }
    }
    else if (prev_w < e_w) {
      /* upward notch */
      fet_type = 1;
      rect = d->getNotchSpacing();
      if (left->contact &&
	  (rect + d->effOverhang (e_w) < d->viaSpaceMid())) {
	rect = d->viaSpaceMid() - d->effOverhang (e_w);
      }
    }
    else {
      fet_type = -1;
      rect = d->effOverhang (e_w);
    }
  }

  Assert (rect > 0, "FIX FOR FINFETS!");

  dx += rect;

  if (fet_type != 0) {
    if (fet_type < 0) {
      /* down notch */
      rect = d->getNotchSpacing();
      if (left->contact &&
	  (rect + d->effOverhang (e_w) < d->viaSpaceMid())) {
	rect = d->viaSpaceMid() - d->effOverhang (e_w);
      }
    }
    else {
      /* up notch */
      rect = d->effOverhang (e_w);
    } 
    dx += rect;
  }

  return dx;
}


static void print_rectangle (FILE *fp, netlist_t *N,
			     const char *mat, node_t *name,
			     int llx, int lly, int wx, int wy)
{
  fprintf (fp, "rect ");
  if (name) {
    dump_node (fp, N, name);
  }
  else {
    fprintf (fp, "#");
  }
  int nllx, nlly, nurx, nury;

  if (wx < 0) {
    llx = llx + wx;
    wx = -wx;
  }
  if (wy < 0) {
    lly = lly + wy;
    wy = -wy;
  }
  fprintf (fp, " %s %d %d %d %d\n", mat, llx, lly, llx + wx, lly + wy);
}

static void update_bbox (BBox *cur, int type, int x, int y, int rx, int ry)
{
  if (type == EDGE_PFET) {				
    if (cur->p.llx >= cur->p.urx || cur->p.lly >= cur->p.ury) {	
      cur->p.llx = MIN(x,rx);				
      cur->p.lly = MIN(y,ry);				
      cur->p.urx = MAX(x,rx);				
      cur->p.ury = MAX(y,ry);				
    }							
    else {						
      cur->p.llx = MIN(cur->p.llx,x);			
      cur->p.llx = MIN(cur->p.llx,rx);			
      cur->p.lly = MIN(cur->p.lly,y);			
      cur->p.lly = MIN(cur->p.lly,ry);			
      cur->p.urx = MAX(cur->p.urx,x);			
      cur->p.urx = MAX(cur->p.urx,rx);			
      cur->p.ury = MAX(cur->p.ury,y);			
      cur->p.ury = MAX(cur->p.ury,ry);			
    }							
  } else {						
    if (cur->n.llx >= cur->n.urx || cur->n.lly >= cur->n.ury) {	
      cur->n.llx = MIN(x,rx);				
      cur->n.lly = MIN(y,ry);				
      cur->n.urx = MAX(x,rx);				
      cur->n.ury = MAX(y,ry);				
    }							
    else {						
      cur->n.llx = MIN(cur->n.llx,x);			
      cur->n.llx = MIN(cur->n.llx,rx);			
      cur->n.lly = MIN(cur->n.lly,y);			
      cur->n.lly = MIN(cur->n.lly,ry);			
      cur->n.urx = MAX(cur->n.urx,x);			
      cur->n.urx = MAX(cur->n.urx,rx);			
      cur->n.ury = MAX(cur->n.ury,y);			
      cur->n.ury = MAX(cur->n.ury,ry);			
    }							
  }							
}

/*
  emits rectangles needed upto the FET.
  If it is right edge, also emits the final diffusion of the right edge.
*/
static int emit_rectangle (FILE *fp,
			   netlist_t *N, Layout *L,
			   int pad,
			   int dx, int dy,
			   unsigned int flags,
			   edge_t *prev, int previdx,
			   node_t *left, edge_t *e, int eidx, int yup,
			   BBox *ret)
{
  DiffMat *d;
  FetMat *f;
  PolyMat *p;
  int rect;
  int fet_type; /* -1 = downward notch, +1 = upward notch, 0 = same
		   width */

  BBox b;

  int e_w = getwidth (eidx, e);
  
  if (ret) {
    b = *ret;
  }
  else {
    b.p.llx = 0;
    b.p.lly = 0;
    b.p.urx = 0;
    b.p.ury = 0;
    b.n = b.p;
  }

#define RECT_UPDATE(type,x,y,rx,ry)	update_bbox(&b,type,x,y,rx,ry)

#if 0
  printf ("emit rect: left=");
  dump_node (stdout, N, left);
  printf ("; gate=");
  dump_node (stdout, N, e->g);
  printf ("; right=");
  dump_node (stdout, N, right);
  printf ("\n");
#endif  

  /* XXX: THIS CODE GETS COPIED TO locate_fetedge!!!! */
  
  d = L->getDiff (e->type, e->flavor);
  f = L->getFet (e->type, e->flavor);
  p = L->getPoly ();
  b.flavor = e->flavor;

  int prev_w = 0;

  rect = 0;
  if (flags & EDGE_FLAGS_LEFT) {
    fet_type = 0;
    /* actual overhang rule */
    rect = d->effOverhang (e_w, left->contact);
  }
  else {
    Assert (prev, "Hmm");
    prev_w = getwidth (previdx, prev);
    
    if (prev_w == e_w) {
      fet_type = 0;
      rect = f->getSpacing (e_w);
      if (left->contact) {
	rect = MAX (rect, d->viaSpaceMid());
      }
    }
    else if (prev_w < e_w) {
      /* upward notch */
      fet_type = 1;
      rect = d->getNotchSpacing();
      if (left->contact &&
	  (rect + d->effOverhang (e_w) < d->viaSpaceMid())) {
	rect = d->viaSpaceMid() - d->effOverhang (e_w);
      }
    }
    else {
      fet_type = -1;
      rect = d->effOverhang (e_w);
    }
  }

  Assert (rect > 0, "FIX FOR FINFETS!");

  if (fet_type != -1) {
    rect += pad;
    pad = 0;
  }

  if (fet_type == 0) {
    print_rectangle (fp, N, d->getName(), left->contact ? left : NULL,
		     dx, dy, rect, yup*e_w);
    RECT_UPDATE(e->type, dx, dy, dx+rect, dy + yup*e_w);
  }
  else {
    print_rectangle (fp, N, d->getName(), left->contact ? left : NULL,
		     dx, dy, rect, yup*prev_w);
    RECT_UPDATE(e->type, dx,dy,dx+rect, dy + yup*prev_w);
  }
  dx += rect;

  if (fet_type != 0) {
    if (fet_type < 0) {
      /* down notch */
      rect = d->getNotchSpacing();
      if (left->contact &&
	  (rect + d->effOverhang (e_w) < d->viaSpaceMid())) {
	rect = d->viaSpaceMid() - d->effOverhang (e_w);
      }
    }
    else {
      /* up notch */
      rect = d->effOverhang (e_w);
    }
    rect += pad;
    pad = 0;
    print_rectangle (fp, N, d->getName(), NULL, dx, dy, rect, yup*e_w);
    RECT_UPDATE (e->type, dx,dy,dx+rect,dy+yup*e_w);
    dx += rect;
  }

  /* now print fet */
  print_rectangle (fp, N, f->getName(), NULL, dx, dy, e->l, yup*e_w);

  int poverhang = p->getOverhang (e->l);
  int uoverhang = poverhang;

  if (fet_type != 0) {
    uoverhang = MAX (uoverhang, p->getNotchOverhang (e->l));
  }

  /* now print poly edges */
  print_rectangle (fp, N, p->getName(), e->g,
		   dx, dy - yup*poverhang, e->l, yup*poverhang);

  print_rectangle (fp, N, p->getName(), NULL, dx, dy + yup*e_w,
		   e->l, yup*uoverhang);

  dx += e->l;
  
  if (flags & EDGE_FLAGS_RIGHT) {
    node_t *right;

    if (left == e->a) {
      right = e->b;
    }
    else {
      right = e->a;
    }
    rect = d->effOverhang (e_w, right->contact);
    print_rectangle (fp, N, d->getName(), right, dx, dy, rect, yup*e_w);
    RECT_UPDATE (e->type, dx,dy,dx+rect,dy+yup*e_w);
    dx += rect;
  }

  if (ret) {
    *ret = b;
  }

  fprintf (fp, "#----\n");

  return dx;
}

BBox print_dualstack (int dx, int dy,
		      FILE *fp, netlist_t *N, Layout *L, struct gate_pairs *gp)
{
  int flavor;
  int xpos, xpos_p;
  int diffspace;
  BBox b;
  
  if (gp->basepair) {
    flavor = gp->u.e.n->flavor;
  }
  else {
    struct gate_pairs *tmp;
    tmp = (struct gate_pairs *) list_value (list_first (gp->u.gp));
    if (tmp->u.e.n) {
      flavor = tmp->u.e.n->flavor;
    }
    else {
      Assert (tmp->u.e.p, "Hmm");
      flavor = tmp->u.e.p->flavor;
    }
  }

  DiffMat *ndiff = Technology::T->diff[EDGE_NFET][flavor];
  DiffMat *pdiff = Technology::T->diff[EDGE_PFET][flavor];

  diffspace = ndiff->getOppDiffSpacing (flavor);
  Assert (diffspace == pdiff->getOppDiffSpacing (flavor), "Hmm?!");

  FetMat *nfet = Technology::T->fet[EDGE_NFET][flavor];
  FetMat *pfet = Technology::T->fet[EDGE_PFET][flavor];

  PolyMat *poly = Technology::T->poly;

  /* ok, now we can draw! */
  Assert (nfet && pfet && poly && ndiff && pdiff, "What?");

  xpos = dx;
  xpos_p = dx;

  b.p.llx = dx;
  b.p.lly = dy;
  b.p.urx = dx;
  b.p.ury = dy;
  b.n = b.p;

  int padn, padp;
  int fposn, fposp;

  int yp = dy + diffspace/2;
  int yn = yp - diffspace;
  
  if (gp->basepair) {
    fposn = locate_fetedge (L, xpos, EDGE_FLAGS_LEFT|EDGE_FLAGS_RIGHT,
			    NULL, 0, gp->l.n, gp->u.e.n, gp->n_start);
    fposp = locate_fetedge (L, xpos, EDGE_FLAGS_LEFT|EDGE_FLAGS_RIGHT,
			    NULL, 0, gp->l.p, gp->u.e.p, gp->p_start);
    
    if (fposn > fposp) {
      padp = fposn - fposp;
      padn = 0;
    }
    else {
      padn = fposp - fposn;
      padp = 0;
    }

    xpos = emit_rectangle (fp, N, L, padn, xpos, yn,
			   EDGE_FLAGS_LEFT|EDGE_FLAGS_RIGHT,
			   NULL, 0,
			   gp->l.n, gp->u.e.n, gp->n_start, -1, &b);
    
    xpos_p = emit_rectangle (fp, N, L, padp, xpos_p, yp,
			     EDGE_FLAGS_LEFT|EDGE_FLAGS_RIGHT,
			     NULL, 0,
			     gp->l.p, gp->u.e.p, gp->p_start, 1, &b);
  }
  else {
    listitem_t *li;
    int firstp = 1, firstn = 1;
    edge_t *prevp = NULL, *prevn = NULL;
    int prevpidx = 0, prevnidx = 0;
    node_t *leftp, *leftn;

    leftp = NULL;
    leftn = NULL;

    for (li = list_first (gp->u.gp); li; li = list_next (li)) {
      struct gate_pairs *tmp;
      unsigned int flagsp = 0, flagsn = 0;
      tmp = (struct gate_pairs *) list_value (li);

      Assert (tmp->basepair, "Hmm");
      
      if (firstp && tmp->u.e.p) {
	flagsp |= EDGE_FLAGS_LEFT;
	firstp = 0;
      }
      if (firstn && tmp->u.e.n) {
	firstn = 0;
	flagsn |= EDGE_FLAGS_LEFT;
      }
      if (!list_next (li)) {
	flagsp |= EDGE_FLAGS_RIGHT;
	flagsn |= EDGE_FLAGS_RIGHT;
      }
      else {
	struct gate_pairs *tnext;
	tnext = (struct gate_pairs *) list_value (list_next (li));
	if (!tnext->u.e.p) {
	  flagsp |= EDGE_FLAGS_RIGHT;
	}
	if (!tnext->u.e.n) {
	  flagsn |= EDGE_FLAGS_RIGHT;
	}
      }

      if (tmp->u.e.n) {
	if (!leftn) {
	  leftn = gp->l.n;
	}
	else {
	  Assert (prevn, "Hmm");
	  if (prevn->a == leftn) {
	    leftn = prevn->b;
	  }
	  else {
	    Assert (prevn->b == leftn, "Hmm");
	    leftn = prevn->a;
	  }
	}
      }
      if (tmp->u.e.p) {
	if (!leftp) {
	  leftp  = gp->l.p;
	}
	else {
	  Assert (prevp, "Hmm");
	  if (prevp->a == leftp) {
	    leftp = prevp->b;
	  }
	  else {
	    Assert (prevp->b == leftp, "Hmm");
	    leftp = prevp->a;
	  }
	}
      }

      /* compute padding */
      padn = 0;
      padp = 0;
      if (tmp->u.e.n && tmp->u.e.p) {
	fposn = locate_fetedge (L, xpos, flagsn,
				prevn, prevnidx, leftn, tmp->u.e.n,
				tmp->n_start);
	fposp = locate_fetedge (L, xpos_p, flagsp,
				prevp, prevpidx, leftp, tmp->u.e.p,
				tmp->p_start);
	if (fposn > fposp) {
	  padp = padp + fposn - fposp;
	}
	else {
	  padn = padn + fposp - fposn;
	}
      }
      
      if (tmp->u.e.n) {
	xpos = emit_rectangle (fp, N, L, padn, xpos, yn, flagsn,
			       prevn, prevnidx, leftn, tmp->u.e.n,
			       tmp->n_start, -1, &b);
	prevn = tmp->u.e.n;
	prevnidx = tmp->n_start;
	if (!tmp->u.e.p) {
	  xpos_p = xpos;
	}
      }
      
      if (tmp->u.e.p) {
	xpos_p = emit_rectangle (fp, N, L, padp, xpos_p, yp, flagsp,
				 prevp, prevpidx, leftp, tmp->u.e.p,
				 tmp->p_start, 1, &b);

	prevp = tmp->u.e.p;
	prevpidx = tmp->p_start;
	if (!tmp->u.e.n) {
	  xpos = xpos_p;
	}
      }
      
    }
  }
  return b;
}

BBox print_singlestack (int dx, int dy,
			FILE *fp, netlist_t *N, Layout *L, list_t *l)
{
  int flavor;
  int type;
  node_t *n;
  edge_t *e;
  edge_t *prev;
  int xpos = dx;
  int ypos = dy;
  BBox b;
  int idx = 0;
  int previdx = 0;

  b.p.llx = 0;
  b.p.lly = 0;
  b.p.urx = 0;
  b.p.ury = 0;
  b.n = b.p;

  if (list_length (l) < 4) return b;

  n = (node_t *) list_value (list_first (l));
  e = (edge_t *) list_value (list_next (list_first (l)));
  idx = (long) list_value (list_next (list_next (list_first (l))));
  
  flavor = e->flavor;
  type = e->type;
  
  DiffMat *diff = Technology::T->diff[type][flavor];
  FetMat *fet = Technology::T->fet[type][flavor];
  PolyMat *poly = Technology::T->poly;

  /* ok, now we can draw! */
  Assert (fet && diff && poly, "What?");

  /* lets draw rectangles */
  listitem_t *li;

  prev = NULL;
  previdx = 0;
  li = list_first (l);
  while (li && list_next (li) && list_next (list_next (li))) {
    unsigned int flags = 0;
    node_t *m;

    n = (node_t *) list_value (li);
    e = (edge_t *) list_value (list_next (li));
    idx = (long) list_value (list_next (list_next (li)));
    m = (node_t *) list_value (list_next (list_next (list_next (li))));

    if (li == list_first (l)) {
      flags |= EDGE_FLAGS_LEFT;
    }
    if (!list_next (list_next (list_next (li)))) {
      flags |= EDGE_FLAGS_RIGHT;
    }
    xpos = emit_rectangle (fp, N, L, 0, xpos, ypos, flags, prev, previdx, 
			   n, e, idx, 1, &b);
    prev = e;
    previdx = idx;

    li = list_next (list_next (list_next (li)));
  }
  Assert (li && !list_next (li), "Eh?");
  n = (node_t *) list_value (li);
  return b;
}

static void dump_gateinfo (netlist_t *N, edge_t *e1, edge_t *e2)
{
  if (e1 && e2) {
    dump_node (stdout, N, e1->g);
    printf ("<%d,%d>", e1->w, e1->l);
    if (e2->g != e1->g) {
      printf (":");
      dump_node (stdout, N, e2->g);
      printf ("<%d,%d>", e2->w, e2->l);
    }
  }
  else if (e1) {
    dump_node (stdout, N, e1->g);
    printf ("<%d,%d>", e1->w, e1->l);
    printf (":");
  }
  else {
    Assert (e2, "Eh?");
    printf (":");
    dump_node (stdout, N, e2->g);
    printf ("<%d,%d>", e2->w, e2->l);
  }
}

static void dump_nodepair (netlist_t *N, struct node_pair *p)
{
  printf ("(");
  dump_node (stdout, N, p->n);
  printf (",");
  dump_node (stdout, N, p->p);
  printf (")");
}

static void dump_pair (netlist_t *N, struct gate_pairs *p)
{
  listitem_t *li;
  edge_t *e1, *e2;

  printf ("dual_stack [n=%d]: ", p->nodeshare);
  dump_nodepair (N, &p->l);
  printf (" ");
  dump_nodepair (N, &p->r);
  printf (" ");

  if (p->basepair) {
    e1 = p->u.e.n;
    e2 = p->u.e.p;
    printf (" e%d: ", p->share);
    dump_gateinfo (N, e1, e2);
    if (e1) { printf (" ns=%d", p->n_start); }
    if (e2) { printf (" ps=%d", p->p_start); }
    printf ("\n");
    return;
  }

  struct node_pair prev;
  
  prev = p->l;
  for (li = list_first (p->u.gp); li; li = list_next (li)) {
    struct gate_pairs *tmp;

    tmp = (struct gate_pairs *) list_value (li);
    Assert (tmp->basepair, "Hmm");
    e1 = tmp->u.e.n;
    e2 = tmp->u.e.p;

    printf ("\n\t e%d: ", tmp->share);

    dump_nodepair (N, &prev);
    printf ("[");

    dump_gateinfo (N, e1, e2);
    if (e1) { printf (" ns=%d", tmp->n_start); }
    if (e2) { printf (" ps=%d", tmp->p_start); }
    
    printf("] -> ");

    if (e1) {
      if (prev.n == e1->a) {
	prev.n = e1->b;
      }
      else {
	Assert (prev.n == e1->b, "Hmm");
	prev.n = e1->a;
      }
    }

    if (e2) {
      if (prev.p == e2->a) {
	prev.p = e2->b;
      }
      else {
	Assert (prev.p == e2->b, "Hmm");
	prev.p = e2->a;
      }
    }
    dump_nodepair (N, &prev);
  }
  printf("\n");
}



void geom_create_from_stack (Act *a, FILE *fplef, circuit_t *ckt,
			     netlist_t *N, list_t *stacks,
			     int *sizex, int *sizey)
{
  int i;
  listitem_t *li;
  Layout *L = new Layout(N);
  char buf[1024];
  FILE *fp;
  BBox b;
  int xpos = 0;
  BBox all;
  Process *p = N->bN->p;
  int flag;
  int len;
  int well_pad = 0;

  Assert (p, "Hmm");

  min_width = config_get_int ("net.min_width");
  fold_n_width = config_get_int ("net.fold_nfet_width");
  fold_p_width = config_get_int ("net.fold_pfet_width");

  /*--- process gp ---*/
  a->msnprintfproc (buf, 1024, p);
  sprintf (buf+strlen(buf), "_stks");
  
  fp = fopen (buf, "w");
  if (!fp) {
    fatal_error ("Could not open file `%s' for writing!", buf);
  }

  all.n.llx = 0;
  all.n.lly = 0;
  all.n.urx = 0;
  all.n.ury = 0;
  all.p.llx = 0;
  all.p.lly = 0;
  all.p.urx = 0;
  all.p.ury = 0;
  
  li = list_first (stacks);
  list_t *gplist = (list_t *) list_value (li);

  //printf ("\nproc: %s\n", proc_name);

  if (list_length (gplist) > 0) {
    listitem_t *si;

    for (si = list_first (gplist); si; si = list_next (si)) {
      struct gate_pairs *gp;
      gp = (struct gate_pairs *) list_value (si);

#if 0
      dump_pair (N, gp);
#endif

      /*--- process gp ---*/
      b = print_dualstack (xpos, 0, fp, N, L, gp);

#if 0      
      printf ("[xpos=%d] BBOX: p (%d,%d) -> (%d,%d); n (%d,%d) -> (%d,%d)\n", 
	      xpos,
	      b.p.llx, b.p.lly, b.p.urx, b.p.ury,
	      b.n.llx, b.n.lly, b.n.urx, b.n.ury);
      printf ("---\n");
#endif

      /*-- now print nwells and pwells --*/
      if (b.p.llx < b.p.urx && b.p.lly < b.p.ury) {
	WellMat *well = Technology::T->well[EDGE_PFET][b.flavor];
	if (well) {
	  print_rectangle (fp, N, well->getName(), N->nsc,
			   b.p.llx - well->getOverhang(),
			   b.p.lly - well->getOverhang(),
			   (b.p.urx - b.p.llx) + 2*well->getOverhang(),
			   (b.p.ury - b.p.lly) + 2*well->getOverhang());
	  well_pad = MAX(well_pad, well->getOverhang());
	}
	update_bbox (&all, EDGE_PFET, b.p.llx, b.p.lly, b.p.urx, b.p.ury);
      }
      if (b.n.llx < b.n.urx && b.n.lly < b.n.ury) {
	WellMat *well = Technology::T->well[EDGE_NFET][b.flavor];
	if (well) {
	  print_rectangle (fp, N, well->getName(), N->nsc,
			   b.n.llx - well->getOverhang(),
			   b.n.lly - well->getOverhang(),
			   (b.n.urx - b.n.llx) + 2*well->getOverhang(),
			   (b.n.ury - b.n.lly) + 2*well->getOverhang());
	  well_pad = MAX(well_pad, well->getOverhang());
	}
	update_bbox (&all, EDGE_NFET, b.n.llx, b.n.lly, b.n.urx, b.n.ury);
      }
      DiffMat *pd, *nd;
      pd = Technology::T->diff[EDGE_PFET][b.flavor];
      nd = Technology::T->diff[EDGE_NFET][b.flavor];
      
      xpos = MAX (b.n.urx, b.p.urx) + MAX (pd->getSpacing(b.flavor),
					   nd->getSpacing(b.flavor));
      well_pad = MAX(well_pad, pd->getSpacing (b.flavor));
      well_pad = MAX(well_pad, nd->getSpacing (b.flavor));
    }
  }

  int oxpos = xpos;
    
  li = list_next (li);
  list_t *nstk = (list_t *) list_value (li);
  if (nstk && list_length (nstk) > 0) {
    listitem_t *si;
    for (si = list_first (nstk); si; si = list_next (si)) {
      list_t *stack = (list_t *) list_value (si);

      b = print_singlestack (xpos, 0, fp, N, L, stack);

      if (b.n.llx < b.n.urx && b.n.lly < b.n.ury) {
	WellMat *well = Technology::T->well[EDGE_NFET][b.flavor];
	if (well) {
	  print_rectangle (fp, N, well->getName(), N->nsc,
			   b.n.llx - well->getOverhang(),
			   b.n.lly - well->getOverhang(),
			   (b.n.urx - b.n.llx) + 2*well->getOverhang(),
			   (b.n.ury - b.n.lly) + 2*well->getOverhang());
	  well_pad = MAX(well_pad, well->getOverhang());
	}
	update_bbox (&all, EDGE_NFET, b.n.llx, b.n.lly, b.n.urx, b.n.ury);
      }
      DiffMat *nd;
      nd = Technology::T->diff[EDGE_NFET][b.flavor];
      xpos = b.n.urx + nd->getSpacing (b.flavor);
      well_pad = MAX(well_pad, nd->getSpacing (b.flavor));
    }
  }

  xpos = oxpos;
  
  li = list_next (li);
  list_t *pstk = (list_t *) list_value (li);
  if (pstk && list_length (pstk) > 0) {
    listitem_t *si;
    for (si = list_first (pstk); si; si = list_next (si)) {
      list_t *stack = (list_t *) list_value (si);

      b = print_singlestack (xpos, 0, fp, N, L, stack);
      
      if (b.p.llx < b.p.urx && b.p.lly < b.p.ury) {
	WellMat *well = Technology::T->well[EDGE_PFET][b.flavor];
	if (well) {
	  print_rectangle (fp, N, well->getName(), N->nsc,
			   b.p.llx - well->getOverhang(),
			   b.p.lly - well->getOverhang(),
			   (b.p.urx - b.p.llx) + 2*well->getOverhang(),
			   (b.p.ury - b.p.lly) + 2*well->getOverhang());
	  well_pad = MAX(well_pad, well->getOverhang());
	}
	update_bbox (&all, EDGE_PFET, b.p.llx, b.p.lly, b.p.urx, b.p.ury);
      }
      DiffMat *pd;
      pd = Technology::T->diff[EDGE_PFET][b.flavor];
      xpos = b.p.urx + pd->getSpacing (b.flavor);
      well_pad = MAX(well_pad, pd->getSpacing (b.flavor));
    }
  }
  fclose (fp);

  /*--- process gp ---*/
  a->msnprintfproc (buf, 1024, p);
  fprintf (fplef, "MACRO %s\n", buf);

  /*-- boilerplate --*/
  fprintf (fplef, "    CLASS CORE ;\n");
  fprintf (fplef, "    FOREIGN %s %.6f %.6f ;\n", buf, 0.0, 0.0);
  fprintf (fplef, "    ORIGIN %.6f %.6f ;\n", 0.0, 0.0);

  update_bbox (&all, EDGE_PFET, all.n.llx, all.n.lly, all.n.urx, all.n.ury);

  /* align RHS of box to m2 pitch */
  int rhs = (all.p.urx - all.p.llx + 2*well_pad);
  int topedge = (all.p.ury - all.p.lly + 2*well_pad);

  RoutingMat *m1 = Technology::T->metal[0];
  RoutingMat *m2 = Technology::T->metal[1];
  
//topedge = MAX(topedge, MIN_TRACKS*m1->getPitch());

  if ((rhs % m2->getPitch()) != 0) {
    rhs = rhs + m2->getPitch() - (rhs % m2->getPitch());
  }

  if ((topedge % m1->getPitch()) != 0) {
    topedge = topedge + m2->getPitch() - (topedge % m2->getPitch());
  }

  double scale = Technology::T->scale/1000.0;

  fprintf (fplef, "    SIZE %.6f BY %.6f ;\n", rhs*scale, topedge*scale);

#ifdef INTEGRATED_PLACER  
  std::string cktblock(buf);
  if (ckt) {
     ckt->add_block_type (cktblock, rhs/m2->getPitch(), topedge/m1->getPitch());
  }
#endif  

  fprintf (fplef, "    SYMMETRY X Y ;\n");
  fprintf (fplef, "    SITE CoreSite ;\n");

  /*-- pins --*/
  int count = 0;
  int cout = 0;
  for (i=0; i < A_LEN (N->bN->ports); i++) {
    if (N->bN->ports[i].omit) continue;
    if (N->bN->ports[i].input) {
      count++;
    }
    else {
      cout++;
    }
  }

  if (((count * m2->getPitch()) > rhs) || ((cout * m2->getPitch()) > rhs)) {
    warning ("Can't fit ports!");
  }

  int stride = 1;
  int strideo = 1;

  while ((m2->getPitch() + count * stride * m2->getPitch()) <= rhs) {
    stride++;
  }
  stride--;

  while ((m2->getPitch() + cout * strideo * m2->getPitch()) <= rhs) {
    strideo++;
  }
  strideo--;

#if 0
  if (stride < 2 || strideo < 2) {
    warning("Tight ports");
  }
#endif
  
  count = m2->getPitch();
  cout = count;
  
  for (i=0; i < A_LEN (N->bN->ports); i++) {
    char tmp[1024];
    ActId *id;
    act_booleanized_var_t *v;
    ihash_bucket_t *b;
    if (N->bN->ports[i].omit) continue;
    id = N->bN->ports[i].c->toid();
    fprintf (fplef, "    PIN ");
    id->sPrint (tmp, 1024);
    a->mfprintf (fplef, "%s", tmp);

    char tmp2[1024];
    a->msnprintf (tmp2, 1024, "%s", tmp);
    std::string pinname(tmp2);
    
    fprintf (fplef, "\n");
    fprintf (fplef, "        DIRECTION %s ;\n",
	     N->bN->ports[i].input ? "INPUT" : "OUTPUT");
    fprintf (fplef, "        USE ");

    b = ihash_lookup (N->bN->cH, (long)N->bN->ports[i].c);
    if (b) {
      struct act_nl_varinfo *av;
      v = (act_booleanized_var_t *) b->v;
      av = (struct act_nl_varinfo *)v->extra;
      if (av) {
	if (av->n == N->Vdd) {
	  fprintf (fplef, "POWER ;\n");
	}
	else if (av->n == N->GND) {
	  fprintf (fplef, "GROUND ;\n");
	}
	else {
	  fprintf (fplef, "SIGNAL ;\n");
	}
      }
      else {
	fprintf (fplef, "SIGNAL ;\n");
      }
    }
    else {
      fprintf (fplef, "SIGNAL ;\n");
    }

    fprintf (fplef, "        PORT\n");
    fprintf (fplef, "        LAYER %s ;\n", m2->getName());
    if (N->bN->ports[i].input) {
      int w;
      w = m2->minWidth();
      fprintf (fplef, "        RECT %.6f %.6f %.6f %.6f ;\n",
	       scale*count,
	       scale*(topedge - w),
	       scale*(count + w),
	       scale*topedge);

      /*-- pin location should be upper left corner --*/
#ifdef INTEGRATED_PLACER      
      if (ckt)
      ckt->add_pin_to_block (cktblock, pinname, count/m2->getPitch(),
			     topedge/m1->getPitch());
#endif      
      
      count += m2->getPitch()*stride;
    }
    else {
      int w;
      w = m2->minWidth();
      fprintf (fplef, "        RECT %.6f %.6f %.6f %.6f ;\n",
	       scale*cout, scale*m1->getPitch(),
	       scale*(cout + w),
	       scale*(w + m1->getPitch()));

#ifdef INTEGRATED_PLACER
      /*-- pin location should be ll corner --*/
      if (ckt)
      ckt->add_pin_to_block (cktblock, pinname, cout/m2->getPitch(), 1);
#endif      
      
      cout += m2->getPitch()*strideo;
    }
    fprintf (fplef, "        END\n");
    fprintf (fplef, "    END ");
    a->mfprintf (fplef, "%s", tmp);
    fprintf (fplef, "\n");
    delete id;
  }

  if (topedge > 6*m1->getPitch()) {
    fprintf (fplef, "    OBS\n");
    fprintf (fplef, "      LAYER %s ;\n", m1->getName());
    fprintf (fplef, "         RECT %.6f %.6f %.6f %.6f ;\n",
	     scale*m2->getPitch(), scale*(3*m1->getPitch()),
	     scale*(rhs - m2->getPitch()), scale*(topedge - 3*m1->getPitch()));
    fprintf (fplef, "    END\n");
  }
  
  fprintf (fplef, "END %s\n", buf);
  if (sizex) {
    *sizex = rhs;
  }
  if (sizey) {
    *sizey = topedge;
  }
  return;
}

