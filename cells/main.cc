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

void usage (char *name)
{
  fprintf (stderr, "Unknown options.\n");
  fprintf (stderr, "Usage: %s -p procname [-s] [-o <name>] [-a <mult>] [-c <cell>] <file.act>\n", name);
  fprintf (stderr, " -p procname: name of ACT process corresponding to the top-level of the design\n");
  fprintf (stderr, " -o <name>: output files will be <name>.<extension> (default: out)\n");
  fprintf (stderr, " -s : emit spice netlist\n");
  fprintf (stderr, " -P : include PINS section in DEF file\n");
  fprintf (stderr, " -a <mult>: use <mult> as the area multiplier for the DEF fie (default 1.4)\n");
  fprintf (stderr, " -c <cell>: Read in the <cell> ACT file as a starting point for cells,\n\toverwriting it with an updated version with any new cells\n");
  fprintf (stderr, " -S : share staticizers\n");
  fprintf (stderr, " -A : report area\n");
  fprintf (stderr, "\n");
  exit (1);
}

int main (int argc, char **argv)
{
  Act *a;
  Process *p;
  int ch;
  char *proc_name = NULL;
  char *outname = NULL;
  char *cellname = NULL;
  int do_pins = 0;
  int do_spice = 0;
  char buf[1024];
  FILE *fp;
  double area_multiplier;
  int report_area = 0;

  area_multiplier = 1.4;
  
  Act::Init (&argc, &argv);
  config_read ("prs2net.conf");
  Layout::Init();
  
  if (Technology::T->nmetals < 2) {
    fatal_error ("Can't handle a process with fewer than two metal layers!");
  }

  while ((ch = getopt (argc, argv, "c:p:o:sPAa:")) != -1) {
    switch (ch) {
    case 'A':
      report_area = 1;
      break;
      
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
      do_pins = 1;
      break;

    case 'p':
      if (proc_name) {
	FREE (proc_name);
      }
      proc_name = Strdup (optarg);
      break;

    case 'o':
      if (outname) {
	FREE (outname);
      }
      outname = Strdup (optarg);
      break;
      
    case 's':
      do_spice = 1;
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
  if (!outname) {
    outname = Strdup ("out");
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
  ActStackLayoutPass *lp = new ActStackLayoutPass (a);

  lp->run (p);

  /* --- emit SPICE netlist, if requested --- */
  if (do_spice) {
    ActNetlistPass *netinfo;
    snprintf (buf, 1024, "%s.sp", outname);
    FILE *sp = fopen (buf, "w");
    if (!sp) { fatal_error ("Could not open file `%s'", buf); }
    netinfo = dynamic_cast<ActNetlistPass *>(a->pass_find ("prs2net"));
    netinfo->Print (sp, p);
    fclose (sp);
  }

  ActNamespace *cell_ns = a->findNamespace ("cell");
  Assert (cell_ns, "No cell namespace? No circuits?!");

  //a->Print (stdout);

  /*--- print out lef file, plus rectangles ---*/
  snprintf (buf, 1024, "%s.lef", outname);
  fp = fopen (buf, "w");
  if (!fp) {
    fatal_error ("Could not open file `%s' for writing", buf);
  }
  lp->emitLEFHeader (fp);

  FILE *fpcell;
  snprintf (buf, 1024, "%s.cell", outname);
  fpcell = fopen (buf, "w");
  if (!fpcell) {
    fatal_error ("Could not open file `%s' for writing", buf);
  }
  lp->emitWellHeader (fpcell);

  /* -- walk through cells, emitting 
       1. LEF files for any cell layout
       2. .rect files for those cells
  -- */
  ActTypeiter it(cell_ns);
  for (it = it.begin(); it != it.end(); it++) {
    Type *u = (*it);
    Process *p = dynamic_cast<Process *>(u);
    
    if (!p) continue;
    if (!p->isCell()) continue;
    if (!p->isExpanded()) continue;

    if (!p->getprs()) {
      /* no circuits */
      continue;
    }
    if (!lp->haveRect (p)) {
      continue;
    }

    /* append to LEF file */
    lp->emitLEF (fp, p);
    fprintf (fp, "\n");

    lp->emitWellLEF (fpcell, p);
    fprintf (fpcell, "\n");
    
    /* generate .rect file */
    {
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
  }
  fclose (fp);
  fclose (fpcell);

  /* 
     preparation for DEF file generation: create flat netlist using
     special method in the booleanize pass
  */
  ActBooleanizePass *boolinfo;
  boolinfo = dynamic_cast<ActBooleanizePass *> (a->pass_find ("booleanize"));
  boolinfo->createNets (p);

  /* --- print out def file --- */
  snprintf (buf, 1024, "%s.def", outname);
  fp = fopen (buf, "w+");
  if (!fp) {
    fatal_error ("Could not open file `%s' for writing", buf);
  }
  lp->emitDEF (fp, p, area_multiplier, do_pins);
  fclose (fp);

  if (report_area) {
    double a = lp->getArea();
    a *= Technology::T->scale/1000.0;
    a *= Technology::T->scale/1000.0;
    if (a > 1e4) {
      a /= 1e6;
      printf ("Area: %.3g mm^2\n", a);
    }
    else {
      printf ("Area: %.3g um^2\n", a);
    }
  }

  /* -- dump updated cells file, if necessary -- */
  if (cellname) {
    fp = fopen (cellname, "w");
    if (!fp) {
      fatal_error ("Could not write `%s'", cellname);
    }
    cp->Print (fp);
    fclose (fp);
  }
  return 0;
}
