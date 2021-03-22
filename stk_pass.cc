/*************************************************************************
 *
 *  Copyright (c) 2021 Rajit Manohar
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
#include <heap.h>
#include "stk_pass.h"
#include "config.h"

#ifndef MIN
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif

#define WEIGHT_SHARING 8

#define COST(p)  ((p)->share*WEIGHT_SHARING + (p)->nodeshare)


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

int node_pair::endpoint (netlist_t *N)
{
  int cost = 0;

  if (n == p) {
    cost++;
  }
  if (n == N->GND) {
    cost += 2;
  }
  if (p == N->Vdd) {
    cost += 2;
  }
  return cost;
}

int gate_pairs::available_basepair ()
{
  Assert (basepair, "Hmm");
  if (u.e.n->visited + share <= u.e.n->nfolds &&
      u.e.p->visited + share <= u.e.p->nfolds) {
    return 1;
  }
  else {
    return 0;
  }
}

int gate_pairs::available()
{
  if (basepair) { return available_basepair(); }

  listitem_t *li;
  for (li = list_first (u.gp); li; li = list_next (li)) {
    struct gate_pairs *tmp;
    tmp = (struct gate_pairs *) list_value (li);
    if (!tmp->available_basepair ()) return 0;
  }
  return 1;
}

int gate_pairs::available_mark()
{
  if (basepair) {
    if (available_basepair()) {
      n_start = u.e.n->visited;
      p_start = u.e.p->visited;
      u.e.n->visited += share;
      u.e.p->visited += share;
      visited = 1;
      return 1;
    }
    return 0;
  }
  else {
    if (available ()) {
      listitem_t *li;
      for (li = list_first (u.gp); li; li = list_next (li)) {
	struct gate_pairs *tmp;
	tmp = (struct gate_pairs *) list_value (li);
	Assert (tmp->available_mark(), "Hmm");
      }
      return 1;
    }
    else {
      return 0;
    }
  }
}

/*
  Compute the # of pfets and # of nfets attached to a node
  Assumes that both pcnt, ncnt are non-NULL pointers
*/
static void node_degree (node_t *n, int *pcnt, int *ncnt)
{
  listitem_t *mi;
  edge_t *e;

  *ncnt = 0;
  *pcnt = 0;
    
  /* now go through edges: visited ones are always at the end of
     the list */
  for (mi = list_first (n->e); mi; mi = list_next (mi)) {
    e = (edge_t *) list_value (mi);
    if (e->visited == e->nfolds) break;	/* no longer used; we've visited this
					   one already and it is part of a stack */
    if (e->type == EDGE_PFET) {
      *pcnt += e->nfolds;
    }
    else {
      *ncnt += e->nfolds;
    }
  }
}


/*
 * Search for gate pair to see if it is already in the heap
 */
static int find_pairs (Heap *h, struct gate_pairs *p)
{
  struct gate_pairs *x;

  for (int i=0; i < h->sz; i++) {
    x = (struct gate_pairs *) h->value[i];
    if (x->share == p->share && x->nodeshare == p->nodeshare &&
	x->basepair == p->basepair) {
      listitem_t *li, *mi;
      if (x->l == p->l) {
	if (x->basepair) {
	  if (x->u.e.n == p->u.e.n && x->u.e.p == p->u.e.p)
	    return 1;
	}
	else {
	  for (li = list_first (x->u.gp), mi = list_first (p->u.gp); 
	       li && mi; li = list_next (li), mi = list_next (mi)) {
	    if (list_value (li) != list_value (mi))  {
	      break;
	    }
	  }
	  if (!li || !mi) return 1;
	}
      }
      else if (x->l == p->r) {
	if (x->basepair) {
	  if (x->u.e.n == p->u.e.n && x->u.e.p == p->u.e.p)
	    return 1;
	}
	else {
	  list_t *rp;
	  rp = list_dup (p->u.gp);
	  list_reverse (rp);
	  for (li = list_first (x->u.gp), mi = list_first (p->u.gp); 
	       li && mi; li = list_next (li), mi = list_next (mi)) {
	    if (list_value (li) != list_value (mi))  {
	      break;
	    }
	  }
	  if (!li || !mi) return 1;
	}
      }
    }
  }
  return 0;
}


/*
 * release storage for gate pair
 */
static void delete_pair (struct gate_pairs *p)
{
  if (!p->basepair)  {
    list_free (p->u.gp);
  }
  FREE (p);
}

static int available_edge (edge_t *e)
{
  return (e->nfolds - e->visited);
}


/* dir = 0, add to front; dir = 1, add to end */
static void extend_gatepair (netlist_t *N, struct gate_pairs **gpp,
			     int dir)
{
  int tryn, tryp;
  edge_t *en, *ep;
  edge_t *sn, *sp;
  listitem_t *li;
  node_t *no;
  struct node_pair other;
  int update;
  struct gate_pairs *gp;
  tryn = 1;
  tryp = 1;

  gp = *gpp;

  while (tryn || tryp) {
    other.n = NULL;
    other.p = NULL;
    sn = NULL;
    sp = NULL;
    if (tryn) {
      node_t *n = (dir ? gp->r.n : gp->l.n);
      no = NULL;
      update = 0;
      for (li = list_first (n->e); li && !no; li = list_next (li)) {
	en = (edge_t *) list_value (li);
	if (en->type != EDGE_NFET) continue;
	if (en->visited == en->nfolds) continue;
	/* candidate edge! */
	if (en->a == n) {
	  sn = en;
	  other.n = en->b;
	  update = 1;
	}
	else if (en->b == n) {
	  sn = en;
	  other.n = en->a;
	  update = 1;
	}
	else {
	  update = 0;
	}
	if (update) {
	  listitem_t *mi;
	  for (mi = list_first (other.n->e); !no && mi; mi = list_next (mi)) {
	    edge_t *tmp;
	    tmp = (edge_t *) list_value (mi);
	    if (tmp->type != EDGE_NFET) continue;
	    if (tmp->visited == tmp->nfolds) continue;
	    if (tmp == en) continue;
	    /* there is another edge! */
	    no = other.n;
	  }
	}
      }
    }
    if (tryp) {
      node_t *n = (dir ? gp->r.p : gp->l.p);
      no = NULL;
      update = 0;
      for (li = list_first (n->e); li && !no; li = list_next (li)) {
	ep = (edge_t *) list_value (li);
	if (ep->type != EDGE_PFET) continue;
	if (ep->visited == ep->nfolds) continue;
	/* candidate edge! */
	if (ep->a == n) {
	  sp = ep;
	  other.p = ep->b;
	  update = 1;
	}
	else if (ep->b == n) {
	  sp = ep;
	  other.p = ep->a;
	  update = 1;
	}
	else {
	  update = 0;
	}
	if (update) {
	  listitem_t *mi;
	  for (mi = list_first (other.p->e); !no && mi; mi = list_next (mi)) {
	    edge_t *tmp;
	    tmp = (edge_t *) list_value (mi);
	    if (tmp->type != EDGE_PFET) continue;
	    if (tmp->visited == tmp->nfolds) continue;
	    if (tmp == ep) continue;
	    /* there is another edge! */
	    no = other.p;
	  }
	}
      }
    }
    if (!other.n && !other.p) return;
    if (!other.n) tryn = 0;
    if (!other.p) tryp = 0;

    struct gate_pairs *tmp;
    NEW (tmp, struct gate_pairs);
    tmp->basepair = 1;
    tmp->visited = 1;
    tmp->u.e.n = sn;
    tmp->u.e.p = sp;
    tmp->l = gp->l;
    tmp->r = gp->r;
    tmp->share = 1; // not really share 

    if (other.n) {
      if (dir) {
	tmp->l.n = gp->r.n;
	gp->r.n = other.n;
	tmp->r.n = gp->r.n;
      }
      else {
	tmp->r.n = gp->l.n;
	gp->l.n = other.n;
	tmp->l.n = gp->l.n;
      }
      tmp->n_start = sn->visited;
      sn->visited++;
    }
    if (other.p) {
      if (dir) {
	tmp->l.p = gp->r.p;
	gp->r.p = other.p;
	tmp->r.p = gp->r.p;
      }
      else {
	tmp->r.p = gp->l.p;
	gp->l.p = other.p;
	tmp->l.p = gp->l.p;
      }
      tmp->p_start = sp->visited;
      sp->visited++;
    }


    if (gp->basepair) {
      struct gate_pairs *t2;
      NEW (t2, struct gate_pairs);
      *t2 = *gp;
      t2->u.gp = list_new ();
      t2->basepair = 0;
      t2->visited = 0;
      list_append (t2->u.gp, gp);
      *gpp = t2;
      gp = t2;
    }
    
    if (dir) {
      list_append (gp->u.gp, tmp);
    }
    else {
      list_append_head (gp->u.gp, tmp);
    }
  }
  return;
}

static node_t *pick_node_odd_degree (list_t *lst, int type)
{
  edge_t *e;
  node_t *n;
  listitem_t *li, *mi;
  listitem_t *prev;
  int deg;

  prev = NULL;
  li = list_first (lst);
  while (li) {
    n = (node_t *) list_value (li);
    deg = 0;
    for (mi = list_first (n->e); mi; mi = list_next (mi)) {
      e = (edge_t *) list_value (mi);
      if (e->type != type) continue;
      if (!available_edge (e)) continue;
      deg++;
    }
    if (deg == 0) {
      /* remove node from the list */
      li = list_next (li);
      list_delete_next (lst, prev);
    }
    else if (deg & 1) {
      return n;
    }
    else {
      prev = li;
      li = list_next (li);
    }
  }
  /* none left, just take one that is even. doesn't matter */
  li = list_first (lst);
  if (li) {
    return (node_t *) list_value (li);
  }
  else {
    return NULL;
  }
}

static edge_t *pick_candidate_edge (list_t *l, node_t *n, int type, node_t **otherp)
{
  listitem_t *li;
  listitem_t *mi;
  edge_t *e, *eopt;
  node_t *other;
  int av;

  eopt = NULL;
  other = NULL;
  
  for (li = list_first (l); li; li = list_next (li)) {
    e = (edge_t *) list_value (li);
    if (e->type != type) continue;
    av = available_edge (e);
    if (!av) continue;

    eopt = e;
    
    if (e->a == n) {
      if (av & 1) {
	other = e->b;
      }
      else {
	other = e->a;
      }
    }
    else {
      Assert (e->b == n, "Hmm");
      if (av & 1) {
	other = e->a;
      }
      else {
	other = e->b;
      }
    }
    for (mi = list_first (other->e); mi; mi = list_next (mi)) {
      edge_t *f;
      f = (edge_t *) list_value (mi);
      if (f->type != type) continue;
      if (f == e) continue;
      if (available_edge (f)) {
	break;
      }
    }
    if (mi) {
      *otherp = other;
      return e;
    }
  }
  *otherp = other;
  return eopt;
}


static list_t *compute_raw_stacks (netlist_t *N, list_t *l, int type)
{
  node_t *n;
  list_t *stks;
  list_t *onestk;
  edge_t *e;
  node_t *other;

  stks = list_new ();


#if 0
  printf ("Type: %c\n", type == EDGE_PFET ? 'p' : 'n');
#endif  

  while ((n = pick_node_odd_degree (l, type))) {
#if 0
    printf ("candidate node: ");
    dump_node (N, n);
    printf ("\n");
#endif
    /* grab edges, walk the graph! */
    onestk = list_new ();
    list_append (onestk, n);
    while ((e = pick_candidate_edge (n->e, n, type, &other))) {
      list_append (onestk, e);
      list_append (onestk, (void*)(long)e->visited);
      e->visited = e->nfolds;
      list_append (onestk, other);
#if 0
      printf ("\t edge: ");
      dump_edge (N, e);
      printf ("\n");
#endif
      n = other;
    }
    if (list_length (onestk) == 1) {
      list_free (onestk);
    }
    else {
      list_append (stks, onestk);
    }
  }
  return stks;
}


void stk_init (ActPass *a)
{
  ActNetlistPass *nl = NULL;
  RawActStackPass *_sp;
  ActDynamicPass *dp = dynamic_cast<ActDynamicPass *> (a);
  
  Assert (dp, "What?");

#if 0
  printf ("[stk] cur state: %p\n", config_get_state ());
  printf ("[stk] set to: %p\n", dp->getConfig());
  config_set_state (dp->getConfig());
#endif
  
  if (dp->getPtrParam("raw")) {
    warning ("stk_init(): Stack pass already created. Skipping.");
    return;
  }

  if (!a->getAct()->pass_find ("prs2net")) {
    nl = new ActNetlistPass (a->getAct());
  }
  a->AddDependency ("prs2net");

  ActPass *pass = a->getAct()->pass_find ("prs2net");
  Assert (pass, "Hmm...");
  nl = dynamic_cast<ActNetlistPass *>(pass);
  Assert (nl, "Hmm too...");

  _sp = new RawActStackPass (a);
  _sp->setNL (nl);
  dp->setParam ("raw", (void *)_sp);
}
  
void stk_run (ActPass *ap, Process *p)
{
  /* nothing to do */
}

void stk_recursive (ActPass *ap, Process *p, int mode)
{
  /* nothing extra */
}


void *stk_proc (ActPass *_ap, Process *p, int mode)
{
  ActDynamicPass *ap = dynamic_cast<ActDynamicPass *> (_ap);
  RawActStackPass *_sp = (RawActStackPass *)ap->getPtrParam ("raw");
  Assert (_sp, "What?");
  
  netlist_t *N = _sp->getNL (p);
  Assert (N, "What?");

  node_t *n;
  list_t *pnodes, *nnodes;
  listitem_t *li, *mi;
  int maxedges;

  /* check we have already handled this process */
#if 0
  printf ("--------------------------------------------\n");
  printf ("creating stacks for: %s\n", p->getName());
#endif  

  /* nodes to be processed */
  pnodes = list_new ();
  nnodes = list_new ();

  maxedges = 8;
  /* flag nodes that will need a contact */
  for (n = N->hd; n; n = n->next) {
    int pc, nc;
    
    maxedges++;
    
    if (n->supply) {
      n->contact = 1;
    }
    if (n->v && n->v->v->output) {
      n->contact = 1;
    }
    node_degree (n, &pc, &nc);
    maxedges += WEIGHT_SHARING*(pc + nc);
    if ((nc & 1) || nc > 2) {
      n->contact = 1;
    }
    if ((pc & 1) || pc > 2) {
      n->contact = 1;
    }
    if (nc > 0 && pc > 0) {
      n->contact = 1;
    }
    if (nc > 0) {
      list_append (nnodes, n);
    }
    if (pc > 0) {
      list_append (pnodes, n);
    }
  }

  /* 
     We have the list of n nodes and p nodes.

     Each node has a list of edges, so we need to find pairing
     opportunities.

     If an edge has folds:
        a pairing opportunity = min(folds-n, folds-p)
	and there will be residual opportunities.

     Fold conflicting pairing opportunities into conflicting edges

     - pick pairing that minimizes conflicts
     - search tree for all the conflicting options 
         - remove all conflicting edges, and recurse
	 - traverse tree according to max # of abutments possible
	     min(degree of vertex, # of edges/2)
  */

  Heap *pairs;
  Heap *final;
  list_t *rawpairs;

  pairs = heap_new (32);
  rawpairs = list_new ();

#if 0
  printf ("raw-pairs:\n");
#endif
  for (li = list_first (nnodes); li; li = list_next (li)) {
    node_t *l, *m;
    l = (node_t *) list_value (li);
    
    for (mi = list_first (pnodes); mi; mi = list_next (mi)) {
      /* find potential pairing opportunities */
      m = (node_t *) list_value (mi);

      listitem_t *ei, *ej;
      for (ei = list_first (l->e); ei; ei = list_next (ei)) {
	edge_t *e1;
	e1 = (edge_t *) list_value (ei);
	if (e1->type != EDGE_NFET) continue;
	
	for (ej = list_first (m->e); ej; ej = list_next (ej)) {
	  edge_t *e2;

	  e2 = (edge_t *) list_value (ej);
	  if (e2->type != EDGE_PFET) continue;
				      
	  if (e1->g == e2->g) {
	    /* pairing opportunity */
	    struct gate_pairs *p, *p2;

	    NEW (p, struct gate_pairs);
	    p->l.n = l;
	    p->l.p = m;
	    p->basepair = 1;
	    p->visited = 0;
	    p->u.e.n = e1;
	    p->u.e.p = e2;

	    Assert (e1->visited == 0 && e2->visited == 0, "What");

	    if (e1->a == l) {
	      p->r.n = e1->b;
	    }
	    else {
	      Assert (e1->b == l, "Hmm");
	      p->r.n = e1->a;
	    }
	    if (e2->a == m) {
	      p->r.p = e2->b;
	    }
	    else {
	      Assert (e2->b == m, "Hmm");
	      p->r.p = e2->a;
	    }

	    p->share = MIN(e1->nfolds, e2->nfolds);
	    p->n_start = 0;
	    p->p_start = 0;

	    if (!(p->share & 1)) {
	      /* even, make it odd since otherwise you can have a
		 disconnection chance */
	      p->share--;
	      // add another gate pair, singleton
	      NEW (p2, struct gate_pairs);
	      *p2 = *p;
	      p2->share = 1;
	      p2->nodeshare = p2->l.endpoint (N) + p2->r.endpoint (N);
	    }
	    else {
	      p2 = NULL;
	    }
	      
	    /* nodeshare is good: left and right edges have the *same* node */
	    p->nodeshare = p->l.endpoint (N) + p->r.endpoint (N);

	    /* see if we can find this in the heap */
	    if (!find_pairs (pairs, p)) {
	      heap_insert (pairs, maxedges-COST(p), p);
	      list_append (rawpairs, p);
#if 0
	      dump_pair (N, p);
#endif
	    }
	    else {
	      delete_pair (p);
	    }
	    if (p2 && !find_pairs (pairs, p2)) {
	      heap_insert (pairs, maxedges-COST(p2), p2);
	      list_append (rawpairs, p2);
#if 0
	      dump_pair (N, p);
#endif
	    }
	    else {
	      if (p2) {
		delete_pair (p2);
	      }
	    }
	  }
	}
      }
    }
  }
  /* We have all potential pairing opportunities, which correspond
     to fet chains of length 1.
  */

  final = heap_new (32);
  int found = 1;
  while (heap_size (pairs) > 0) {
    struct gate_pairs *gp;

    found  = 0;
    /* for each element of the heap, attempt to extend the size using
       one of the pairs */
    gp = (struct gate_pairs *) heap_remove_min (pairs);
#if 0
    /* XXX: need to prune the search tree */
    printf ("looking-at:\n");
    dump_pair (N, gp);
#endif

    /*-- find potential extensions to this pair! --*/

    /*- mark all edges in the pairing as visited. 
        Note: visited starts at 0
    -*/
    if (gp->basepair) {
      Assert (gp->share > 0, "What?");
      gp->u.e.n->visited += gp->share;
      gp->u.e.p->visited += gp->share;
    }
    else {
      for (li = list_first (gp->u.gp); li; li = list_next (li)) {
	struct gate_pairs *tmp;
	tmp = (struct gate_pairs *) list_value (li);
	Assert (tmp->basepair, "hmm");
	tmp->u.e.n->visited += tmp->share;
	tmp->u.e.p->visited += tmp->share;
      }
    }

    for (li = list_first (rawpairs); li; li = list_next (li)) {
      struct gate_pairs *gtmp, *gnew;

      gtmp = (struct gate_pairs *) list_value (li);
      if (gtmp->available_basepair ()) {
	/*-- this pair is still available --*/
	if ((gtmp->l == gp->l) || (gtmp->l == gp->r) ||
	    (gtmp->r == gp->l) || (gtmp->r == gp->r)) {
	  /* opportunity! */
	  NEW (gnew, struct gate_pairs);
	  
	  gnew->share = gtmp->share + gp->share;
	  gnew->nodeshare = gtmp->nodeshare + gp->nodeshare;

	  gnew->basepair = 0;
	  if (gp->basepair) {
	    gnew->u.gp = list_new ();
	    list_append (gnew->u.gp, gp);
	  }
	  else {
	    gnew->u.gp = list_dup (gp->u.gp);
	  }

	  if (gtmp->l == gp->l) {
	    gnew->nodeshare -= 2*gtmp->l.endpoint(N) - gtmp->l.midpoint(N);
	    
	    gnew->l = gtmp->r;
	    gnew->r = gp->r;

	    list_append_head (gnew->u.gp, gtmp);
	  }
	  else if (gtmp->l == gp->r) {
	    gnew->nodeshare -= 2*gtmp->l.endpoint(N) - gtmp->l.midpoint(N);
	    
	    gnew->l = gp->l;
	    gnew->r = gtmp->r;

	    list_append (gnew->u.gp, gtmp);
	  }
	  else if (gtmp->r == gp->l) {
	    gnew->nodeshare -= 2*gtmp->r.endpoint(N) - gtmp->r.midpoint(N);
	    
	    gnew->l = gtmp->l;
	    gnew->r = gp->r;
	    
	    list_append_head (gnew->u.gp, gtmp);
	  }
	  else if (gtmp->r == gp->r) {
	    gnew->nodeshare -= 2*gtmp->r.endpoint(N) - gtmp->r.midpoint(N);
	    
	    gnew->l = gp->l;
	    gnew->r = gtmp->l;
	    
	    list_append (gnew->u.gp, gtmp);
	  }
	  found = 1;
	  if (!find_pairs (pairs, gnew)) {
	    heap_insert (pairs, maxedges - COST(gnew), gnew);
#if 0
	    printf ("new-pair: ");
	    dump_pair (N, gnew);
#endif
	  }
	  else {
	    delete_pair (gnew);
	  }
	}
      }
    }

    if (gp->basepair) {
      gp->u.e.n->visited -= gp->share;
      gp->u.e.p->visited -= gp->share;

      Assert (gp->u.e.n->visited == 0, "Hmm");
      Assert (gp->u.e.p->visited == 0, "Hmm");
    }
    else {
      for (li = list_first (gp->u.gp); li; li = list_next (li)) {
	struct gate_pairs *tmp;
	tmp = (struct gate_pairs *) list_value (li);
	Assert (tmp->basepair, "Hmm");
	tmp->u.e.n->visited -= tmp->share;
	tmp->u.e.p->visited -= tmp->share;
      }
    }

    if (!found && !find_pairs (final, gp)) {
      heap_insert (final, maxedges - COST(gp), gp);
    }
    else {
      if (!gp->basepair) {
	/* don't free raw pairs */
	delete_pair (gp);
      }
    }
  }

  /* 
     root of search tree, empty.
     select some stack set with maximal cost
  */
  list_t *stks;

  stks = list_new ();

#if 0
  printf ("-- candidates ---\n");
#endif    
  while (heap_size (final) > 0) {
    struct gate_pairs *gp;
    
    gp = (struct gate_pairs *) heap_remove_min (final);

#if 0
    dump_pair (N, gp);
#endif      

    if (gp->available_mark ()) {
      list_append (stks, gp);
    }
    else {
      if (!gp->basepair) {
	delete_pair (gp);
      }
    }
  }

  for (li = list_first (rawpairs); li; li = list_next (li)) {
    struct gate_pairs *gp;

    gp = (struct gate_pairs *) list_value (li);

    if (!gp->visited) {
      delete_pair (gp);
    }
  }
  list_free (rawpairs);
  
  /*-- stks has the final candidate stacks.
    Now we add the remaining edges where possible, stitching together
    stacks if that is a possibility.
    --*/
  for (li = list_first (stks); li; li = list_next (li)) {
    struct gate_pairs *gp;

    gp = (struct gate_pairs *) list_value (li);

    /* attempt to extend stacks left and right */
    //printf ("-- attempt to extend --\n");
    //dump_pair (N, gp);

    extend_gatepair (N, &gp, 0);
    extend_gatepair (N, &gp, 1);
    //dump_pair (N, gp);
    list_value (li) = gp;

    gp->l.n->contact = 1;
    gp->l.p->contact = 1;
    
    gp->r.n->contact = 1;
    gp->r.p->contact = 1;
  }

  /*-- finally, any orphaned edges get stacked up as normal --*/
  list_t *tmplist;

  tmplist = list_new ();
  
  for (li = list_first (nnodes); li; li = list_next (li)) {
    n = (node_t *) list_value (li);
    for (mi = list_first (n->e); mi; mi = list_next (mi)) {
      edge_t *e;
      e = (edge_t *) list_value (mi);
      if (e->type != EDGE_NFET) continue;
      if (!available_edge (e)) continue;
      list_append (tmplist, n);
      break;
    }
  }
  list_free (nnodes);
  nnodes = tmplist;

  tmplist = list_new ();
  for (li = list_first (pnodes); li; li = list_next (li)) {
    n = (node_t *) list_value (li);
    for (mi = list_first (n->e); mi; mi = list_next (mi)) {
      edge_t *e;
      e = (edge_t *) list_value (mi);
      if (e->type != EDGE_PFET) continue;
      if (!available_edge (e)) continue;
      list_append (tmplist, n);
      break;
    }
  }
  list_free (pnodes);
  pnodes = tmplist;

  list_t *stk_n = NULL, *stk_p = NULL;
  if (list_length (nnodes) > 0) {
    stk_n = compute_raw_stacks (N, nnodes, EDGE_NFET);
  }
  list_free (nnodes);
  if (list_length (pnodes) > 0) {
    stk_p = compute_raw_stacks (N, pnodes, EDGE_PFET);
  }
  list_free (pnodes);
  
  list_t *retlist;
  retlist = list_new ();
  list_append (retlist, stks);
  list_append (retlist, stk_n);
  list_append (retlist, stk_p);

  return retlist;
}

void *stk_data (ActPass *ap, Data *d, int mode)
{
  return NULL;
}

void *stk_chan (ActPass *ap, Channel *c, int mode)
{
  return NULL;
}

void stk_free (ActPass *ap, void *v)
{
  if (v) {
    list_t *stk = (list_t *)v;
    /* XXX do something here! */
    list_free (stk);
  }
}


void stk_done (ActPass *_ap)
{
  ActDynamicPass *ap = dynamic_cast<ActDynamicPass *> (_ap);
  RawActStackPass *_sp = (RawActStackPass *)ap->getPtrParam ("raw");
  Assert (_sp, "What?");
  delete _sp;
  ap->setParam ("raw", (void *)NULL);
}
