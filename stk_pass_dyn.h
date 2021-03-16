/*************************************************************************
 *
 *  Copyright (c) 2021 Rajit Manohar
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
#ifndef __ACT_STK_PASS_DYN_H__
#define __ACT_STK_PASS_DYN_H__

#include <act/passes/netlist.h>
#include <map>
#include <hash.h>

/*-- data structures --*/

struct node_pair {
  node_t *n, *p;
  bool operator==(const node_pair &x) { return n == x.n && p == x.p; }
  int endpoint (netlist_t*N);
  int midpoint (netlist_t *N) { if (n == p) { return 1; } else { return 0; } }
};

struct gate_pairs {
  struct node_pair l, r;
  union {
    list_t *gp;		     // gate pair list
    struct {
      edge_t *n, *p;		// base case
      /* XXX: right now we don't check flavor, but we should! 
	 The flavor of both edges must match.
       */
    } e;
  } u;
  unsigned int basepair:1;   // 1 if it is a basepair
  unsigned int visited:1;    // 1 if visited
  short share;		     // # of shared gates
  short n_start, p_start;    // start id of edges
  short n_fold, p_fold;	     // folding threshold
  int nodeshare;	     // shared vertical nodes + bonus
                             // points for supply on ends of the stack

  /* check if a base pair is available */
  int available_basepair();

  /* check if the gate pair elements are available */
  int available();

  /*
   * check if it is available, and if so mark it
   */
  int available_mark ();
};

class RawActStackPass {
public:
  RawActStackPass (ActPass *p) { me = p; }
  
  int isEmpty (list_t *stk);
  list_t *getStacks (Process *p = NULL);
  netlist_t *getNL (Process *p = NULL) { return nl->getNL (p); }

  void setNL (ActNetlistPass *_nl) { nl = _nl; }
  void *getMap (Process *p) { return me->getMap (p); }
  ActPass *getPass () { return me; }

private:
  ActNetlistPass *nl;
  ActPass *me;
};

extern "C" {

  void stk_init (ActPass *ap);
  void stk_run (ActPass *ap, Process *p);
  void stk_recursive (ActPass *ap, UserDef *u, int mode);
  void *stk_proc (ActPass *ap, Process *p, int mode);
  void *stk_data (ActPass *ap, Data *d, int mode);
  void *stk_chan (ActPass *ap, Channel *c, int mode);
  void stk_free (ActPass *ap, void *v);
  void stk_done (ActPass *ap);

}

#endif /* __ACT_STK_PASS_DYN_H__ */
