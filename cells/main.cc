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

#include <act/passes.h>
#include <act/passes/netlist.h>
#include <act/layout/geom.h>
#include <act/iter.h>
#include <hash.h>

#include "stacks.h"

static Act *global_act;


struct def_fmt_nets {
  act_connection *netname;	/* the net itself */
  list_t *pins; 		/* inst ; pin sequence */

  int Print (Act *a, FILE *fp, ActId *prefix, int debug = 0) {
    char buf[10240];

    if (!pins || list_length (pins) < 4) return 0;
    
    fprintf (fp, "- ");

    if (prefix) {
      prefix->Print (fp);
      fprintf (fp, ".");
    }

    ActId *tmp = netname->toid();
    tmp->Print (fp);
    delete tmp;

    if (debug) { fprintf (fp, " [%d]", portid); }
    
    fprintf (fp, "\n  ");

    listitem_t *li;
    for (li = list_first (pins); li; li = list_next (li)) {
      char *x = (char *)list_value (li);
      fprintf (fp, " ( ");
      if (prefix) {
	prefix->sPrint (buf, 10240);
	a->mfprintf (fp, "%s.", buf);
      }
      a->mfprintf (fp, "%s ", x);
      li = list_next (li);
      act_connection *c = (act_connection *) list_value (li);
      tmp = c->toid();
      tmp->sPrint (buf, 10240);
      a->mfprintf (fp, "%s", buf);
      fprintf (fp, " )");
      delete tmp;
    }
    fprintf (fp, "\n;\n");
    return 1;
  }
    

  void addPin (const char *name /* inst name */,  act_connection *pin) {
    if (!pins) {
      pins = list_new ();
    }
    list_append (pins, name);
    list_append (pins, pin);
  }

  void importPins (const char *name, def_fmt_nets *sub) {
    listitem_t *li;
    if (!sub->pins) return;
    for (li = list_first (sub->pins); li; li = list_next (li)) {
      char *tmp = (char *)list_value (li);

      char *res;
      MALLOC (res, char, strlen (name) + strlen (tmp) + 2);
      snprintf (res, strlen (name) + strlen (tmp) + 2, "%s.%s", name, tmp);

      li = list_next (li);
      act_connection *pin = (act_connection *) list_value (li);

      addPin (res,pin);
    }
  }
  int portid;
  int idx;
}; 

struct process_aux {

  process_aux() {
    p = NULL;
    x = 0;
    y = 0;
    visited = 0;
    n = NULL;
    portmap = NULL;
    iH = NULL;
    A_INIT (nets);
  }

  int ismyport (act_connection *c) {
    for (int i=0; i < A_LEN (n->bN->ports); i++) {
      if (n->bN->ports[i].omit) continue;
      if (c == n->bN->ports[i].c) return i;
    }
    return -1;
  }

  def_fmt_nets *addNet (act_connection *c) {
    def_fmt_nets *r;
    ihash_bucket_t *b;

    if (!iH) {
      iH = ihash_new (8);
    }

    b = ihash_lookup (iH, (long)c);
    if (b) {
      return &nets[b->i];
    }
    b = ihash_add (iH, (long)c);
    A_NEW (nets, def_fmt_nets);
    b->i = A_LEN (nets);
    A_NEXT (nets).idx = b->i;
    A_NEXT (nets).netname = c;
    A_NEXT (nets).pins = NULL;
    A_NEXT (nets).portid = ismyport (c);
    r = &A_NEXT (nets);
    A_INC (nets);
    return r;
  }

  struct iHashtable *iH;
  
  Process *p; 			/* sanity */
  int x, y;			/* size of component */

  netlist_t *n;

  /*-- local nets --*/
  int visited;
  int nnets;
  int *portmap;			/* maps ports to nets */

  A_DECL (def_fmt_nets, nets);	/* the nets */
  
  /*-- port nets --*/
  
};

#define MICRON_CONVERSION 2000
#define TRACK_CONVERSION 12

static std::map<Process *, process_aux *> *procmap = NULL;


static std::map<Process *, netlist_t *> *netmap = NULL;


void _collect_nets (Act *a, Process *p)
{
  process_aux *px;
  Assert (p->isExpanded(), "What are we doing");

  if (procmap->find (p) == procmap->end()) {
    /* this is an instance with no circuits! */
    px = new process_aux();
    px->p = p;
    (*procmap)[p] = px;
  }
  else {
    px = procmap->find(p)->second;
  }
  if (px->visited) return;
  px->visited = 1;

  if (netmap->find(p) == netmap->end()) {
    fatal_error ("Could not find netlist for `%s'", p->getName());
  }
  netlist_t *n = netmap->find (p)->second;
  px->n = n;

  if (A_LEN (n->bN->ports) > 0) {
    MALLOC (px->portmap, int, A_LEN (n->bN->ports));
    for (int i = 0; i < A_LEN (n->bN->ports); i++) {
      px->portmap[i] = -1;
    }
  }

  ActInstiter i(p->CurScope());

  for (i = i.begin(); i != i.end(); i++) {
    ValueIdx *vx = (*i);
    if (!TypeFactory::isProcessType (vx->t)) continue;
    _collect_nets (a, dynamic_cast<Process *>(vx->t->BaseType ()));
  }

  int iport = 0;
  for (i = i.begin(); i != i.end(); i++) {
    ValueIdx *vx = (*i);
    if (!TypeFactory::isProcessType (vx->t)) continue;

    process_aux *sub;
    Process *instproc = dynamic_cast<Process *>(vx->t->BaseType ());
    int ports_exist;

    if (procmap->find (instproc) == procmap->end()) {
      fatal_error ("Error looking for instproc %s!\n", instproc->getName());
    }

    sub = procmap->find (instproc)->second;
    Assert (sub->n, "What?");

    ports_exist = 0;
    for (int i=0; i < A_LEN (sub->n->bN->ports); i++) {
      if (sub->n->bN->ports[i].omit == 0) {
	ports_exist = 1;
	break;
      }
    }

    if (!ports_exist) continue;

    if (vx->t->arrayInfo()) {
      Arraystep *as = vx->t->arrayInfo()->stepper();
      while (!as->isend()) {
	for (int i=0; i < A_LEN (sub->n->bN->ports); i++) {
	  if (sub->n->bN->ports[i].omit) continue;

	  def_fmt_nets *d;
	  char *tmp;
	  char *tmp2;

	  d = px->addNet (n->bN->instports[iport]);

	  tmp2 = as->string();
	  MALLOC (tmp, char, strlen (vx->getName()) + strlen (tmp2) + 1);
	  sprintf (tmp, "%s%s", vx->getName(), tmp2);
	  FREE (tmp2);

	  if (sub->portmap[i] == -1) {
	    /* there are no nets associated with this port in the type
	       of this instance */
	    /* this port is not connected to anything else; we just need
	       a generic new net that looks like <inst>.<port> */
	    d->addPin (tmp, sub->n->bN->ports[i].c);
	  }
	  else {
	    /* we need to copy the net up, and update all the instance names! */
	    d->importPins (tmp, &sub->nets[sub->portmap[i]]);
	    FREE (tmp);
	  }
	  int pid = px->ismyport (n->bN->instports[iport]);
	  if (pid != -1) {
	    /* this is a port pin! */
	    px->portmap[pid] = d->idx;
	  }
	  iport++;
	}
	as->step();
      }
      delete as;
    }
    else {
      for (int i=0; i < A_LEN (sub->n->bN->ports); i++) {
	if (sub->n->bN->ports[i].omit) continue;
	def_fmt_nets *d;

	d = px->addNet (n->bN->instports[iport]);

	if (sub->portmap[i] == -1) {
	  /* there are no nets associated with this port in the type
	     of this instance */
	  /* this port is not connected to anything else; we just need
	     a generic new net that looks like <inst>.<port> */
	  d->addPin (vx->getName(), sub->n->bN->ports[i].c);
	}
	else {
	  /* we need to copy the net up, and update all the instance names! */
	  d->importPins (vx->getName(), &sub->nets[sub->portmap[i]]);
	}
	int pid = px->ismyport (n->bN->instports[iport]);
	if (pid != -1) {
	  /* this is a port pin! */
	  px->portmap[pid] = d->idx;
	}
	iport++;
      }
    }
  }
  Assert (iport == A_LEN (n->bN->instports), "What?!");
  return;
}

void _dump_collection (Act *a, Process *p)
{
  process_aux *px;
  netlist_t *n;
  Assert (p->isExpanded(), "What are we doing");

  px = procmap->find(p)->second;
  if (!px->visited) return;
  px->visited = 0;

  printf ("Process %s\n", p->getName());
  for (int i=0; i < A_LEN (px->nets); i++) {
    px->nets[i].Print (a, stdout, NULL, 1);
  }
  printf ("\n");

  ActInstiter i(p->CurScope());

  for (i = i.begin(); i != i.end(); i++) {
    ValueIdx *vx = (*i);
    if (!TypeFactory::isProcessType (vx->t)) continue;
    _dump_collection (a, dynamic_cast<Process *>(vx->t->BaseType ()));
  }
  
  return;
}



static unsigned long netcount = 0;


void _collect_emit_nets (Act *a, ActId *prefix, Process *p, FILE *fp)
{
  process_aux *px;
  Assert (p->isExpanded(), "What are we doing");

  if (procmap->find (p) == procmap->end()) {
    fatal_error ("Could not find netlist for `%s'", p->getName());
  }
  px = procmap->find(p)->second;

  /* first, print my local nets */
  for (int i=0; i < A_LEN (px->nets); i++) {
    if (px->nets[i].portid == -1) {
      if (px->nets[i].Print (a, fp, prefix)) {
	netcount++;
      }
    }
  }

  ActInstiter i(p->CurScope());

  for (i = i.begin(); i != i.end(); i++) {
    ValueIdx *vx = (*i);
    if (!TypeFactory::isProcessType (vx->t)) continue;

    ActId *newid;
    ActId *cpy;
    
    process_aux *sub;
    Process *instproc = dynamic_cast<Process *>(vx->t->BaseType ());
    int ports_exist;

    if (procmap->find (instproc) == procmap->end()) {
      fatal_error ("Error looking for instproc %s!\n", instproc->getName());
    }

    sub = procmap->find (instproc)->second;
    Assert (sub->n, "What?");

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
	_collect_emit_nets (a, cpy, instproc, fp);
	delete x;
	newid->setArray (NULL);
	as->step();
      }
      delete as;
    }
    else {
      _collect_emit_nets (a, cpy, instproc, fp);
    }
    delete cpy;
  }
  return;
}



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
  fprintf (stderr, "Usage: %s -p procname -l lefname -d defname [-s spicename] <file.act>\n", name);
  exit (1);
}


static void lef_header (FILE *fp)
{
  /* -- lef header -- */
  fprintf (fp, "VERSION 5.8 ;\n\n");
  fprintf (fp, "BUSBITCHARS \"[]\" ;\n\n");
  fprintf (fp, "DIVIDERCHAR \"/\" ;\n\n");
  fprintf (fp, "UNITS\n");
  fprintf (fp, "    DATABASE MICRONS %d ;\n", MICRON_CONVERSION);
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
  fprintf (fp, "\nUNITS DISTANCE MICRONS %d ;\n\n", MICRON_CONVERSION);
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
    fprintf (fp, "- ");
    prefix->sPrint (buf, 10240);
    global_act->mfprintf (fp, "%s ", buf);
    print_procname (global_act, p, fp);
    fprintf (fp, " ;\n");
  }
}

static void dump_nets (void *x, ActId *prefix, Process *p)
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



static void emit_def (Act *a, Process *p, char *proc_name, char *defname)
{
  FILE *fp;
  
  fp = fopen (defname, "w");
  if (!fp) {
    fatal_error ("Could not open file `%s' for writing", defname);
  }
  def_header (fp, proc_name);
  
  global_act = a;
  
  double unit_conv;

  /* collect process info */
  act_flat_apply_processes (a, fp, p, count_inst);

  /* add white space */
  total_area /= 0.60;

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
  act_flat_apply_processes (a, fp, p, dump_inst);
  /* FORMAT: 
         inst2591 NAND4X2 ;

         inst2591 NAND4X2 + PLACED ( 100000 71820 ) N ;   <- pre-placed
  */
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

  _collect_nets (a, p);

  //_dump_collection (p);
  
  _collect_emit_nets (a, NULL, p, fp);
  
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
  
  Act::Init (&argc, &argv);
  Layout::Init();
  
  if (Technology::T->nmetals < 2) {
    fatal_error ("Can't handle a process with fewer than two metal layers!");
  }

  while ((ch = getopt (argc, argv, "p:l:d:s:")) != -1) {
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

  if (spice) {
    FILE *sp = fopen (spice, "w");
    if (!sp) { fatal_error ("Could not open file `%s'", spice); }
    act_emit_netlist (a, p, sp);
    fclose (sp);
  }
  //a->Print (stdout);
  
  /*--- create stacks for all cells ---*/
  netmap = (std::map<Process *, netlist_t *> *) a->aux_find ("prs2net");

  ActNamespace *cell_ns = a->findNamespace ("cell");
  Assert (cell_ns, "No cell namespace? No circuits?!");

  /*--- print out lef file, plus rectangles ---*/
  fp = fopen (lefname, "w");
  if (!fp) {
    fatal_error ("Could not open file `%s' for writing", lefname);
  }
  lef_header (fp);

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
    if (netmap->find (p) == netmap->end()) continue;
    netlist_t *N = netmap->find (p)->second;
    Assert (N, "Hmm...");

    list_t *l = stacks_create (N);

    px = new process_aux();
    px->p = p;

    (*procmap)[p] = px;
    
    geom_create_from_stack (a, fp, N, l, &px->x, &px->y);
    fprintf (fp, "\n");
  }
  fclose (fp);


  /*--- print out def file ---*/
  emit_def (a, p, proc_name, defname);

  delete procmap;
  
  return 0;
}
