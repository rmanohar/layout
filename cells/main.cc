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
#include <map>
#include <act/passes.h>
#include <act/passes/netlist.h>
#include <act/layout/geom.h>
#include "stacks.h"


int main (int argc, char **argv)
{
  Act *a;
  Process *p;
  
  Act::Init (&argc, &argv);
  Layout::Init();

  a = new Act ("test.act");
  a->Expand();

  p = a->findProcess ("foo<>");

  act_booleanize_netlist (a, p);
  act_prs_to_netlist (a, p);

  std::map<Process *, netlist_t *> *netmap =
    (std::map<Process *, netlist_t *> *) a->aux_find ("prs2net");

  netlist_t *N = netmap->find (p)->second;

  list_t *l = stacks_create (N);

  geom_create_from_stack (N, l);
  
  return 0;
}
