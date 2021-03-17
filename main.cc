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

#include <act/passes.h>
#include <act/iter.h>
#include <hash.h>
#include <config.h>

#include "stk_layout.h"
#include "geom.h"

void usage (char *name)
{
  fprintf (stderr, "Unknown options.\n");
  fprintf (stderr, "Usage: %s -p procname [-s] [-o <name>] [-a <mult>] [-c <cell>] <file.act>\n", name);
  fprintf (stderr, " -p procname: name of ACT process corresponding to the top-level of the design\n");
  fprintf (stderr, " -o <name>: output files will be <name>.<extension> (default: out)\n");
  fprintf (stderr, " -s : emit spice netlist\n");
  fprintf (stderr, " -P : include PINS section in DEF file\n");
  fprintf (stderr, " -a <mult>: use <mult> as the area multiplier for the DEF fie (default 1.4)\n");
  fprintf (stderr, " -r <ratio> : use this as the aspect ratio = x-size/y-size (default 1.0)\n");
  fprintf (stderr, " -c <cell>: Read in the <cell> ACT file as a starting point for cells,\n\toverwriting it with an updated version with any new cells\n");
  fprintf (stderr, " -S : share staticizers\n");
  //fprintf (stderr, " -A : report area\n");
  fprintf (stderr, " -R : generate report\n");
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
  double aspect_ratio;
  int report = 0;
  int share_staticizers = 0;

  area_multiplier = 1.4;
  aspect_ratio = 1.0;
  
  Act::Init (&argc, &argv);
  {
    char *tmpfile = config_file_name ("macros.conf");
    if (tmpfile) {
      FREE (tmpfile);
      config_read ("macros.conf");
    }
  }
  Layout::Init();
  
  if (Technology::T->nmetals < 2) {
    fatal_error ("Can't handle a process with fewer than two metal layers!");
  }

  while ((ch = getopt (argc, argv, "c:p:o:sSPRa:r:")) != -1) {
    switch (ch) {
    case 'S':
      share_staticizers = 1;
      break;
      
    case 'R':
      report = 1;
      break;
      
    case 'a':
      area_multiplier = atof (optarg);
      break;

    case 'r':
      aspect_ratio = atof (optarg);
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

  new ActApplyPass (a);
  new ActNetlistPass (a);
  new ActDynamicPass (a, "net2stk", "pass_stk.so", "stk");

  ActDynamicPass *lp = new ActDynamicPass(a, "stk2layout", "pass_layout.so", "layout");

  ActNetlistPass *netinfo;
  netinfo = dynamic_cast<ActNetlistPass *>(a->pass_find ("prs2net"));

  if (share_staticizers) {
    netinfo->enableSharedStat();
  }
  netinfo->run (p);

  /* --- emit SPICE netlist, if requested --- */
  if (do_spice) {
    snprintf (buf, 1024, "%s.sp", outname);
    FILE *sp = fopen (buf, "w");
    if (!sp) { fatal_error ("Could not open file `%s'", buf); }
    netinfo->Print (sp, p);
    fclose (sp);
  }

  lp->run (p);

  ActNamespace *cell_ns = a->findNamespace ("cell");
  Assert (cell_ns, "No cell namespace? No circuits?!");

  //a->Print (stdout);

  /*--- print out lef file, plus rectangles ---*/
  snprintf (buf, 1024, "%s.lef", outname);
  fp = fopen (buf, "w");
  if (!fp) {
    fatal_error ("Could not open file `%s' for writing", buf);
  }
  lp->setParam ("lef_file", (void *)fp);

  FILE *fpcell;
  snprintf (buf, 1024, "%s.cell", outname);
  fpcell = fopen (buf, "w");
  if (!fpcell) {
    fatal_error ("Could not open file `%s' for writing", buf);
  }
  lp->setParam ("cell_file", (void *)fpcell);

  /* emit lef and cell files */
  lp->run_recursive (p, 1);
  
  fclose (fp);
  fclose (fpcell);

  lp->setParam ("cell_file", (void*)NULL);
  lp->setParam ("lef_file", (void*)NULL);

  /* emit rect */
  lp->run_recursive (p, 4);
  
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
  lp->setParam ("def_file", (void *)fp);
  lp->setParam ("do_pins", do_pins);
  lp->setParam ("area_mult", area_multiplier);
  lp->setParam ("aspect_ratio", aspect_ratio);

  lp->run_recursive (p, 5);

  fclose (fp);
  lp->setParam ("def_file", (void*)NULL);

  if (report) {
    double a = lp->getRealParam ("total_area");
    double as = lp->getRealParam ("stdcell_area");
    a *= Technology::T->scale/1000.0;
    a *= Technology::T->scale/1000.0;
    as *= Technology::T->scale/1000.0;
    as *= Technology::T->scale/1000.0;
    if (a > 1e4) {
      a /= 1e6;
      as /= 1e6;
      printf ("Total Area: %.3g mm^2\n", a);
      printf ("Total StdCell Area: %.3g mm^2 ", as);
    }
    else {
      printf ("Total Area: %.3g um^2\n", a);
      printf ("Total StdCell Area: %.3g um^2 ", as);
    }
    int stdcellht = lp->getIntParam ("cell_maxheight");
    printf (" (%.2g%%) [height=%d, #tracks=%d]\n", 
	    (as-a)/a*100.0, stdcellht,
	    stdcellht/Technology::T->metal[0]->getPitch());

    lp->run_recursive (p, 2);
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
