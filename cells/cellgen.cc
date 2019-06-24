#include <stdio.h>
#include <string.h>
#include <list.h>
#include <act/passes/netlist.h>
#include <act/layout/geom.h>
#include <act/layout/tech.h>
#include "stacks.h"

void print_dualstack (netlist_t *N, struct gate_pairs *gp)
{
  int flavor;
  if (gp->basepair) {
    flavor = gp->u.e.n->flavor;
  }
  else {
    struct gate_pairs *tmp;
    tmp = (struct gate_pairs *) list_value (list_first (gp->u.gp));
    flavor = tmp->u.e.n->flavor;
  }

  WellMat *nfet_well = Technology::T->well[EDGE_NFET][flavor];
  WellMat *pfet_well = Technology::T->well[EDGE_PFET][flavor];

  DiffMat *ndiff = Technology::T->diff[EDGE_NFET][flavor];
  DiffMat *pdiff = Technology::T->diff[EDGE_PFET][flavor];

  FetMat *nfet = Technology::T->fet[EDGE_NFET][flavor];
  FetMat *pfet = Technology::T->fet[EDGE_PFET][flavor];

  PolyMat *poly = Technology::T->poly;

  /* ok, now we can draw! */
  Assert (nfet && pfet && poly && ndiff && pdiff, "What?");

  /* let's draw rectangles */
  

}

void print_singlestack (netlist_t *N, list_t *l)
{
  int flavor;
  node_t *n;
  edge_t *e;

  if (list_length (l) < 3) return;

  n = (node_t *) list_value (list_first (l));
  e = (edge_t *) list_value (list_next (list_first (l)));

  flavor = e->flavor;
  
  WellMat *nfet_well = Technology::T->well[EDGE_NFET][flavor];
  WellMat *pfet_well = Technology::T->well[EDGE_PFET][flavor];

  DiffMat *ndiff = Technology::T->diff[EDGE_NFET][flavor];
  DiffMat *pdiff = Technology::T->diff[EDGE_PFET][flavor];

  FetMat *nfet = Technology::T->fet[EDGE_NFET][flavor];
  FetMat *pfet = Technology::T->fet[EDGE_PFET][flavor];

  PolyMat *poly = Technology::T->poly;

  /* ok, now we can draw! */
  Assert (nfet && pfet && poly && ndiff && pdiff, "What?");

  /* let's draw rectangles */
  

}


void geom_create_from_stack (netlist_t *N, list_t *stacks)
{
  listitem_t *li;
  
  li = list_first (stacks);
  list_t *gplist = (list_t *) list_value (li);
  if (list_length (gplist) > 0) {
    listitem_t *si;
    int x = 0;
    for (si = list_first (gplist); si; si = list_next (si)) {
      struct gate_pairs *gp;
      gp = (struct gate_pairs *) list_value (si);
      /*--- process gp ---*/
      //print_dualstack_tcl (N, gp);
    }
  }

  li = list_next (li);
  list_t *nstk = (list_t *) list_value (li);
  if (nstk && list_length (nstk) > 0) {
    listitem_t *si;
    int x = 0;
    for (si = list_first (nstk); si; si = list_next (si)) {
      list_t *stack = (list_t *) list_value (si);
      listitem_t *ti;
      node_t *n;
      edge_t *e;
      
      /* n-stack */
      ti = list_first (stack);
      while (ti && list_next (ti)) {
	n = (node_t *) list_value (ti);
	ti = list_next (ti);
	e = (edge_t *) list_value (ti);
	ti = list_next (ti);
      }
      Assert (ti && !list_next (ti), "Hmm");
      n = (node_t *) list_value (ti);
    }
  }

  li = list_next (li);
  list_t *pstk = (list_t *) list_value (li);
  if (pstk && list_length (pstk) > 0) {
    listitem_t *si;
    int x = 0;
    for (si = list_first (pstk); si; si = list_next (si)) {
      list_t *stack = (list_t *) list_value (si);
      listitem_t *ti;
      node_t *n;
      edge_t *e;
      
      /* p-stack */
      ti = list_first (stack);
      while (ti && list_next (ti)) {
	n = (node_t *) list_value (ti);
	ti = list_next (ti);
	e = (edge_t *) list_value (ti);
	ti = list_next (ti);
      }
      Assert (ti && !list_next (ti), "Hmm");
      n = (node_t *) list_value (ti);
    }
  }
}
