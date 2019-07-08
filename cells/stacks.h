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
#ifndef __ACT_STACKS_H__
#define __ACT_STACKS_H__


struct node_pair {
  node_t *n, *p;
  bool operator==(const node_pair &x) {
    return n == x.n && p == x.p;
  }
  int endpoint (netlist_t*N) 
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
  int midpoint (netlist_t *N)
  {
    if (n == p) { return 1; } else { return 0; }
  }
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
  int nodeshare;	     // shared vertical nodes + bonus
                             // points for supply on ends of the stack
};


list_t *stacks_create (netlist_t *N);
void geom_create_from_stack (Act *a, FILE *fplef, netlist_t *N, list_t *stacks,
			     int *sizex, int *sizey);


#endif /* __ACT_STACKS_H__ */
