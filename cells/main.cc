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
#include <config.h>

#include "stacks.h"

#ifdef INTEGRATED_PLACER
#include "placer.h"
#include "placeral.h"
#include "placerdla.h"
#endif

static Act *global_act;


struct def_fmt_nets {
  act_connection *netname;	/* the net itself */
  list_t *pins; 		/* inst ; pin sequence */
  int skip;

  int Print (Act *a, FILE *fp, ActId *prefix, int debug = 0) {
    char buf[10240];

    if (skip || !pins || list_length (pins) < 4) return 0;
    
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

#ifdef INTEGRATED_PLACER
  int AddCkt (Act *a, circuit_t *ckt, ActId *prefix) {
    char buf[10240];
    char mbuf[10240];
    int len = 0;

    if (skip || !pins || list_length (pins) < 4) return 0;
    if (!ckt) return 0;
    
    if (prefix) {
      prefix->sPrint (buf, 10240);
      strcat (buf, ".");
    }
    else {
      buf[0] = '\0';
    }

    len = strlen (buf);

    ActId *tmp = netname->toid();
    tmp->sPrint (buf+len, 1024-len);
    delete tmp;
    len += strlen (buf+len);

    std::string netstr(buf);

    if (netname->isglobal()) {
      // globals are cheap!
      ckt->create_blank_net (netstr, 1e-6);
    }
    else {
      ckt->create_blank_net (netstr, 1.0); // WEIGHT GOES HERE!
    }

    listitem_t *li;
    for (li = list_first (pins); li; li = list_next (li)) {
      char *x = (char *)list_value (li);
      // block name is:
      //    prefix + x

      if (prefix) {
	prefix->sPrint (buf, 10240);
	a->msnprintf (mbuf, 10240, "%s.%s", buf, x);
      }
      else {
	a->msnprintf (mbuf, 10240, "%s", x);
      }

      std::string blkname(mbuf);

      li = list_next (li);
      act_connection *c = (act_connection *) list_value (li);
      tmp = c->toid();
      tmp->sPrint (buf, 10240);
      a->msnprintf (mbuf, 10240, "%s", buf);
      delete tmp;

      std::string pinname(mbuf);

      ckt->add_pin_to_net (netstr, blkname, pinname);
    }
    return 1;
  }
#endif  
  

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
    A_INIT (global_nets);
  }

  int ismyport (act_connection *c) {
    for (int i=0; i < A_LEN (n->bN->ports); i++) {
      if (n->bN->ports[i].omit) continue;
      if (c == n->bN->ports[i].c) return i;
    }
    return -1;
  }

  /* create a new net, based on this connection pointer */
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
    A_NEXT (nets).skip = 0;
    A_NEXT (nets).portid = ismyport (c);
    if (c->isglobal()) {
      A_NEWM (global_nets, int);
      A_NEXT (global_nets) = b->i;
      A_INC (global_nets);
    }
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

  /*-- global nets --*/
  A_DECL (int, global_nets);
  
};

#define MICRON_CONVERSION 2000
#define TRACK_CONVERSION 18

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
	def_fmt_nets *d;
	char *tmp;
	char *tmp2;
	
	for (int i=0; i < A_LEN (sub->n->bN->ports); i++) {
	  if (sub->n->bN->ports[i].omit) continue;


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

	/*-- global nets are like an implicit pin --*/
	for (int i=0; i < A_LEN (sub->global_nets); i++) {
	  if (sub->global_nets[i] != -1) {
	    d = px->addNet (sub->nets[sub->global_nets[i]].netname);
	    tmp2 = as->string();
	    MALLOC (tmp, char, strlen (vx->getName()) + strlen (tmp2) + 1);
	    sprintf (tmp, "%s%s", vx->getName(), tmp2);
	    FREE (tmp2);

	    d->importPins (tmp, &sub->nets[sub->global_nets[i]]);
	    sub->nets[sub->global_nets[i]].skip = 1;
	    FREE (tmp);
	  }
	}
	
	as->step();
      }
      delete as;
    }
    else {
      def_fmt_nets *d;
      
      for (int i=0; i < A_LEN (sub->n->bN->ports); i++) {
	if (sub->n->bN->ports[i].omit) continue;

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

      for (int i=0; i < A_LEN (sub->global_nets); i++) {
	if (sub->global_nets[i] != -1) {
	  d = px->addNet (sub->nets[sub->global_nets[i]].netname);
	  d->importPins (vx->getName(), &sub->nets[sub->global_nets[i]]);
	  sub->nets[sub->global_nets[i]].skip = 1;
	}
      }
    }
  }
  Assert (iport == A_LEN (n->bN->instports), "What?!");

  /* if a global net is also a port, then get rid of it from the
     global net list */
  for (int i=0; i < A_LEN (px->global_nets); i++) {
    for (int j=0; j < A_LEN (px->n->bN->ports); j++) {
      if (px->n->bN->ports[j].omit) continue;
      if (px->portmap[j] == px->global_nets[i]) {
	px->global_nets[i] = -1;
      }
    }
  }
  
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


void _collect_emit_nets (Act *a, ActId *prefix, Process *p, FILE *fp,
			 circuit_t *ckt)
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
#ifdef INTEGRATED_PLACER
      px->nets[i].AddCkt (a, ckt, prefix);
#endif      
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


static void lef_header (FILE *fp, circuit_t *ckt)
{
  /* -- lef header -- */
  fprintf (fp, "VERSION 5.8 ;\n\n");
  fprintf (fp, "BUSBITCHARS \"[]\" ;\n\n");
  fprintf (fp, "DIVIDERCHAR \"/\" ;\n\n");
  fprintf (fp, "UNITS\n");
  
  fprintf (fp, "    DATABASE MICRONS %d ;\n", MICRON_CONVERSION);

#ifdef INTEGRATED_PLACER
  if (ckt)
  ckt->lef_database_microns = MICRON_CONVERSION;
#endif  
  
  fprintf (fp, "END UNITS\n\n");

  fprintf (fp, "MANUFACTURINGGRID 0.000500 ; \n\n");
  fprintf (fp, "CLEARANCEMEASURE EUCLIDEAN ; \n\n");
  fprintf (fp, "USEMINSPACING OBS ON ; \n\n");

  fprintf (fp, "SITE CoreSite\n");
  fprintf (fp, "    CLASS CORE ;\n");
  fprintf (fp, "    SIZE 0.2 BY 1.2 ;\n"); /* not used */
  fprintf (fp, "END CoreSite\n\n");
  
  int i;
  double scale = Technology::T->scale/1000.0;
  for (i=0; i < Technology::T->nmetals; i++) {
    RoutingMat *mat = Technology::T->metal[i];
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
    fprintf (fp, "   PITCH %.6f %.6f ;\n",
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

#ifdef INTEGRATED_PLACER
  if (ckt)
  ckt->m2_pitch = Technology::T->metal[1]->getPitch()*scale;
#endif

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
  if (ckt)
  ckt->def_distance_microns = MICRON_CONVERSION;
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
  act_flat_apply_processes (a, fp, p, count_inst);

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
  act_flat_apply_processes (a, fp, p, dump_inst);

#ifdef INTEGRATED_PLACER
  /* create circuit instances */
  if (ckt) 
  act_flat_apply_processes (a, ckt, p, dump_inst_to_ckt);
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

  _collect_nets (a, p);

  //_dump_collection (p);
  
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

#ifdef INTEGRATED_PLACER
  circuit_t ckt;
#endif  
  
  fp = fopen (lefname, "w");
  if (!fp) {
    fatal_error ("Could not open file `%s' for writing", lefname);
  }
#ifdef INTEGRATED_PLACER
  if (do_place) {
    lef_header (fp, &ckt);
  }
  else {
    lef_header (fp, NULL);
  }
#else  
  lef_header (fp, NULL);
#endif  

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
