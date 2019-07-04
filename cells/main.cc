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

static Act *global_act;

void print_procname (Act *a, Process *p, FILE *fp)
{
  char buf[1024];
  
  if (p->getns() && p->getns() != ActNamespace::Global()) {
    char *s = p->getns()->Name();
    a->mfprintf (fp, "%s::", s);
    FREE (s);
  }
  const char *proc_name = p->getName();
  int len = strlen (proc_name);
  if (len > 2 && proc_name[len-1] == '>' && proc_name[len-2] == '<') {
    int i = 0;
    while (proc_name[i] != '<') {
      fputc (proc_name[i], fp);
      i++;
    }
  }
  else {
    a->mfprintf (fp, "%s", proc_name);
  }
}


void usage (char *name)
{
  fprintf (stderr, "Unknown options.\n");
  fprintf (stderr, "Usage: %s -p procname [-o dirname] <file.act>\n", name);
  exit (1);
}


static void lef_header (FILE *fp)
{
  /* -- lef header -- */
  fprintf (fp, "VERSION 5.8 ;\n\n");
  fprintf (fp, "BUSBITCHARS \"[]\" ;\n\n");
  fprintf (fp, "DIVIDERCHAR \"/\" ;\n\n");
  fprintf (fp, "UNITS\n");
  fprintf (fp, "    DATABASE MICRONS 2000 ;\n");
  fprintf (fp, "END UNITS\n\n");

  fprintf (fp, "MANUFACTURINGGRID 0.000500 ; \n\n");
  fprintf (fp, "CLEARANCEMEASURE EUCLIDEAN ; \n\n");
  fprintf (fp, "USEMINSPACING OBS ON ; \n\n");

  fprintf (fp, "SITE CoreSite\n");
  fprintf (fp, "    CLASS CORE ;\n");
  fprintf (fp, "    SIZE 0.2 BY 1.2\n"); /* not used */
  fprintf (fp, "END CoreSite\n\n");
  
  int i;
  for (i=0; i < Technology::T->nmetals; i++) {
    RoutingMat *mat = Technology::T->metal[i];
    double scale = Technology::T->scale/1000.0;
    fprintf (fp, "LAYER %s\n", mat->getName());
    fprintf (fp, "   TYPE ROUTING ;\n");

    fprintf (fp, "   DIRECTION %s ;\n",
	     (i % 2) ? "VERTICAL" : "HORIZONTAL");
    fprintf (fp, "   MINWIDTH %.6f ;\n", mat->minWidth()*scale);
    if (mat->minArea() > 0) {
      fprintf (fp, "   AREA %.6f ;\n", mat->minArea()*scale*scale);
    }
    fprintf (fp, "   WIDTH %.6f ;\n", mat->minWidth()*scale);
    fprintf (fp, "   SPACING %.6f ;\n", mat->minSpacing()*scale);
    fprintf (fp, "   PITCH %.6f %.6f;\n",
	     mat->getPitch()*scale, mat->getPitch()*scale);
    fprintf (fp, "END %s\n\n", mat->getName());


    if (i != Technology::T->nmetals - 1) {
      fprintf (fp, "LAYER %s\n", mat->getUpC()->getName());
      fprintf (fp, "    TYPE CUT ;\n");
      fprintf (fp, "    SPACING %.6f ;\n", scale*mat->getUpC()->getSpacing());
      fprintf (fp, "    WIDTH %.6f ;\n",  scale*mat->getUpC()->getWidth ());
      fprintf (fp, "END %s\n\n", mat->getUpC()->getName());
    }
  }

  fprintf (fp, "\n");

  for (i=0; i < Technology::T->nmetals-1; i++) {
    RoutingMat *mat = Technology::T->metal[i];
    Contact *vup = mat->getUpC();
    double scale = Technology::T->scale/1000.0;
    double w;
    
    fprintf (fp, "VIA %s_C DEFAULT\n", vup->getName());

    w = (vup->getWidth() + 2*vup->getSym())*scale/2;
    fprintf (fp, "   LAYER %s ;\n", mat->getName());
    fprintf (fp, "     RECT %.6f %.6f %.6f %.6f ;\n", -w, -w, w, w);

    w = vup->getWidth()*scale/2;
    fprintf (fp, "   LAYER %s ;\n", vup->getName());
    fprintf (fp, "     RECT %.6f %.6f %.6f %.6f ;\n", -w, -w, w, w);

    w = (vup->getWidth() + 2*vup->getSymUp())*scale/2;
    fprintf (fp, "   LAYER %s ;\n", Technology::T->metal[i+1]->getName());
    fprintf (fp, "     RECT %.6f %.6f %.6f %.6f ;\n", -w, -w, w, w);
    
    fprintf (fp, "END %s_C\n\n", vup->getName());
  }
}


static void def_header (FILE *fp, const char *proc)
{
  int i;
  
  /* -- def header -- */
  fprintf (fp, "VERSION 5.8 ;\n\n");
  fprintf (fp, "BUSBITCHARS \"[]\" ;\n\n");
  fprintf (fp, "DIVIDERCHAR \"/\" ;\n\n");
  fprintf (fp, "DESIGN ");
  i = strlen (proc);
  if (i > 2 && proc[i-1] == '>' && proc[i-2] == '<') {
    i = 0;
    while (proc[i] && proc[i] != '<') {
      fputc (proc[i], fp);
      i++;
    }
    fprintf (fp, "\n");
  }
  else {
    fprintf (fp, "%s ;\n", proc);
  }
  fprintf (fp, "\nUNITS DISTANCE MICRONS 2000 ;\n\n");
}

static int total_instances = 0;

static void count_inst (void *x, ActId *prefix, Process *p)
{
  if (p->getprs()) {
    total_instances++;
  }
}

static void dump_inst (void *x, ActId *prefix, Process *p)
{
  FILE *fp = (FILE *)x;
  char buf[10240];

  if (p->getprs()) {
    prefix->sPrint (buf, 10240);
    global_act->mfprintf (fp, "%s ", buf);
    print_procname (global_act, p, fp);
    fprintf (fp, " ;\n");
  }
}

static void emit_def (Act *a, FILE *fp)
{
  global_act = a;
  /*--- DIE AREA ---*/

  /*

DIEAREA ( 83600 71820 ) ( 104400 91200 ) ;

ROW CORE_ROW_0 CoreSite 83600 71820 N DO 52 BY 1 STEP 400 0
 ;
ROW CORE_ROW_1 CoreSite 83600 75240 FS DO 52 BY 1 STEP 400 0
 ;
ROW CORE_ROW_2 CoreSite 83600 78660 N DO 52 BY 1 STEP 400 0
 ;
ROW CORE_ROW_3 CoreSite 83600 82080 FS DO 52 BY 1 STEP 400 0
 ;
ROW CORE_ROW_4 CoreSite 83600 85500 N DO 52 BY 1 STEP 400 0
 ;

TRACKS X 83800 DO 52 STEP 400 LAYER Metal9 ;
TRACKS Y 72770 DO 25 STEP 760 LAYER Metal9 ;
TRACKS Y 72580 DO 33 STEP 570 LAYER Metal8 ;
TRACKS X 83800 DO 52 STEP 400 LAYER Metal8 ;

  */

  act_flat_apply_processes (a, fp, count_inst);

  /* -- instances  -- */
  fprintf (fp, "COMPONENTS %d ;\n", total_instances);
  act_flat_apply_processes (a, fp, dump_inst);
  /*
    inst2591 NAND4X2 + PLACED ( 100000 71820 ) N ;
  */
  fprintf (fp, "END COMPONENTS\n\n");

  /* -- no I/O constraints -- */
  fprintf (fp, "PINS 0 ;\n");
  fprintf (fp, "END PINS\n\n");

  /* -- nets -- */
  fprintf (fp, "NETS 0; \n");
  /*
- net1237
  ( inst5638 A ) ( inst4678 Y )
 ;
  */
  
  fprintf (fp, "END NETS\n\n");

  fprintf (fp, "END DESIGN\n");
}


int main (int argc, char **argv)
{
  Act *a;
  Process *p;
  int ch;
  char *proc_name = NULL;
  char *outdir = NULL;
  FILE *fp;
  char *lefname = NULL;
  char *defname = NULL;
  
  Act::Init (&argc, &argv);
  Layout::Init();

  while ((ch = getopt (argc, argv, "p:l:d:o:")) != -1) {
    switch (ch) {
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
  a->Expand();

  /*--- find top level process ---*/
  p = a->findProcess (proc_name);
  if (!p) {
    fatal_error ("Could not find process `%s' in act file `%s'.\n",
		 proc_name, argv[optind]);
  }

  /*--- core passes ---*/
  act_prs_to_cells (a, p, 0);
  act_booleanize_netlist (a, p);
  act_prs_to_netlist (a, p);

  //act_emit_netlist (a, p, stdout);
  //a->Print (stdout);
  
  /*--- create stacks for all cells ---*/
  std::map<Process *, netlist_t *> *netmap =
    (std::map<Process *, netlist_t *> *) a->aux_find ("prs2net");

  ActNamespace *cell_ns = a->findNamespace ("cell");
  Assert (cell_ns, "No cell namespace? No circuits?!");

  /*--- print out lef file, plus rectangles ---*/
  fp = fopen (lefname, "w");
  if (!fp) {
    fatal_error ("Could not open file `%s' for writing", lefname);
  }
  lef_header (fp);

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
    geom_create_from_stack (a, fp, N, l);
    fprintf (fp, "\n");
  }
  fclose (fp);

  /*--- print out def file ---*/
  fp = fopen (defname, "w");
  if (!fp) {
    fatal_error ("Could not open file `%s' for writing", defname);
  }
  def_header (fp, proc_name);
  emit_def (a, fp);
  fclose (fp);
  
  return 0;
}
