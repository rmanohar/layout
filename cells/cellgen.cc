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
    xpos = emit_rectangle (fp, N, L, 0, xpos, ypos, flags, prev, n, e, 1, &b);
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
    printf ("<%d,%d>", e1->w, e1->l);
    if (e2->g != e1->g) {
      printf (":");
      printf ("<%d,%d>", e2->w, e2->l);
      dump_node (stdout, N, e2->g);
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
    printf ("<%d,%d>", e2->w, e2->l);
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



void geom_create_from_stack (Act *a, FILE *fplef,
			     netlist_t *N, list_t *stacks,
			     int *sizex, int *sizey)
{
  int i;
  listitem_t *li;
  Layout *L = new Layout();
  char buf[1024];
  FILE *fp;
  BBox b;
  int xpos = 0;
  BBox all;
  Process *p = N->bN->p;
  const char *proc_name;
  int flag;
  int len;
  int well_pad = 0;

  Assert (p, "Hmm");

  proc_name = p->getName();
  len = strlen (proc_name);
  if (len > 2 && proc_name[len-1] == '>' && proc_name[len-2] == '<') {
    flag = 1;
  }
  else {
    flag = 0;
  }
  
  /*--- process gp ---*/
  if (flag) {
    sprintf (buf, "%s", proc_name);
    sprintf (buf+len-2, "_stks");
  }
  else {
    a->msnprintf (buf, 1024, "%s", proc_name);
    sprintf (buf+strlen(buf), "_stks");
  }
  
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

      //dump_pair (N, gp);

      /*--- process gp ---*/
      b = print_dualstack (xpos, 0, fp, N, L, gp);

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
  if (p->getns() && p->getns() != ActNamespace::Global()) {
    char *s = p->getns()->Name();
    a->msnprintf (buf, 1024, "%s::", s);
    FREE (s);
  }
  else {
    buf[0] = '\0';
  }

  len = strlen (buf);
  if (flag) {
    i = 0;
    while (proc_name[i] != '<') {
      buf[len++] = proc_name[i++];
    }
    buf[len] = '\0';
  }
  else {
    a->msnprintf (buf+len, 1024-len, "%s", proc_name);
  }
  
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
  
  if ((rhs % m2->getPitch()) != 0) {
    rhs = rhs + m2->getPitch() - (rhs % m2->getPitch());
  }

  if ((topedge % m1->getPitch()) != 0) {
    topedge = topedge + m2->getPitch() - (topedge % m2->getPitch());
  }

  double scale = Technology::T->scale/1000.0;

  fprintf (fplef, "    SIZE %.6f BY %.6f ;\n", rhs*scale, topedge*scale);

  fprintf (fplef, "    SYMMETRY X Y ;\n");
  fprintf (fplef, "    SITE CoreSite ;\n");

  /*-- pins --*/
  int count = 0; 
  for (i=0; i < A_LEN (N->bN->ports); i++) {
    if (N->bN->ports[i].omit) continue;
    count++;
  }

  if (count * m2->getPitch() > rhs) {
    warning ("Can't fit ports!");
  }

  int stride = 1;

  while ((m2->getPitch() + count * stride * m2->getPitch()) <= rhs) {
    stride++;
  }
  stride--;

  count = m2->getPitch();
  
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
    fprintf (fplef, "\n");
    fprintf (fplef, "        DIRECTION %s ;\n",
	     N->bN->ports[i].input ? "INPUT" : "OUTPUT");
    fprintf (fplef, "        USE ");

    b = ihash_lookup (N->bN->cH, (long)N->bN->ports[i].c);
    if (b) {
      struct act_varinfo *av;
      v = (act_booleanized_var_t *) b->v;
      av = (struct act_varinfo *)v->extra;
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
    fprintf (fplef, "        RECT %.6f %.6f %.6f %.6f ;\n",
	     scale*count,
	     scale*(topedge - m2->minWidth()),
	     scale*(count + m2->minWidth()),
	     scale*topedge);
    fprintf (fplef, "        END\n");
    fprintf (fplef, "    END ");
    a->mfprintf (fplef, "%s", tmp);
    fprintf (fplef, "\n");
    delete id;
    
    count += m2->getPitch()*stride;
  }

  if (topedge > 2*m1->getPitch()) {
    fprintf (fplef, "    OBS\n");
    fprintf (fplef, "      LAYER %s ;\n", m1->getName());
    fprintf (fplef, "         RECT %.6f %.6f %.6f %.6f ;\n",
	     0.0, scale*(m1->getPitch()),
	     scale*rhs, scale*(topedge - 2*m1->getPitch()));
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
