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
#include <math.h>
#include <string.h>
#include <map>

#include <act/passes/cells.h>
#include <act/passes/netlist.h>
#include <act/iter.h>
#include <hash.h>
#include <config.h>

#include "stk_pass.h"
#include "stk_layout.h"
#include "geom.h"
#include "stacks.h"

static double area_multiplier;

static ActNetlistPass *netinfo = NULL;
static ActBooleanizePass *boolinfo = NULL;

void usage (char *name)
{
  fprintf (stderr, "Unknown options.\n");
  fprintf (stderr, "Usage: %s -p procname -l lefname -d defname [-s spicename] [-c cell.act] <file.act>\n", name);
  exit (1);
}

int main (int argc, char **argv)
{
  Act *a;
  Process *p;
  int ch;
  char *proc_name = NULL;
  char *spice = NULL;
  FILE *fp;
  char *lefname = NULL;
  char *defname = NULL;
  char *cellname = NULL;
  int do_place = 0;

  area_multiplier = 1.4;
  
  Act::Init (&argc, &argv);
  config_read ("prs2net.conf");
  Layout::Init();
  
  if (Technology::T->nmetals < 2) {
    fatal_error ("Can't handle a process with fewer than two metal layers!");
  }

  while ((ch = getopt (argc, argv, "c:p:l:d:s:Pa:")) != -1) {
    switch (ch) {
    case 'a':
      area_multiplier = atof (optarg);
      break;
      
    case 'c':
      if (cellname) {
	FREE (cellname);
      }
      cellname = Strdup (optarg);
      break;
      
    case 'P':
      do_place = 1;
      break;

    case 'p':
      if (proc_name) {
	FREE (proc_name);
      }
      proc_name = Strdup (optarg);
      break;

    case 'd':
      if (defname) {
	FREE (defname);
      }
      defname = Strdup (optarg);
      break;
      
    case 'l':
      if (lefname) {
	FREE (lefname);
      }
      lefname = Strdup (optarg);
      break;

    case 's':
      if (spice) {
	FREE (spice);
      }
      spice = Strdup (optarg);
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

  if (!proc_name) {
    fatal_error ("Missing process name!");
  }
  if (!lefname) {
    fatal_error ("Missing lef name!");
  }
  if (!defname) {
    fatal_error ("Missing def name!");
  }

  /*--- read in and expand ACT file ---*/
  a = new Act (argv[optind]);

  /* merge in cell file name, if it exists */
  if (cellname) {
    FILE *cf = fopen (cellname, "r");
    if (cf) {
      fclose (cf);
      a->Merge (cellname);
    }
  }
  
  a->Expand();


  /*--- find top level process ---*/
  p = a->findProcess (proc_name);
  if (!p) {
    fatal_error ("Could not find process `%s' in act file `%s'.\n",
		 proc_name, argv[optind]);
  }

  /*--- core passes ---*/
  ActCellPass *cp = new ActCellPass (a);
  cp->run (p);
  boolinfo = new ActBooleanizePass (a);
  netinfo = new ActNetlistPass (a);
  netinfo->run (p);

  ActStackPass *stkp = new ActStackPass (a);

  ActStackLayoutPass *lp = new ActStackLayoutPass (a);

  if (spice) {
    FILE *sp = fopen (spice, "w");
    if (!sp) { fatal_error ("Could not open file `%s'", spice); }
    netinfo->Print (sp, p);
    fclose (sp);
  }
  //a->Print (stdout);

  lp->run (p);
  
  ActNamespace *cell_ns = a->findNamespace ("cell");
  Assert (cell_ns, "No cell namespace? No circuits?!");

  //a->Print (stdout);

  /*--- print out lef file, plus rectangles ---*/
  FILE *xfp = fopen (lefname, "w");
  if (!xfp) {
    fatal_error ("Could not open file `%s' for writing", lefname);
  }
  lp->emitLEFHeader (xfp);

  ActTypeiter it(cell_ns);

  for (it = it.begin(); it != it.end(); it++) {
    Type *u = (*it);
    Process *p = dynamic_cast<Process *>(u);
    
    if (!p) continue;
    if (!p->isCell()) continue;
    if (!p->isExpanded()) continue;
    netlist_t *N = netinfo->getNL (p);

    if (!N) {
      /* cell defined, but not used */
      continue;
    }

    /* append to LEF file */
    lp->emitLEF (xfp, p);
    fprintf (xfp, "\n");
    
    /* generate .rect file */
    FILE *tfp;
    char cname[10240];
    int len;

    a->msnprintfproc (cname, 10240, p);
    len = strlen (cname);
    snprintf (cname + len, 10240-len, ".rect");

    tfp = fopen (cname, "w");
    lp->emitRect (tfp, p);
    fclose (tfp);
  }
  fclose (xfp);

  /* -- preparation for DEF file generation: create flat netlist -- */
  boolinfo->createNets (p);

  /* --- print out def file --- */
  FILE *yfp = fopen (defname, "w+");
  if (!yfp) {
    fatal_error ("Could not open file `%s' for writing", defname);
  }
  lp->emitDEF (yfp, p, area_multiplier);
  fclose (yfp);

  /* -- dump updated cells file, if necessary -- */
  if (cellname) {
    xfp = fopen (cellname, "w");
    if (!xfp) {
      fatal_error ("Could not write `%s'", cellname);
    }
    cp->Print (xfp);
    fclose (xfp);
  }

  return 0;
}
