#include <stdio.h>
#include <string.h>
#include <list.h>
#include <act/passes/netlist.h>
#include <act/layout/geom.h>
#include <act/layout/tech.h>
#include "stacks.h"

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

/*
  emits rectangles needed upto the FET.
  If it is right edge, also emits the final diffusion of the right edge.
*/
static int locate_fetedge (Layout *L, int dx,
			   unsigned int flags,
			   edge_t *prev,
			   node_t *left, edge_t *e)
{
  DiffMat *d;
  FetMat *f;
  PolyMat *p;
  int rect;
  int fet_type; /* -1 = downward notch, +1 = upward notch, 0 = same
		   width */

  /* XXX: THIS CODE IS COPIED FROM emit_rectangle!!!!! */

  d = L->getDiff (e->type, e->flavor);
  f = L->getFet (e->type, e->flavor);
  p = L->getPoly ();

  rect = 0;
  if (flags & EDGE_FLAGS_LEFT) {
    fet_type = 0;
    /* actual overhang rule */
    rect = d->effOverhang (e->w, left->contact);
  }
  else {
    Assert (prev, "Hmm");

    if (prev->w == e->w) {
      fet_type = 0;
      rect = f->getSpacing (e->w);
      if (left->contact) {
	rect = MAX (rect, d->viaSpaceMid());
      }
    }
    else if (prev->w < e->w) {
      /* upward notch */
      fet_type = 1;
      rect = d->getNotchSpacing();
      if (left->contact &&
	  (rect + d->effOverhang (e->w) < d->viaSpaceMid())) {
	rect = d->viaSpaceMid() - d->effOverhang (e->w);
      }
    }
    else {
      fet_type = -1;
      rect = d->effOverhang (e->w);
    }
  }

  Assert (rect > 0, "FIX FOR FINFETS!");

  dx += rect;

  if (fet_type != 0) {
    if (fet_type < 0) {
      /* down notch */
      rect = d->getNotchSpacing();
      if (left->contact &&
	  (rect + d->effOverhang (e->w) < d->viaSpaceMid())) {
	rect = d->viaSpaceMid() - d->effOverhang (e->w);
      }
    }
    else {
      /* up notch */
      rect = d->effOverhang (e->w);
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
  fprintf (fp, " %s %d %d %d %d\n", mat, llx, lly, llx + wx, lly + wy);
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
			   edge_t *prev,
			   node_t *left, edge_t *e, int yup,
			   BBox *ret)
{
  DiffMat *d;
  FetMat *f;
  PolyMat *p;
  int rect;
  int fet_type; /* -1 = downward notch, +1 = upward notch, 0 = same
		   width */

  BBox b;

  if (ret) {
    b = *ret;
  }
  else {
    b.p.llx = dx;
    b.p.lly = 0;
    b.p.urx = dx;
    b.p.ury = 0;
    b.n = b.p;
  }

#define RECT_UPDATE(type,x,y,rx,ry)			\
  do {							\
    if (type == EDGE_PFET) {				\
      if (b.p.llx >= b.p.urx || b.p.lly >= b.p.ury) {	\
	b.p.llx = MIN(x,rx);				\
	b.p.lly = MIN(y,ry);				\
	b.p.urx = MAX(x,rx);				\
	b.p.ury = MAX(y,ry);				\
      }							\
      else {						\
	b.p.llx = MIN(b.p.llx,x);			\
	b.p.llx = MIN(b.p.llx,rx);			\
	b.p.lly = MIN(b.p.lly,y);			\
	b.p.lly = MIN(b.p.lly,ry);			\
	b.p.urx = MAX(b.p.urx,x);			\
	b.p.urx = MAX(b.p.urx,rx);			\
	b.p.ury = MAX(b.p.ury,y);			\
	b.p.ury = MAX(b.p.ury,ry);			\
      }							\
    } else {						\
      if (b.n.llx >= b.n.urx || b.n.lly >= b.n.ury) {	\
	b.n.llx = MIN(x,rx);				\
	b.n.lly = MIN(y,ry);				\
	b.n.urx = MAX(x,rx);				\
	b.n.ury = MAX(y,ry);				\
      }							\
      else {						\
	b.n.llx = MIN(b.n.llx,x);			\
	b.n.llx = MIN(b.n.llx,rx);			\
	b.n.lly = MIN(b.n.lly,y);			\
	b.n.lly = MIN(b.n.lly,ry);			\
	b.n.urx = MAX(b.n.urx,x);			\
	b.n.urx = MAX(b.n.urx,rx);			\
	b.n.ury = MAX(b.n.ury,y);			\
	b.n.ury = MAX(b.n.ury,ry);			\
      }							\
    }							\
  } while (0)
  

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

  rect = 0;
  if (flags & EDGE_FLAGS_LEFT) {
    fet_type = 0;
    /* actual overhang rule */
    rect = d->effOverhang (e->w, left->contact);
  }
  else {
    Assert (prev, "Hmm");

    if (prev->w == e->w) {
      fet_type = 0;
      rect = f->getSpacing (e->w);
      if (left->contact) {
	rect = MAX (rect, d->viaSpaceMid());
      }
    }
    else if (prev->w < e->w) {
      /* upward notch */
      fet_type = 1;
      rect = d->getNotchSpacing();
      if (left->contact &&
	  (rect + d->effOverhang (e->w) < d->viaSpaceMid())) {
	rect = d->viaSpaceMid() - d->effOverhang (e->w);
      }
    }
    else {
      fet_type = -1;
      rect = d->effOverhang (e->w);
    }
  }

  Assert (rect > 0, "FIX FOR FINFETS!");

  if (fet_type != -1) {
    rect += pad;
    pad = 0;
  }

  if (fet_type == 0) {
    print_rectangle (fp, N, d->getName(), left->contact ? left : NULL,
		     dx, dy, rect, yup*e->w);
    RECT_UPDATE(e->type, dx, dy, dx+rect, dy + yup*e->w);
  }
  else {
    print_rectangle (fp, N, d->getName(), left->contact ? left : NULL,
		     dx, dy, rect, yup*prev->w);
    RECT_UPDATE(e->type, dx,dy,dx+rect, dy + yup*prev->w);
  }
  dx += rect;

  if (fet_type != 0) {
    if (fet_type < 0) {
      /* down notch */
      rect = d->getNotchSpacing();
      if (left->contact &&
	  (rect + d->effOverhang (e->w) < d->viaSpaceMid())) {
	rect = d->viaSpaceMid() - d->effOverhang (e->w);
      }
    }
    else {
      /* up notch */
      rect = d->effOverhang (e->w);
    }
    rect += pad;
    pad = 0;
    print_rectangle (fp, N, d->getName(), NULL, dx, dy, rect, yup*e->w);
    RECT_UPDATE (e->type, dx,dy,dx+rect,dy+yup*e->w);
    dx += rect;
  }

  /* now print fet */
  print_rectangle (fp, N, f->getName(), NULL, dx, dy, e->l, yup*e->w);

  int poverhang = p->getOverhang (e->l);
  int uoverhang = poverhang;

  if (fet_type != 0) {
    uoverhang = MAX (uoverhang, p->getNotchOverhang (e->l));
  }

  /* now print poly edges */
  print_rectangle (fp, N, p->getName(), e->g,
		   dx, dy - yup*poverhang, e->l, yup*poverhang);

  print_rectangle (fp, N, p->getName(), NULL, dx, dy + yup*e->w,
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
    rect = d->effOverhang (e->w, right->contact);
    print_rectangle (fp, N, d->getName(), right, dx, dy, rect, yup*e->w);
    RECT_UPDATE (e->type, dx,dy,dx+rect,dy+yup*e->w);
    dx += rect;
  }

  if (ret) {
    *ret = b;
  }

  return dx;
}

BBox print_dualstack (FILE *fp, netlist_t *N, Layout *L, struct gate_pairs *gp)
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
    flavor = tmp->u.e.n->flavor;
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

  xpos = 0;
  xpos_p = 0;

  b.p.llx = 0;
  b.p.lly = 0;
  b.p.urx = 0;
  b.p.ury = 0;
  b.n = b.p;

  int padn, padp;
  int fposn, fposp;

  int yp = diffspace/2;
  int yn = yp - diffspace;
  
  if (gp->basepair) {
    fposn = locate_fetedge (L, xpos, EDGE_FLAGS_LEFT|EDGE_FLAGS_RIGHT,
			    NULL, gp->l.n, gp->u.e.n);
    fposp = locate_fetedge (L, xpos, EDGE_FLAGS_LEFT|EDGE_FLAGS_RIGHT,
			    NULL, gp->l.p, gp->u.e.p);
    
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
			   NULL,
			   gp->l.n, gp->u.e.n, -1, &b);
    
    xpos_p = emit_rectangle (fp, N, L, padp, xpos_p, yp,
			     EDGE_FLAGS_LEFT|EDGE_FLAGS_RIGHT,
			     NULL,
			     gp->l.p, gp->u.e.p, 1, &b);
  }
  else {
    listitem_t *li;
    int firstp = 1, firstn = 1;
    edge_t *prevp = NULL, *prevn = NULL;
    node_t *leftp, *leftn;

    xpos = 0;
    xpos_p = 0;
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
				prevn, leftn, tmp->u.e.n);
	fposp = locate_fetedge (L, xpos_p, flagsp,
				prevp, leftp, tmp->u.e.p);
	if (fposn > fposp) {
	  padp = padp + fposn - fposp;
	}
	else {
	  padn = padn + fposp - fposn;
	}
      }
      
      if (tmp->u.e.n) {
	xpos = emit_rectangle (fp, N, L, padn, xpos, yn, flagsn,
			       prevn, leftn, tmp->u.e.n, -1, &b);
	prevn = tmp->u.e.n;
	if (!tmp->u.e.p) {
	  xpos_p = xpos;
	}
      }
      
      if (tmp->u.e.p) {
	xpos_p = emit_rectangle (fp, N, L, padp, xpos_p, yp, flagsp,
				 prevp, leftp, tmp->u.e.p, 1, &b);

	prevp = tmp->u.e.p;
	if (!tmp->u.e.n) {
	  xpos = xpos_p;
	}
      }
      
    }
  }
  return b;
}

BBox print_singlestack (FILE *fp, netlist_t *N, Layout *L, list_t *l)
{
  int flavor;
  int type;
  node_t *n;
  edge_t *e;
  edge_t *prev;
  int xpos = 0;
  BBox b;

  b.p.llx = 0;
  b.p.lly = 0;
  b.p.urx = 0;
  b.p.ury = 0;
  b.n = b.p;

  if (list_length (l) < 3) return b;

  n = (node_t *) list_value (list_first (l));
  e = (edge_t *) list_value (list_next (list_first (l)));

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
  li = list_first (l);
  while (li && list_next (li)) {
    unsigned int flags = 0;
    node_t *m;

    n = (node_t *) list_value (li);
    e = (edge_t *) list_value (list_next (li));
    m = (node_t *) list_value (list_next (list_next (li)));

    if (li == list_first (l)) {
      flags |= EDGE_FLAGS_LEFT;
    }
    if (!list_next (list_next (list_next (li)))) {
      flags |= EDGE_FLAGS_RIGHT;
    }
    xpos = emit_rectangle (fp, N, L, 0, xpos, 0, flags, prev, n, e, 1, &b);
    prev = e;

    li = list_next (list_next (li));
  }
  Assert (li && !list_next (li), "Eh?");
  n = (node_t *) list_value (li);
  return b;
}

static void dump_gateinfo (netlist_t *N, edge_t *e1, edge_t *e2)
{
  if (e1 && e2) {
    dump_node (stdout, N, e1->g);
    if (e2->g != e1->g) {
      printf (":");
      dump_node (stdout, N, e2->g);
    }
  }
  else if (e1) {
    dump_node (stdout, N, e1->g);
    printf (":");
  }
  else {
    Assert (e2, "Eh?");
    printf (":");
    dump_node (stdout, N, e2->g);
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
    printf (" e: ");
    dump_gateinfo (N, e1, e2);
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

    printf ("\n\t e: ");

    dump_nodepair (N, &prev);
    printf ("[");

    dump_gateinfo (N, e1, e2);
    
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



void geom_create_from_stack (netlist_t *N, list_t *stacks, Process *p)
{
  listitem_t *li;
  Layout *L = new Layout();
  char buf[1024];
  int num = 0;
  FILE *fp;
  BBox b;
  
  li = list_first (stacks);
  list_t *gplist = (list_t *) list_value (li);
  if (list_length (gplist) > 0) {
    listitem_t *si;
    int x = 0;
    for (si = list_first (gplist); si; si = list_next (si)) {
      struct gate_pairs *gp;
      gp = (struct gate_pairs *) list_value (si);
      /*--- process gp ---*/
      sprintf (buf, "%s", p->getName());
      if (buf[strlen (buf)-1] == '>') {
	sprintf (buf+strlen (buf)-2, "_stk_d%d", num++);
      }
      else {
	sprintf (buf+strlen (buf), "_stk_d%d", num++);
      }
      fp = fopen (buf, "w");
      b = print_dualstack (fp, N, L, gp);

      /*-- now print nwells and pwells --*/
      if (b.p.llx < b.p.urx && b.p.lly < b.p.ury) {
	WellMat *well = Technology::T->well[EDGE_PFET][b.flavor];
	if (well) {
	  print_rectangle (fp, N, well->getName(), N->nsc,
			   b.p.llx - well->getOverhang(),
			   b.p.lly - well->getOverhang(),
			   (b.p.urx - b.p.llx) + 2*well->getOverhang(),
			   (b.p.ury - b.p.lly) + 2*well->getOverhang());
	}
      }
      if (b.n.llx < b.n.urx && b.n.lly < b.n.ury) {
	WellMat *well = Technology::T->well[EDGE_NFET][b.flavor];
	if (well) {
	  print_rectangle (fp, N, well->getName(), N->nsc,
			   b.n.llx - well->getOverhang(),
			   b.n.lly - well->getOverhang(),
			   (b.n.urx - b.n.llx) + 2*well->getOverhang(),
			   (b.n.ury - b.n.lly) + 2*well->getOverhang());
	}
      }
      fclose (fp);
#if 0
      dump_pair (N, gp);
#endif
    }
  }

  li = list_next (li);
  list_t *nstk = (list_t *) list_value (li);
  if (nstk && list_length (nstk) > 0) {
    listitem_t *si;
    for (si = list_first (nstk); si; si = list_next (si)) {
      list_t *stack = (list_t *) list_value (si);

      sprintf (buf, "%s", p->getName());
      if (buf[strlen (buf)-1] == '>') {
	sprintf (buf+strlen (buf)-2, "_stk_n%d", num++);
      }
      else {
	sprintf (buf+strlen (buf), "_stk_n%d", num++);
      }
      fp = fopen (buf, "w");
      b = print_singlestack (fp, N, L, stack);

      if (b.n.llx < b.n.urx && b.n.lly < b.n.ury) {
	WellMat *well = Technology::T->well[EDGE_NFET][b.flavor];
	if (well) {
	  print_rectangle (fp, N, well->getName(), N->nsc,
			   b.n.llx - well->getOverhang(),
			   b.n.lly - well->getOverhang(),
			   (b.n.urx - b.n.llx) + 2*well->getOverhang(),
			   (b.n.ury - b.n.lly) + 2*well->getOverhang());
	}
      }

      fclose (fp);
    }
  }

  li = list_next (li);
  list_t *pstk = (list_t *) list_value (li);
  if (pstk && list_length (pstk) > 0) {
    listitem_t *si;
    for (si = list_first (pstk); si; si = list_next (si)) {
      list_t *stack = (list_t *) list_value (si);

      sprintf (buf, "%s", p->getName());
      if (buf[strlen (buf)-1] == '>') {
	sprintf (buf+strlen (buf)-2, "_stk_p%d", num++);
      }
      else {
	sprintf (buf+strlen (buf), "_stk_p%d", num++);
      }
      fp = fopen (buf, "w");
      b = print_singlestack (fp, N, L, stack);
      if (b.p.llx < b.p.urx && b.p.lly < b.p.ury) {
	WellMat *well = Technology::T->well[EDGE_PFET][b.flavor];
	if (well) {
	  print_rectangle (fp, N, well->getName(), N->nsc,
			   b.p.llx - well->getOverhang(),
			   b.p.lly - well->getOverhang(),
			   (b.p.urx - b.p.llx) + 2*well->getOverhang(),
			   (b.p.ury - b.p.lly) + 2*well->getOverhang());
	}
      }
      fclose (fp);
    }
  }
}
