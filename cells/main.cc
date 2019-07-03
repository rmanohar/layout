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
#include <unistd.h>
#include <map>
#include <act/passes.h>
#include <act/passes/netlist.h>
#include <act/layout/geom.h>
#include <act/iter.h>
#include "stacks.h"

void usage (char *name)
{
  fprintf (stderr, "Unknown options.\n");
  fprintf (stderr, "Usage: %s -p procname [-o dirname] <file.act>\n", name);
  exit (1);
}
  

int main (int argc, char **argv)
{
  Act *a;
  Process *p;
  int ch;
  char *proc_name = NULL;
  char *outdir = NULL;
  
  Act::Init (&argc, &argv);
  Layout::Init();

  while ((ch = getopt (argc, argv, "p:o:")) != -1) {
    switch (ch) {
    case 'p':
      if (proc_name) {
	FREE (proc_name);
      }
      proc_name = Strdup (optarg);
      break;
      
    case 'o':
      if (outdir) {
	FREE (outdir);
      }
      outdir = Strdup (optarg);
      break;

    case '?':
      fprintf (stderr, "Unknown option.\n");
      usage (argv[0]);
      break;
      
    default:
      fatal_error ("shouldn't be here");
      break;
    }
  }

  if (optind != argc - 1) {
    fprintf (stderr, "Missing act file name.\n");
    usage (argv[0]);
  }

  a = new Act (argv[optind]);
  a->Expand();

  if (!proc_name) {
    fatal_error ("Missing process name!");
  }

  p = a->findProcess (proc_name);
  if (!p) {
    fatal_error ("Could not find process `%s' in act file `%s'.\n",
		 proc_name, argv[optind]);
  }
		   
  act_prs_to_cells (a, p, 0);
  act_booleanize_netlist (a, p);
  act_prs_to_netlist (a, p);

  /* stacks! */
  
  std::map<Process *, netlist_t *> *netmap =
    (std::map<Process *, netlist_t *> *) a->aux_find ("prs2net");

  ActNamespace *cell_ns = a->findNamespace ("cell");
  Assert (cell_ns, "No cell namespace? No circuits?!");

  ActTypeiter it(cell_ns);
  for (it = it.begin(); it != it.end(); it++) {
    Type *u = (*it);
    Process *p = dynamic_cast<Process *>(u);
    if (!p) continue;
    if (!p->isCell()) continue;
    if (!p->isExpanded()) continue;
    if (netmap->find (p) == netmap->end()) continue;
    netlist_t *N = netmap->find (p)->second;
    Assert (N, "Hmm...");

    list_t *l = stacks_create (N);
    geom_create_from_stack (N, l, p);
  }
  
  return 0;
}
