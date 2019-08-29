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

#ifdef INTEGRATED_PLACER
#include "placer.h"
#include "placeral.h"
#include "placerdla.h"
#endif

static Act *global_act;

void geom_create_from_stack (Act *a, FILE *fplef,
			     circuit_t *ckt,
			     netlist_t *N, list_t *stacks,
			     int *sizex, int *sizey);



static int print_net (Act *a, FILE *fp, ActId *prefix, act_local_net_t *net)
{
  Assert (net, "Why are you calling this function?");
  if (net->skip) return 0;
  if (net->port) return 0;

  if (A_LEN (net->pins) < 2) return 0;

  fprintf (fp, "- ");
  if (prefix) {
    prefix->Print (fp);
    fprintf (fp, ".");
  }
  ActId *tmp = net->net->toid();
  tmp->Print (fp);
  delete tmp;

  fprintf (fp, "\n  ");

  char buf[10240];

  for (int i=0; i < A_LEN (net->pins); i++) {
    fprintf (fp, " ( ");
    if (prefix) {
      prefix->sPrint (buf, 10240);
      a->mfprintf (fp, "%s.", buf);
    }
    net->pins[i].inst->sPrint (buf, 10240);
    a->mfprintf (fp, "%s ", buf);

    tmp = net->pins[i].pin->toid();
    tmp->sPrint (buf, 10240);
    delete tmp;
    a->mfprintf (fp, "%s ", buf);
    fprintf (fp, ")");
  }
  fprintf (fp, "\n;\n");

  return 1;
}

#ifdef INTEGRATED_PLACER
void add_ckt (Act *a, circuit_t *ckt, ActId *prefix, act_local_net_t *net)
{
  char buf[10240];
  char mbuf[10240];
  int len = 0;

  if (net->skip || net->port || A_LEN (net->pins) < 2) return;
  if (!ckt) return;
    
  if (prefix) {
    prefix->sPrint (buf, 10240);
    strcat (buf, ".");
  }
  else {
    buf[0] = '\0';
  }

  len = strlen (buf);

  ActId *tmp = net->net->toid();
  tmp->sPrint (buf+len, 1024-len);
  delete tmp;
  len += strlen (buf+len);

  std::string netstr(buf);

  if (net->net->isglobal()) {
    // globals are cheap!
    ckt->create_blank_net (netstr, 1e-6);
  }
  else {
    ckt->create_blank_net (netstr, 1.0); // WEIGHT GOES HERE!
  }

  for (int i=0; i < A_LEN (net->pins); i++) {
    len = 0;
    buf[0] = '\0';
    if (prefix) {
      prefix->sPrint (buf, 10240);
      len = strlen (buf);
      if (len < 10200) {
	buf[len++] = '.';
	buf[len] = '\0';
      }
      else {
	warning ("Fix buffer size!");
      }
    }
    net->pins[i].inst->sPrint (buf+len, 10240-len);
    a->msnprintf (mbuf, 10240, "%s", buf);

    std::string blkname(mbuf);

    tmp = net->pins[i].pin->toid();
    tmp->sPrint (buf, 10240);
    a->msnprintf (mbuf, 10240, "%s", buf);
    delete tmp;

    std::string pinname(mbuf);

    ckt->add_pin_to_net (netstr, blkname, pinname);
  }
}
#endif

struct process_aux {

  process_aux() {
    p = NULL;
    x = 0;
    y = 0;
    visited = 0;
    n = NULL;
  }

  Process *p; 			/* sanity */
  int x, y;			/* size of component */

  netlist_t *n;

  /*-- local nets --*/
  int visited;
};

#define TRACK_CONVERSION 18

static std::map<Process *, process_aux *> *procmap = NULL;
static ActNetlistPass *netinfo = NULL;
static ActBooleanizePass *boolinfo = NULL;

static unsigned long netcount = 0;

void _collect_emit_nets (Act *a, ActId *prefix, Process *p, FILE *fp,
			 circuit_t *ckt)
{
  Assert (p->isExpanded(), "What are we doing");

  act_boolean_netlist_t *n = boolinfo->getBNL (p);
  Assert (n, "What!");

  /* first, print my local nets */
  for (int i=0; i < A_LEN (n->nets); i++) {
    if (print_net (a, fp, prefix, &n->nets[i])) {
      netcount++;
#ifdef INTEGRATED_PLACER
      add_ckt (a, ckt, prefix, &n->nets[i]);
#endif      
    }
  }

  ActInstiter i(p->CurScope());

  for (i = i.begin(); i != i.end(); i++) {
    ValueIdx *vx = (*i);
    if (!TypeFactory::isProcessType (vx->t)) continue;

    ActId *newid;
    ActId *cpy;
    
    Process *instproc = dynamic_cast<Process *>(vx->t->BaseType ());
    int ports_exist;

    newid = new ActId (vx->getName());
    if (prefix) {
      cpy = prefix->Clone ();
      ActId *tmp = cpy;
      while (tmp->Rest()) {
	tmp = tmp->Rest();
      }
      tmp->Append (newid);
    }
    else {
      cpy = newid;
    }

    if (vx->t->arrayInfo()) {
      Arraystep *as = vx->t->arrayInfo()->stepper();
      while (!as->isend()) {
	Array *x = as->toArray();
	newid->setArray (x);
	_collect_emit_nets (a, cpy, instproc, fp, ckt);
	delete x;
	newid->setArray (NULL);
	as->step();
      }
      delete as;
    }
    else {
      _collect_emit_nets (a, cpy, instproc, fp, ckt);
    }
    delete cpy;
  }
  return;
}

void usage (char *name)
{
  fprintf (stderr, "Unknown options.\n");
  fprintf (stderr, "Usage: %s -p procname -l lefname -d defname [-s spicename] <file.act>\n", name);
  exit (1);
}

static void def_header (FILE *fp, circuit_t *ckt, Process *p)
{
  int i;
  
  /* -- def header -- */
  fprintf (fp, "VERSION 5.8 ;\n\n");
  fprintf (fp, "BUSBITCHARS \"[]\" ;\n\n");
  fprintf (fp, "DIVIDERCHAR \"/\" ;\n\n");
  fprintf (fp, "DESIGN ");

  global_act->mfprintfproc (fp, p);
  fprintf (fp, " ;\n");
  
  fprintf (fp, "\nUNITS DISTANCE MICRONS %d ;\n\n", MICRON_CONVERSION);
#ifdef INTEGRATED_PLACER
  if (ckt) {
    ckt->def_distance_microns = MICRON_CONVERSION;
    ckt->lef_database_microns = MICRON_CONVERSION;
    ckt->m2_pitch =
      Technology::T->metal[1]->getPitch()*Technology::T->scale/1000.0;
  }
#endif  
}


static int total_instances = 0;
static double total_area = 0.0;

/*-- collect info per process --*/
static void count_inst (void *x, ActId *prefix, Process *p)
{
  if (p->getprs()) {
    process_aux *px;
    total_instances++;
    if (procmap->find (p) == procmap->end()) {
      fatal_error ("Encountered instance of type `%s' that was not converted ot layout!", p->getName());
    }
    px = procmap->find (p)->second;
    Assert (px && px->p == p, "Weird!");

    /*-- collect area --*/
    total_area += px->x*px->y;
  }
}

static void dump_inst (void *x, ActId *prefix, Process *p)
{
  FILE *fp = (FILE *)x;
  char buf[10240];

  if (p->getprs()) {
    /* FORMAT: 
         - inst2591 NAND4X2 ;
         - inst2591 NAND4X2 + PLACED ( 100000 71820 ) N ;   <- pre-placed
    */
    fprintf (fp, "- ");
    prefix->sPrint (buf, 10240);
    global_act->mfprintf (fp, "%s ", buf);
    global_act->mfprintfproc (fp, p);
    fprintf (fp, " ;\n");
  }
}

#ifdef INTEGRATED_PLACER
static void dump_inst_to_ckt (void *x, ActId *prefix, Process *p)
{
  circuit_t *ckt = (circuit_t *)x;
  char buf[10240];
  char mbuf[10240];

  if (p->getprs()) {
    prefix->sPrint (buf, 10240);
    global_act->msnprintf (mbuf, 10240, "%s", buf);
    std::string nm(mbuf);
    
    global_act->msnprintfproc (mbuf, 10240, p);
    std::string btype(mbuf);
    ckt->add_new_block (nm, btype);
  }
}
#endif

static void dump_nets (void *x, ActId *prefix, Process *p)
{
  FILE *fp = (FILE *)x;
  char buf[10240];

  if (p->getprs()) {
    prefix->sPrint (buf, 10240);
    global_act->mfprintf (fp, "%s ", buf);
    global_act->mfprintfproc (fp, p);
    fprintf (fp, " ;\n");
  }
}

ActApplyPass *gapply = NULL;

static void emit_def (Act *a, Process *p, circuit_t *ckt, char *proc_name, char *defname)
{
  FILE *fp;

  global_act = a;
  
  fp = fopen (defname, "w");
  if (!fp) {
    fatal_error ("Could not open file `%s' for writing", defname);
  }
  def_header (fp, ckt, p);
  
  
  double unit_conv;

  /* collect process info */
  gapply->setCookie (fp);
  gapply->setInstFn (count_inst);
  gapply->run (p);

  /* add white space */
  total_area *= 2.0; // 1.7;

  /* make it roughly square */
  total_area = sqrt (total_area);

  unit_conv = Technology::T->scale*MICRON_CONVERSION/1000.0;
  
  /*--- DIE AREA ---*/
  total_area = total_area*unit_conv;

  int pitchx, pitchy, track_gap;
  int nx, ny;

  pitchx = Technology::T->metal[1]->getPitch();
  pitchy = Technology::T->metal[0]->getPitch();

  pitchx = pitchx*unit_conv;
  pitchy = pitchy*unit_conv;
  
  track_gap = pitchy*TRACK_CONVERSION;

  nx = (total_area + pitchx - 1)/pitchx;
  ny = (total_area + track_gap - 1)/track_gap;

  fprintf (fp, "DIEAREA ( %d %d ) ( %d %d ) ;\n",
	   10*pitchx, track_gap,
	   (10+nx)*pitchx, (1+ny)*track_gap);

  /*-- variables in units of pitch --*/
#ifdef INTEGRATED_PLACER
  if (ckt) {
    ckt->def_left = 10;
    ckt->def_bottom = track_gap/pitchy;

    ckt->def_right = (10+nx);
    ckt->def_top = (1+ny)*track_gap/pitchy;
  }
#endif
  
  fprintf (fp, "\nROW CORE_ROW_0 CoreSite %d %d N DO %d BY 1 STEP %d 0 ;\n\n",
	   10*pitchx, pitchy, ny, track_gap);

  /* routing tracks: metal1, metal2 */

  for (int i=0; i < Technology::T->nmetals; i++) {
    RoutingMat *mx = Technology::T->metal[i];
    int pitchxy = mx->getPitch()*unit_conv;
    int startxy = mx->minWidth()*unit_conv/2;
    
    int ntracksx = (pitchx*nx)/pitchxy;
    int ntracksy = (track_gap*ny)/pitchxy;

    /* vertical tracks */
    fprintf (fp, "TRACKS X %d DO %d STEP %d LAYER %s ;\n",
	     10*pitchx + startxy, ntracksx, pitchxy, mx->getName());
    /* horizontal tracks */
    fprintf (fp, "TRACKS Y %d DO %d STEP %d LAYER %s ;\n",
	     track_gap + startxy, ntracksy, pitchxy, mx->getName());

    fprintf (fp, "\n");

  }
  
  /* -- instances  -- */
  fprintf (fp, "COMPONENTS %d ;\n", total_instances);
  gapply->setCookie (fp);
  gapply->setInstFn (dump_inst);
  gapply->run (p);
  //act_flat_apply_processes (a, fp, p, dump_inst);

#ifdef INTEGRATED_PLACER
  /* create circuit instances */
  if (ckt)  {
    //act_flat_apply_processes (a, ckt, p, dump_inst_to_ckt);
    gapply->setCookie (ckt);
    gapply->setInstFn (dump_inst_to_ckt);
    gapply->run (p);
  }
#endif  
  
  fprintf (fp, "END COMPONENTS\n\n");

  /* -- no I/O constraints -- */
  fprintf (fp, "PINS 0 ;\n");
  fprintf (fp, "END PINS\n\n");

  /* -- emit net info -- */

  netcount = 0;
  unsigned long pos = 0;

  /* -- nets -- */
  pos = ftell (fp);
  fprintf (fp, "NETS %012lu ;\n", netcount);
  /*
- net1237
  ( inst5638 A ) ( inst4678 Y )
 ;
  */

  _collect_emit_nets (a, NULL, p, fp, ckt);

  
  fprintf (fp, "END NETS\n\n");
  fprintf (fp, "END DESIGN\n");
  fclose (fp);


  fp = fopen (defname, "r+");
  fseek (fp, pos, SEEK_SET);
  fprintf (fp, "NETS %12lu ;\n", netcount);
  fclose (fp);
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
  int do_place = 0;
  
  Act::Init (&argc, &argv);
  config_read ("prs2net.conf");
  Layout::Init();
  
  if (Technology::T->nmetals < 2) {
    fatal_error ("Can't handle a process with fewer than two metal layers!");
  }

  while ((ch = getopt (argc, argv, "p:l:d:s:P")) != -1) {
    switch (ch) {
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
  a->Expand();

  gapply = new ActApplyPass (a);

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

  /*--- print out lef file, plus rectangles ---*/

#ifdef INTEGRATED_PLACER
  circuit_t ckt;
#endif  
  
  fp = fopen (lefname, "w");
  if (!fp) {
    fatal_error ("Could not open file `%s' for writing", lefname);
  }
  lp->emitLEFHeader (fp);

  FILE *xfp = fopen ("test.lef", "w");
  lp->emitLEFHeader (xfp);

  ActTypeiter it(cell_ns);

  procmap = new std::map<Process *, process_aux *>();
  total_area = 0.0;
  
  for (it = it.begin(); it != it.end(); it++) {
    Type *u = (*it);
    Process *p = dynamic_cast<Process *>(u);
    process_aux *px;
    
    if (!p) continue;
    if (!p->isCell()) continue;
    if (!p->isExpanded()) continue;
    netlist_t *N = netinfo->getNL (p);
    Assert (N, "Hmm...");

    list_t *l = stkp->getStacks (p);

    FILE *tfp;

    tfp = fopen (p->getName(), "w");
    lp->emitRect (tfp, p);
    fclose (tfp);

    lp->emitLEF (xfp, p);

    px = new process_aux();
    px->p = p;

    (*procmap)[p] = px;
    
#ifdef INTEGRATED_PLACER
    if (do_place) {
      geom_create_from_stack (a, fp, &ckt, N, l, &px->x, &px->y);
    }
    else {
      geom_create_from_stack (a, fp, NULL, N, l, &px->x, &px->y);
    }      
#else
    geom_create_from_stack (a, fp, NULL, N, l, &px->x, &px->y);
#endif
    fprintf (fp, "\n");

#if 0
    /* tracks */
    int ctracks = Technology::T->metal[0]->getPitch();
    printf ("Cell height: %d tracks\n", px->y/ctracks);
#endif
  }
  fclose (fp);
  fclose (xfp);

  boolinfo->createNets (p);

  /*--- print out def file ---*/
#ifdef INTEGRATED_PLACER
  if (do_place) {
    emit_def (a, p, &ckt, proc_name, defname);
  }
  else {
    emit_def (a, p, NULL, proc_name, defname);
  }
#else  
  emit_def (a, p, NULL, proc_name, defname);
#endif  

  delete procmap;

  // run placement!

  if (!do_place) {
    return 0;
  }
  
#ifdef INTEGRATED_PLACER
  placer_t *placer = new placer_al_t;
  placer->set_input_circuit (&ckt);
  placer->set_boundary(ckt.def_left, ckt.def_right, ckt.def_bottom,ckt.def_top);
  placer->start_placement ();
  placer->report_placement_result ();

  std::string defs(defname);
  std::string defsp = defs + ".p";
  ckt.save_DEF (defsp,defname);

  delete placer;
#endif  
  
  return 0;
}
