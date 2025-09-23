/*************************************************************************
 *
 *  Copyright (c) 2019-2021 Rajit Manohar
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
#include <act/act.h>
#include <act/iter.h>
#include <act/passes.h>
#include <math.h>
#include <string.h>
#include "stk_pass.h"
#include "stk_layout.h"

#define IS_METAL_HORIZ(i) ((((i) % 2) == _horiz_metal) ? 1 : 0)

#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

static double manufacturing_grid_in_nm;

static long snap_up (long w, unsigned long pitch)
{
  if (w >= 0) {
    if (w % pitch != 0) {
      w += pitch - (w % pitch);
    }
  }
  else {
    w = -w;
    if (w % pitch != 0) {
      w += pitch - (w % pitch);
      w = w - pitch;
    }
    w = -w;
  }
  return w;
}

static long snap_dn (long w, unsigned long pitch)
{
  if (w >= 0) {
    if (w % pitch != 0) {
      w -= (w % pitch);
    }
  }
  else {
    w = -w;
    if (w % pitch != 0) {
      w -= (w % pitch);
      w = w + pitch;
    }
    w = -w;
  }
  return w;
}

long ActStackLayout::snap_up_x (long w)
{
  return snap_up (w, _m_align_x->getPitch());
}

long ActStackLayout::snap_dn_x (long w)
{
  return snap_dn (w, _m_align_x->getPitch());
}

long ActStackLayout::snap_up_y (long w)
{
  return snap_up (w, _m_align_y->getPitch());
}

long ActStackLayout::snap_dn_y (long w)
{
  return snap_dn (w, _m_align_y->getPitch());
}

void layout_init (ActPass *a)
{
  ActDynamicPass *dp = dynamic_cast<ActDynamicPass *> (a);

#if 0
  printf ("[lay] cur state: %p\n", config_get_state ());
  printf ("[lay] set to: %p\n", dp->getConfig());
#endif

  ActNamespace::setAct (a->getAct());

  Assert (dp, "What?");
#if 1
  config_set_state (dp->getConfig ());
  Technology::T = dp->getTech ();
#endif
  
  Layout::Init ();

  if (dp->getPtrParam ("raw")) {
    warning ("layout_init(): Layout pass already created. Skipping.");
    return;
  }

  if (!a->getAct()->pass_find ("net2stk")) {
    new ActDynamicPass (a->getAct(), "net2stk", "pass_stk.so", "stk");
  }
  dp->addDependency ("net2stk");
  dp->setParam ("raw", (void *)new ActStackLayout(a));
}

ActStackLayout::ActStackLayout (ActPass *ap) 
{
  me = ap;
  a = ap->getAct();
  boxH = NULL;

  ActPass *pass = a->pass_find ("net2stk");
  Assert (pass, "Hmm...");
  stk = dynamic_cast<ActDynamicPass *>(pass);
  Assert (stk, "Hmm too...");

  pass = a->pass_find ("prs2net");
  Assert (pass, "Missing netlist pass...");
  nl = dynamic_cast<ActNetlistPass *> (pass);
  Assert (nl, "What?");

  _total_area = -1;
  _total_stdcell_area = -1;
  _total_instances = -1;
  _maxht = -1;
  _ymin = 0;
  _ymax = 0;

  double net_lambda;
  net_lambda = config_get_real ("net.lambda");
  lambda_to_scale = (int)(net_lambda*1e9/Technology::T->scale + 0.5);

  if (fabs(lambda_to_scale*Technology::T->scale - net_lambda*1e9) > 0.001) {
    warning ("Lambda (%g) and technology scale factor (%g) are not integer multiples; rounding down", net_lambda, Technology::T->scale);
  }

  wellplugs = NULL;
  dummy_netlist = NULL;

  /* more parameters */
  if (config_exists ("lefdef.version")) {
    _version = config_get_string ("lefdef.version");
  }
  else {
    _version = "5.8";
  }
  if (config_exists ("lefdef.micron_conversion")) {
    _micron_conv = config_get_int ("lefdef.micron_conversion");
  }
  else {
    _micron_conv = 2000;
  }
  
  if (config_exists ("lefdef.manufacturing_grid")) {
    _manufacturing_grid = config_get_real ("lefdef.manufacturing_grid");
  }
  else {
    _manufacturing_grid = 0.0005;
  }
  manufacturing_grid_in_nm = _manufacturing_grid*1e3;

  int x_align;
  int v;
  if (config_exists ("lefdef.metal_align.x_dim")) {
    v = config_get_int ("lefdef.metal_align.x_dim");
  }
  else {
    v = 2;
  }
  if (v < 1 || v > Technology::T->nmetals) {
    fatal_error ("lefdef.metal_align.x_dim (%d) is out of range (max %d)",
		 v, Technology::T->nmetals);
  }
  _m_align_x = Technology::T->metal[v-1];
  x_align = v-1;

  if (config_exists ("lefdef.metal_align.y_dim")) {
    v = config_get_int ("lefdef.metal_align.y_dim");
  }
  else {
    v = 1;
  }
  if (v < 1 || v > Technology::T->nmetals) {
    fatal_error ("lefdef.metal_align.y_dim (%d) is out of range (max %d)",
		 v, Technology::T->nmetals);
  }
  _m_align_y = Technology::T->metal[v-1];

  if (config_exists ("lefdef.horiz_metal")) {
    _horiz_metal = config_get_int ("lefdef.horiz_metal");
    if (_horiz_metal != 0 && _horiz_metal != 1) {
      fatal_error ("lefdef.horiz_metal: must be 0 or 1");
    }
  }
  else {
    _horiz_metal = 1;
  }

  if (config_exists ("lefdef.pin_layer")) {
    v = config_get_int ("lefdef.pin_layer");
  }
  else {
    v = 2;
  }
  if (v < 1 || v > Technology::T->nmetals) {
    fatal_error ("lefdef.pin_layer (%d) is out of range (max %d)", v, 
		 Technology::T->nmetals);
  }
  _pin_layer = v - 1;
  _pin_metal = Technology::T->metal[v-1];
  
  if (((_pin_layer+1) % 2) == _horiz_metal) {
    if (!config_exists ("lefdef.warnings") ||
	(config_get_int ("lefdef.warnings") == 1)) {
      warning ("lefdef.pin_layer (%d) is a horizontal metal layer.\n\t[default pin locations are in a line at the top/bottom of the cell]", _pin_layer+1);
    }
  }
  if (_pin_metal->getPitch() != _m_align_x->getPitch()) {
    if (!config_exists ("lefdef.warnings") ||
	(config_get_int ("lefdef.warnings") == 1)) {
      warning ("Pin metal (%d) and x-alignment metal (%d) have different pitches"
	       "\n\tpin metal: %d; x-alignment: %d (using x-alignment pitch)",
	       _pin_layer + 1, x_align + 1,
	       _pin_metal->getPitch(), _m_align_x->getPitch());
	     
      if (_pin_metal->getPitch() < _m_align_x->getPitch()) {
	fprintf (stderr, "\tpins may not be on the pin metal pitch.\n");
      }
      else {
	fprintf (stderr, "\tgeneric pins might violate spacing rules.\n");
      }
    }
  }

  if (config_exists ("lefdef.rect_import")) {
    _rect_import = config_get_int ("lefdef.rect_import");
    if (_rect_import != 0 && _rect_import != 1) {
      fatal_error ("lefdef.rect_import: must be 0 or 1");
    }
  }
  else {
    _rect_import = 0;
  }

  _rect_inpath = NULL;
  if (_rect_import) {
    if (config_exists ("lefdef.rect_inpath")) {
      _rect_inpath = path_init ();
      path_add (_rect_inpath, config_get_string ("lefdef.rect_inpath"));
    }
  }

  if (config_exists ("lefdef.rect_outdir")) {
    _rect_outdir = config_get_string ("lefdef.rect_outdir");
  }
  else {
    _rect_outdir = NULL;
  }

  if (config_exists ("lefdef.rect_outinitdir")) {
    _rect_outinitdir = config_get_string ("lefdef.rect_outinitdir");
  }
  else {
    if (_rect_outdir) {
      _rect_outinitdir = _rect_outdir;
    }
    else {
      _rect_outinitdir = NULL;
    }
  }

  if (config_exists ("lefdef.rect_wells")) {
    _rect_wells = config_get_int ("lefdef.rect_wells");
    if (_rect_wells != 0 && _rect_wells != 1) {
      fatal_error ("lefdef.rect_wells: must be 0 or 1");
    }
  }
  else {
    _rect_wells = 0;
  }

  if (config_exists ("lefdef.extra_tracks.top")) {
    _extra_tracks_top = config_get_int ("lefdef.extra_tracks.top");
  }
  else {
    _extra_tracks_top = 0;
  }
  if (config_exists ("lefdef.extra_tracks.bot")) {
    _extra_tracks_bot = config_get_int ("lefdef.extra_tracks.bot");
  }
  else {
    _extra_tracks_bot = 0;
  }
  if (config_exists ("lefdef.extra_tracks.left")) {
    _extra_tracks_left = config_get_int ("lefdef.extra_tracks.left");
  }
  else {
    _extra_tracks_left = 0;
  }
  if (config_exists ("lefdef.extra_tracks.right")) {
    _extra_tracks_right = config_get_int ("lefdef.extra_tracks.right");
  }
  else {
    _extra_tracks_right = 0;
  }

  _lef_header = 0;
  _cell_header = 0;
  _fp = NULL;
  _fpcell = NULL;
}

#define EDGE_FLAGS_LEFT 0x1
#define EDGE_FLAGS_RIGHT 0x2

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

struct BBox {
  int flavor;
  struct {
    int llx, lly, urx, ury;
  } p, n;
};


/* calculate actual edge width */
static int getwidth (int idx, edge_t *e)
{
  return EDGE_WIDTH (e,idx)*manufacturing_grid_in_nm/Technology::T->scale;
}

/* actual edge length */
static int getlength (edge_t *e, double adj)
{
  static int min_length = -1;
  if (min_length == -1) {
    min_length = config_get_int ("net.min_length") *
      ActNetlistPass::getGridsPerLambda();
  }
  if (e->l != min_length) {
    adj = 0;
  }
  return (e->l*manufacturing_grid_in_nm+adj*1e9)/Technology::T->scale;
}


static void update_bbox (BBox *cur, int type, int x, int y, int rx, int ry)
{
  if (type == EDGE_PFET) {				
    if (cur->p.llx >= cur->p.urx || cur->p.lly >= cur->p.ury) {	
      cur->p.llx = MIN(x,rx);				
      cur->p.lly = MIN(y,ry);				
      cur->p.urx = MAX(x,rx);				
      cur->p.ury = MAX(y,ry);				
    }							
    else {						
      cur->p.llx = MIN(cur->p.llx,x);			
      cur->p.llx = MIN(cur->p.llx,rx);			
      cur->p.lly = MIN(cur->p.lly,y);			
      cur->p.lly = MIN(cur->p.lly,ry);			
      cur->p.urx = MAX(cur->p.urx,x);			
      cur->p.urx = MAX(cur->p.urx,rx);			
      cur->p.ury = MAX(cur->p.ury,y);			
      cur->p.ury = MAX(cur->p.ury,ry);			
    }							
  } else {						
    if (cur->n.llx >= cur->n.urx || cur->n.lly >= cur->n.ury) {	
      cur->n.llx = MIN(x,rx);				
      cur->n.lly = MIN(y,ry);				
      cur->n.urx = MAX(x,rx);				
      cur->n.ury = MAX(y,ry);				
    }							
    else {						
      cur->n.llx = MIN(cur->n.llx,x);			
      cur->n.llx = MIN(cur->n.llx,rx);			
      cur->n.lly = MIN(cur->n.lly,y);			
      cur->n.lly = MIN(cur->n.lly,ry);			
      cur->n.urx = MAX(cur->n.urx,x);			
      cur->n.urx = MAX(cur->n.urx,rx);			
      cur->n.ury = MAX(cur->n.ury,y);			
      cur->n.ury = MAX(cur->n.ury,ry);			
    }							
  }							
}


/*
  emits rectangles needed upto the FET.
  If it is right edge, also emits the final diffusion of the right edge.
*/
static int locate_fetedge (Layout *L, int dx,
			   unsigned int flags,
			   edge_t *prev, int previdx,
			   node_t *left, edge_t *e, int eidx)
{
  DiffMat *d;
  FetMat *f;
  PolyMat *p;
  int rect;
  int fet_type; /* -1 = downward notch, +1 = upward notch, 0 = same
		   width */
  int spc = 0;

  int e_w = getwidth (0, e);
  double la = L->leak_adjust();

  /* XXX: THIS CODE IS COPIED FROM emit_rectangle!!!!! */

  d = L->getDiff (e->type, e->flavor);
  f = L->getFet (e->type, e->flavor);
  p = L->getPoly ();

  if (prev) {
    spc = MAX (f->getSpacing (getlength (prev, la)),
	       p->getSpacing (getlength (prev, la)));
  }
  else {
    spc = 0;
  }
  if (e) {
    spc = MAX (MAX (spc, f->getSpacing (getlength (e, la))),
	       p->getSpacing (getlength (e, la)));
  }
  

  rect = 0;
  if (flags & EDGE_FLAGS_LEFT) {
    fet_type = 0;
    /* actual overhang rule */
    rect = d->effOverhang (e_w, left->contact);
  }
  else {
    Assert (prev, "Hmm");
    int prev_w = getwidth (previdx, prev);

    if (prev_w == e_w) {
      fet_type = 0;
      rect = spc;
      if (left->contact) {
	rect = MAX (rect, d->viaSpaceMid());
      }
    }
    else if (prev_w < e_w) {
      /* upward notch */
      fet_type = 1;
      rect = d->getNotchSpacing();
      if (left->contact) {
	rect = MAX (rect, d->viaSpaceMid() - d->effOverhang (e_w));
      }
      rect = MAX (rect, spc);
    }
    else {
      fet_type = -1;
      rect = d->effOverhang (e_w);
    }
  }

  Assert (rect > 0, "FIX FOR FINFETS!");

  dx += rect;

  if (fet_type != 0) {
    if (fet_type < 0) {
      /* down notch */
      rect = d->getNotchSpacing();
      if (left->contact) {
	rect = MAX (rect, d->viaSpaceMid() - d->effOverhang (e_w));
      }
    }
    else {
      /* up notch */
      rect = d->effOverhang (e_w);
    } 
    dx += rect;
  }

  return dx;
}


/*
  emits rectangles needed upto the FET.
  If it is right edge, also emits the final diffusion of the right edge.
*/
static int emit_rectangle (Layout *L,  /* where we are drawing */
			   
			   int pad,    /* extra x-padding for
					  diffusion, used to line up
					  fets */
			   
			   int dx,     /* (dx,dy) : start coord of diffusion */
			   int dy,
			   
			   unsigned int flags, /* left/right edge? */
			   
			   edge_t *prev,
			   int previdx,    /* previous edge: used to
					      check for notches */
			   
			   node_t *left,   /* left node */

			   edge_t *e, /* edge to draw */
			   edge_t *eopp, /* edge opposing it; used
					    to check poly overhang */

			   int oup, /* amount of space available in
				       y-direction until you encroach
				       on the "other half" of the layout */
			   
			   int eidx, /* finger # */
			   
			   int yup,   /* +1 for p-type, -1 for n-type
					 */
			   
			   BBox *ret /* bounding box */
			   )
{
  DiffMat *d;
  FetMat *f;
  PolyMat *p;
  int rect;
  int fet_type; /* -1 = downward notch, +1 = upward notch, 0 = same
		   width */

  BBox b;

  int e_w = getwidth (eidx, e);
  double la = L->leak_adjust();
  
  if (ret) {
    b = *ret;
  }
  else {
    b.p.llx = 0;
    b.p.lly = 0;
    b.p.urx = 0;
    b.p.ury = 0;
    b.n = b.p;
  }

#define RECT_UPDATE(type,x,y,rx,ry)	update_bbox(&b,type,x,y,rx,ry)

  /* XXX: THIS CODE GETS COPIED TO locate_fetedge!!!! */
  
  d = L->getDiff (e->type, e->flavor);
  f = L->getFet (e->type, e->flavor);
  p = L->getPoly ();
  b.flavor = e->flavor;

  int spc;
  if (prev) {
    spc = MAX (f->getSpacing (getlength (prev, la)),
	       p->getSpacing (getlength (prev, la)));
  }
  else {
    spc = 0;
  }
  if (e) {
    spc = MAX (MAX (spc, f->getSpacing (getlength (e, la))),
	       p->getSpacing (getlength (e, la)));
  }
  

  int prev_w = 0;

  rect = 0;
  if (flags & EDGE_FLAGS_LEFT) {
    fet_type = 0;
    /* actual overhang rule */
    rect = d->effOverhang (e_w, left->contact);
  }
  else {
    Assert (prev, "Hmm");
    prev_w = getwidth (previdx, prev);
    
    if (prev_w == e_w) {
      fet_type = 0;
      rect = spc;
      if (left->contact) {
	rect = MAX (rect, d->viaSpaceMid());
      }
    }
    else if (prev_w < e_w) {
      /* upward notch */
      fet_type = 1;
      rect = d->getNotchSpacing();
      if (left->contact) {
	rect = MAX (rect, d->viaSpaceMid() - d->effOverhang (e_w));
      }
      rect = MAX (rect, spc);
    }
    else {
      /* downward step */
      fet_type = -1;
      rect = d->effOverhang (e_w);
    }
  }

  Assert (rect > 0, "FIX FOR FINFETS!");

  if (fet_type != -1) {
    rect += pad;
    pad = 0;
  }

  if (fet_type == 0) {
    if (yup < 0) {
      L->DrawDiff (e->flavor, e->type, dx, dy + yup*e_w, rect, -yup*e_w,
		   left->contact ? left : NULL);
    }
    else {
      L->DrawDiff (e->flavor, e->type, dx, dy, rect, yup*e_w,
		   left->contact ? left : NULL);
    }
    RECT_UPDATE(e->type, dx, dy, dx+rect, dy + yup*e_w);
  }
  else {
    if (yup < 0) {
      L->DrawDiff (e->flavor, e->type, dx, dy + yup*prev_w, rect, -yup*prev_w,
		   left->contact ? left : NULL);
    }
    else {
      L->DrawDiff (e->flavor, e->type, dx, dy, rect, yup*prev_w,
		   left->contact ? left : NULL);
    }
    RECT_UPDATE(e->type, dx,dy,dx+rect, dy + yup*prev_w);
  }
  dx += rect;

  if (fet_type != 0) {
    if (fet_type < 0) {
      /* down notch */
      rect = d->getNotchSpacing();
      if (left->contact) {
	rect = MAX (rect, d->viaSpaceMid() - d->effOverhang (e_w));
      }
    }
    else {
      /* up notch */
      rect = d->effOverhang (e_w);
    }
    rect += pad;
    pad = 0;
    if (yup < 0) {
      L->DrawDiff (e->flavor, e->type, dx, dy + yup*e_w, rect, -yup*e_w,
		   NULL);
    }
    else {
      L->DrawDiff (e->flavor, e->type, dx, dy, rect, yup*e_w, NULL);
    }
    RECT_UPDATE (e->type, dx,dy,dx+rect,dy+yup*e_w);
    dx += rect;
  }

  /* now print fet */
  if (yup < 0) {
    L->DrawFet (e->flavor, e->type, dx, dy + yup*e_w, getlength (e, la),
		-yup*e_w, NULL);
  }
  else {
    L->DrawFet (e->flavor, e->type, dx, dy, getlength (e, la), yup*e_w, NULL);
  }

  int poverhang = p->getOverhang (getlength (e, la));
  int uoverhang = poverhang;

  if (fet_type != 0) {
    uoverhang = MAX (uoverhang, p->getNotchOverhang (getlength (e, la)));
  }
  
#if 0
  printf ("yup=%d; print poly\n", yup);
#endif  
  /* now print poly edges */
  if (yup < 0) {
    L->DrawPoly (dx, dy, getlength (e, la), -yup*poverhang, e->g);
    L->DrawPoly (dx, dy + yup*(e_w+uoverhang), getlength(e, la), -yup*uoverhang, NULL);
  }
  else {
    int oppoverhang;
    if (eopp) {
      oppoverhang = p->getOverhang (getlength (eopp, la));
    }
    else {
      oppoverhang = -1;
    }
    /* 
       There may be an issue in case the diffspacing is not enough to
       account for the poly overhang. We break this tie asymmetrically
       here. We really need to see both transistors! But here we
       assume that the overhang is the same for p and n.
    */
    if (eopp &&  (oup + oppoverhang + poverhang >= dy)) {
      int endpoly = oppoverhang + oup;
      int ht = dy - endpoly;
      //L->DrawPoly (dx, dy - yup*poverhang, getlength (e),
      //yup*poverhang, e->g);
      //printf ("adjust: %d, ht %d\n", endpoly, ht);
      L->DrawPoly (dx, endpoly, getlength (e, la), ht, e->g);
    }
    else {
      L->DrawPoly (dx, dy - yup*poverhang, getlength (e, la), yup*poverhang, e->g);
    }
    
    L->DrawPoly (dx, dy + yup*e_w, getlength (e, la), yup*uoverhang, NULL);
  }
  //printf ("done!\n");
  

  dx += getlength (e, la);
  
  if (flags & EDGE_FLAGS_RIGHT) {
    node_t *right;

    if (left == e->a) {
      right = e->b;
    }
    else {
      right = e->a;
    }
    rect = d->effOverhang (e_w, right->contact);

    if (yup < 0) {
      L->DrawDiff (e->flavor, e->type, dx, dy + yup*e_w, rect, -yup*e_w, right);
    }
    else {
      L->DrawDiff (e->flavor, e->type, dx, dy, rect, yup*e_w, right);
    }
    RECT_UPDATE (e->type, dx,dy,dx+rect,dy+yup*e_w);
    dx += rect;
  }

  if (ret) {
    *ret = b;
  }

  return dx;
}

static BBox print_dualstack (Layout *L, struct gate_pairs *gp, int diffspace)
{
  int flavor;
  int xpos, xpos_p;
  BBox b;
  int dx = 0;
  
  if (gp->basepair) {
    flavor = gp->u.e.n->flavor;
  }
  else {
    struct gate_pairs *tmp;
    tmp = (struct gate_pairs *) list_value (list_first (gp->u.gp));
    if (tmp->u.e.n) {
      flavor = tmp->u.e.n->flavor;
    }
    else {
      Assert (tmp->u.e.p, "Hmm");
      flavor = tmp->u.e.p->flavor;
    }
  }

  DiffMat *ndiff = Technology::T->diff[EDGE_NFET][flavor];
  DiffMat *pdiff = Technology::T->diff[EDGE_PFET][flavor];

  //diffspace = ndiff->getOppDiffSpacing (flavor);
  //Assert (diffspace == pdiff->getOppDiffSpacing (flavor), "Hmm?!");

  FetMat *nfet = Technology::T->fet[EDGE_NFET][flavor];
  FetMat *pfet = Technology::T->fet[EDGE_PFET][flavor];

  PolyMat *poly = Technology::T->poly;

  /* ok, now we can draw! */
  Assert (nfet && pfet && poly && ndiff && pdiff, "What?");

  xpos = dx;
  xpos_p = dx;

  b.p.llx = dx;
  b.p.lly = 0;
  b.p.urx = dx;
  b.p.ury = 0;
  b.n = b.p;

  int padn, padp;
  int fposn, fposp;

  int yp = +diffspace/2;
  int yn = yp - diffspace;
  
  if (gp->basepair) {
    for (int i=0; i < gp->share; i++) {
      int flags = 0;

      if (i == 0) {
	flags |= EDGE_FLAGS_LEFT;
      }
      if (i == gp->share-1) {
	flags |= EDGE_FLAGS_RIGHT;
      }

      edge_t *n_prev, *p_prev;
      int n_previdx, p_previdx;
      node_t *n_left, *p_left;
      int n_idx, p_idx;

      if (i == 0) {
	n_prev = NULL;
	n_previdx = 0;

	p_prev = NULL;
	p_previdx = 0;
      }
      else {
	n_prev = gp->u.e.n;
	n_previdx = gp->n_start + i - 1;

	p_prev = gp->u.e.p;
	p_previdx = gp->p_start + i - 1;
      }

      if (i % 2 == 0) {
	n_left = gp->l.n;
	p_left = gp->l.p;
      }
      else {
	n_left = gp->r.n;
	p_left = gp->r.p;
      }
      
      fposn = locate_fetedge (L, xpos, flags, n_prev, n_previdx, n_left,
			      gp->u.e.n, gp->n_start + i);
      
      fposp = locate_fetedge (L, xpos, flags, p_prev, p_previdx, p_left,
			      gp->u.e.p, gp->p_start + i);
      
      if (fposn > fposp) {
	padp = fposn - fposp;
	padn = 0;
      }
      else {
	padn = fposp - fposn;
	padp = 0;
      }

      xpos = emit_rectangle (L, padn, xpos, yn, flags,
			     n_prev, n_previdx, n_left,
			     gp->u.e.n, gp->u.e.p, yp,
			     gp->n_start + i, -1, &b);
    
      xpos_p = emit_rectangle (L, padp, xpos_p, yp, flags,
			       p_prev, p_previdx, p_left,
			       gp->u.e.p, gp->u.e.n, yn,
			       gp->p_start + i, 1, &b);
    }
  }
  else {
    listitem_t *li;
    int firstp = 1, firstn = 1;
    edge_t *prevp = NULL, *prevn = NULL;
    int prevpidx = 0, prevnidx = 0;
    node_t *leftp, *leftn;

    leftp = NULL;
    leftn = NULL;

    for (li = list_first (gp->u.gp); li; li = list_next (li)) {
      struct gate_pairs *tmp;
      unsigned int flagsp = 0, flagsn = 0;
      tmp = (struct gate_pairs *) list_value (li);

      Assert (tmp->basepair, "Hmm");

      for (int i=0; i < tmp->share; i++) {
      
	if (firstp && tmp->u.e.p) {
	  flagsp |= EDGE_FLAGS_LEFT;
	  firstp = 0;
	}
	if (firstn && tmp->u.e.n) {
	  firstn = 0;
	  flagsn |= EDGE_FLAGS_LEFT;
	}
	if (!list_next (li) && i == (tmp->share-1)) {
	  flagsp |= EDGE_FLAGS_RIGHT;
	  flagsn |= EDGE_FLAGS_RIGHT;
	}
	else if (i == (tmp->share-1)) {
	  struct gate_pairs *tnext;
	  tnext = (struct gate_pairs *) list_value (list_next (li));
	  if (!tnext->u.e.p) {
	    flagsp |= EDGE_FLAGS_RIGHT;
	  }
	  if (!tnext->u.e.n) {
	    flagsn |= EDGE_FLAGS_RIGHT;
	  }
	}

	if (tmp->u.e.n) {
	  if (!leftn) {
	    leftn = gp->l.n;
	  }
	  else {
	    Assert (prevn, "Hmm");
	    if (prevn->a == leftn) {
	      leftn = prevn->b;
	    }
	    else {
	      Assert (prevn->b == leftn, "Hmm");
	      leftn = prevn->a;
	    }
	  }
	}
	if (tmp->u.e.p) {
	  if (!leftp) {
	    leftp  = gp->l.p;
	  }
	  else {
	    Assert (prevp, "Hmm");
	    if (prevp->a == leftp) {
	      leftp = prevp->b;
	    }
	    else {
	      Assert (prevp->b == leftp, "Hmm");
	      leftp = prevp->a;
	    }
	  }
	}

	/* compute padding */
	padn = 0;
	padp = 0;
	if (tmp->u.e.n && tmp->u.e.p) {
	  fposn = locate_fetedge (L, xpos, flagsn,
				  prevn, prevnidx, leftn, tmp->u.e.n,
				  tmp->n_start + i);
	  fposp = locate_fetedge (L, xpos_p, flagsp,
				  prevp, prevpidx, leftp, tmp->u.e.p,
				  tmp->p_start + i);
	  if (fposn > fposp) {
	    padp = padp + fposn - fposp;
	  }
	  else {
	    padn = padn + fposp - fposn;
	  }
	}
      
	if (tmp->u.e.n) {
	  xpos = emit_rectangle (L, padn, xpos, yn, flagsn,
				 prevn, prevnidx, leftn, tmp->u.e.n,
				 tmp->u.e.p, yp,
				 tmp->n_start + i, -1, &b);
	  prevn = tmp->u.e.n;
	  prevnidx = tmp->n_start + i;
	  if (!tmp->u.e.p) {
	    xpos_p = xpos;
	  }
	}
      
	if (tmp->u.e.p) {
	  xpos_p = emit_rectangle (L, padp, xpos_p, yp, flagsp,
				   prevp, prevpidx, leftp, tmp->u.e.p,
				   tmp->u.e.n, yn,
				   tmp->p_start + i, 1, &b);

	  prevp = tmp->u.e.p;
	  prevpidx = tmp->p_start + i;
	  if (!tmp->u.e.n) {
	    xpos = xpos_p;
	  }
	}
      }
    }
  }
  return b;
}


static BBox print_singlestack (Layout *L, list_t *l,
			       int diffspace, int xoff)
{
  int flavor;
  int type;
  node_t *n;
  edge_t *e;
  edge_t *prev;
  int xpos;
  int ypos = 0;
  BBox b;
  int idx = 0;
  int previdx = 0;

  b.p.llx = xpos;
  b.p.lly = 0;
  b.p.urx = xpos;
  b.p.ury = 0;
  b.n = b.p;

  if (list_length (l) < 4) return b;

  n = (node_t *) list_value (list_first (l));
  e = (edge_t *) list_value (list_next (list_first (l)));
  idx = (long) list_value (list_next (list_next (list_first (l))));
  
  flavor = e->flavor;
  type = e->type;
  
  DiffMat *diff = Technology::T->diff[type][flavor];
  FetMat *fet = Technology::T->fet[type][flavor];
  PolyMat *poly = Technology::T->poly;

  ypos = diffspace/2;
  if (type == EDGE_NFET) {
    ypos = ypos - diffspace;
  }
  xpos = xoff;

  /* ok, now we can draw! */
  Assert (fet && diff && poly, "What?");

  /* lets draw rectangles */
  listitem_t *li;

  prev = NULL;
  previdx = 0;
  li = list_first (l);
  while (li && list_next (li) && list_next (list_next (li))) {
    unsigned int flags = 0;
    node_t *m;

    n = (node_t *) list_value (li);
    e = (edge_t *) list_value (list_next (li));
    idx = (long) list_value (list_next (list_next (li)));
    m = (node_t *) list_value (list_next (list_next (list_next (li))));

    if (li == list_first (l)) {
      flags |= EDGE_FLAGS_LEFT;
    }
    if (!list_next (list_next (list_next (list_next (li))))) {
      flags |= EDGE_FLAGS_RIGHT;
    }

    xpos = emit_rectangle (L, 0, xpos, ypos, flags, prev, previdx, 
			   n, e, NULL, 0, idx,
			   (type == EDGE_NFET ? -1 : 1), &b);
    prev = e;
    previdx = idx;

    li = list_next (list_next (list_next (li)));
  }
  Assert (li && !list_next (li), "Eh?");
  n = (node_t *) list_value (li);
  
  return b;
}

void *ActStackLayout::localop (ActPass *ap, Process *p, int mode)
{
  ActDynamicPass *dp = dynamic_cast<ActDynamicPass *>(ap);
  Assert (dp, "What?");
  if (!_fp) {
    _fp = (FILE *)dp->getPtrParam ("lef_file");
  }
  if (!_fpcell) {
    _fpcell = (FILE *)dp->getPtrParam ("cell_file");
  }
  if (mode == 0) {
    return _createlocallayout (p);
  }
  else if (mode == 1) {
    emitLEFHeader (_fp);
    emitWellHeader (_fpcell);
    _emitlocalLEF (p);
  }
  else if (mode == 2) {
    _reportLocalStats (p);
  }
  else if (mode == 3) {
    _maxHeightlocal (p);
  }
  else if (mode == 4) {
    _emitlocalRect (p);
  }
  return ap->getMap (p);
}

void *layout_proc (ActPass *_ap, Process *p, int mode)
{
  ActDynamicPass *ap = dynamic_cast<ActDynamicPass *> (_ap);
  Assert (ap, "What?");
  
  ActStackLayout *lp = (ActStackLayout *)ap->getPtrParam ("raw");
  
  return lp->localop (_ap, p, mode);
}

void layout_free (ActPass *ap, void *v)
{
  LayoutBlob *b = (LayoutBlob *)v;
  if (b) {
    delete b;
  }
}

LayoutBlob *ActStackLayout::_readlocalRect (Process *p)
{
  char cname[10240];
  int len;

  if (!_rect_import) {
    return NULL;
  }
  
  if (!p) {
    snprintf (cname, 10240, "toplevel");
  }
  else {
    a->msnprintfproc (cname, 10240, p);
  }
  len = strlen (cname);
  snprintf (cname + len, 10240 - len, ".rect");

#if 0
  printf (" === processing %s\n", cname);
#endif  
  LayoutBlob *b = LayoutBlob::ReadRect (cname, nl->getNL (p));
  if (!b) {
    return NULL;
  }

  /* now shift all the tiles to line up 0,0 in the middle of the
     diffusion section */
  DiffMat *d = NULL;
  list_t *tiles = NULL;
  int type, flavor;
  long ymin, ymax;
  long updiff, dndiff;
  int set_diff = 0;
  
  for (int i=0; i < Technology::T->num_devs; i++) {
    for (int j=0; j < 2; j++) {
      tiles = b->search (TILE_FLGS_TO_ATTR(i,j,DIFF_OFFSET));
      if (list_isempty (tiles)) {
	list_free (tiles);
      }
      else {
	/* done! */
	long xmin, xmax;
	d = Technology::T->diff[j][i];
	type = j;
	flavor = i;
	LayoutBlob::searchBBox (tiles, &xmin, &ymin, &xmax, &ymax);
	/* calculate ymin, ymax */
	LayoutBlob::searchFree (tiles);
	set_diff++;
	if (type == EDGE_PFET) {
	  updiff = ymin;
	}
	else {
	  dndiff = ymax;
	}
      }
    }
    if (set_diff > 0) {
      break;
    }
  }
  if (set_diff == 0) {
    warning ("Read %s; no diffusion found?", cname);
  }
  else {
    /* 
       We align this so that y-coordinate of 0 can be used
       to consistently align the wells, with the p-diff region on 
       top and the n-diff region on the bottom.
    */

    //int diffspace = d->getOppDiffSpacing (flavor);
    int diffspace = _localdiffspace (p);

    if (set_diff == 2) {
      if ((updiff - dndiff) != diffspace) {
	warning ("%s: center diffusion spacing asjusted (orig: %d; .rect: %d); using .rect file value", p->getName(), diffspace, updiff-dndiff);
	diffspace = updiff - dndiff;
      }
    }
    
    int p = +diffspace/2;
    int n = p - diffspace;
    int xlate;

    if (type == EDGE_NFET) {
      xlate = n - ymax;
    }
    else {
      xlate = p - ymin;
    }
    if (xlate != 0) {
      LayoutBlob *tmp = new LayoutBlob (BLOB_LIST);
      tmp->appendBlob (b, BLOB_VERT, xlate);
      b = tmp;
    }
  }
  
  b = computeLEFBoundary (b);
  b->markRead ();
  
  return b;
}
  

/*
 *
 * Convert transistors stacks into groups and generate layout geometry
 * for the local circuits within the process
 *
 */
LayoutBlob *ActStackLayout::_createlocallayout (Process *p)
{
  list_t *stks;
  BBox b;
  LayoutBlob *BLOB;

  Assert (stk, "What?");

  act_languages *lang = p->getlang();

  if (p->isBlackBox() || p->isLowLevelBlackBox()) {
    netlist_t *n = nl->getNL (p);

    if (n->bN->macro && n->bN->macro->isValid()) {
      BLOB = new LayoutBlob (n->bN->macro);
    }
    else {
      BLOB = NULL;
    }
    return BLOB;
  }
 
  stks = (list_t *) stk->getMap (p);
  if (!stks || list_length (stks) == 0) {
    return NULL;
  }

  BLOB = _readlocalRect (p);
  if (BLOB) {
    return BLOB;
  }

  b.n.llx = 0;
  b.n.lly = 0;
  b.n.urx = 0;
  b.n.ury = 0;
  b.p = b.n;

  listitem_t *li;

  li = list_first (stks);
  list_t *stklist = (list_t *) list_value (li);

  BLOB = new LayoutBlob (BLOB_LIST);

  int diffspace = _localdiffspace (p);

  int has_both_types = 0;

  //printf ("Creating local layout: %s\n", p->getName());

  if (list_length (stklist) > 0) {
    /* dual stacks */
    listitem_t *si;

    for (si = list_first (stklist); si; si = list_next (si)) {
      struct gate_pairs *gp;
      Layout *l = new Layout(nl->getNL (p));
      gp = (struct gate_pairs *) list_value (si);
      has_both_types = 1;

      /*--- process gp ---*/
      b = print_dualstack (l, gp, diffspace);
      
      l->DrawDiffBBox (b.flavor, EDGE_PFET,
		       b.p.llx, b.p.lly, b.p.urx-b.p.llx, b.p.ury-b.p.lly);
      l->DrawDiffBBox (b.flavor, EDGE_NFET,
		       b.n.llx, b.n.lly, b.n.urx-b.n.llx, b.n.ury-b.n.lly);

      BLOB->appendBlob (new LayoutBlob (BLOB_BASE, l), BLOB_HORIZ);
    }
  }

  li = list_next (li);
  stklist = (list_t *) list_value (li);

  /* XXX: check singlestack!!! */
  int nxpos = b.n.urx;
  int pxpos = b.p.urx;

  if (stklist && (list_length (stklist) > 0)) {
    /* n stacks */
    listitem_t *si;

    if (!has_both_types && list_value (list_next (li)) &&
	(list_length ((list_t*) list_value (list_next (li))) > 0)) {
      has_both_types = 1;
    }

    for (si = list_first (stklist); si; si = list_next (si)) {
      list_t *sl = (list_t *) list_value (si);
      Layout *l = new Layout (nl->getNL (p));

      b = print_singlestack (l, sl, diffspace, nxpos);
      
      l->DrawDiffBBox (b.flavor, EDGE_NFET, b.n.llx, b.n.lly,
		       b.n.urx - b.n.llx, b.n.ury - b.n.lly);

      if (!has_both_types && !Technology::T->well[EDGE_NFET][b.flavor]) {
	l->DrawDiff (b.flavor, EDGE_PFET, b.n.llx, b.n.lly + diffspace +
		     (b.n.ury - b.n.lly),
		     b.n.urx - b.n.llx, b.n.ury - b.n.lly, l->getVdd());
	l->DrawDiffBBox (b.flavor, EDGE_PFET, b.n.llx, b.n.lly + diffspace +
			 (b.n.ury - b.n.lly),
			 b.n.urx - b.n.llx, b.n.ury - b.n.lly);
	has_both_types = 1;
      }

      BLOB->appendBlob (new LayoutBlob (BLOB_BASE, l), BLOB_HORIZ); 
    }
  }

  li = list_next (li);
  stklist = (list_t *) list_value (li);
  if (stklist && (list_length (stklist) > 0)) {
    /* p stacks */
    listitem_t *si;

    for (si = list_first (stklist); si; si = list_next (si)) {
      list_t *sl = (list_t *) list_value (si);
      Layout *l = new Layout (nl->getNL (p));

      b = print_singlestack (l, sl, diffspace, pxpos);
      
      l->DrawDiffBBox (b.flavor, EDGE_PFET, b.p.llx, b.p.lly,
		       b.p.urx - b.p.llx, b.p.ury - b.p.lly);

      if (!has_both_types && !Technology::T->well[EDGE_PFET][b.flavor]) {
	l->DrawDiff (b.flavor, EDGE_NFET, b.p.llx, b.p.lly - diffspace -
		     (b.p.ury - b.p.lly),
		     b.p.urx - b.p.llx, b.p.ury - b.p.lly, l->getGND());
	l->DrawDiffBBox (b.flavor, EDGE_NFET, b.p.llx, b.p.lly - diffspace -
			 (b.p.ury - b.p.lly),
			 b.p.urx - b.p.llx, b.p.ury - b.p.lly);
	has_both_types = 1;
      }

      BLOB->appendBlob (new LayoutBlob (BLOB_BASE, l), BLOB_HORIZ); 
    }
  }

  /* now we need to adjust the boundary of this cell to make sure
     several alignment restrictions are satisfied.
     
     * The y-coordinate for 0 is on a track boundary.

     This means that we have to bloat in the -y and +y direction so
     that the bounding box is on a track boundary, rather than looking
     at the total y dimension for bloating.
    
     * Any mirroring is legal. This means that we have to have
       spacing/2 around all the material on all sides.
  */

  BLOB = computeLEFBoundary (BLOB);

  /* --- add pins --- */
  netlist_t *n = nl->getNL (p);

  if (!dummy_netlist && n->psc && n->nsc) {
    dummy_netlist = n;
  }

  Rectangle b_bbox;
  b_bbox = BLOB->getBBox ();

  if (n && !b_bbox.empty()) {
    /* we have a netlist + layout */
    int p_in = 0;
    int p_out = 0;
    int s_in = 1;
    int s_out = 1;

    int redge = (b_bbox.urx() - b_bbox.llx() + 1);
    int tedge = (b_bbox.ury() - b_bbox.lly() + 1);

    redge = snap_up_x (redge);
    tedge = snap_up_y (tedge);

    /* move the top edge if there isn't enough space for two
       rows of pins 
    */
    while (tedge - _pin_metal->getLEFWidth() <=
	   _m_align_y->getPitch() + _pin_metal->getLEFWidth() +
	   _pin_metal->minSpacing())
      {
       tedge += _m_align_y->getPitch();
      }

    int found_vdd = 0;
    int found_gnd = 0;
    for (int i=0; i < A_LEN (n->bN->ports); i++) {
      if (n->bN->ports[i].omit) continue;

      ihash_bucket_t *b;
      b = ihash_lookup (n->bN->cH, (long)n->bN->ports[i].c);

      if (!b) {
	/* port is only used as a pass through, and is not a pin for
	   the local LEF */
	continue;
      }
      
      if (n->bN->ports[i].input) {
	p_in++;
      }
      else {
	p_out++;
      }

      act_booleanized_var_t *bv = (act_booleanized_var_t *)b->v;
      struct act_nl_varinfo *av = (struct act_nl_varinfo *)bv->extra;
      Assert (av, "Hmm");

      if (av->n == n->Vdd) {
	found_vdd = 1;
      }
      if (av->n == n->GND) {
	found_gnd = 1;
      }
    }
    for (int i=0; i < A_LEN (n->bN->used_globals); i++) {
      ihash_bucket_t *b;
      
      b = ihash_lookup (n->bN->cH, (long)n->bN->used_globals[i].c);
      p_in++;

      act_booleanized_var_t *bv = (act_booleanized_var_t *)b->v;
      struct act_nl_varinfo *av = (struct act_nl_varinfo *)bv->extra;
      Assert (av, "Hmm");

      if (av->n == n->Vdd) {
	found_vdd = 1;
      }
      if (av->n == n->GND) {
	found_gnd = 1;
      }
    }

    if (!found_vdd && n->Vdd && n->Vdd->e && list_length (n->Vdd->e) > 0) {
      p_in++;
      found_vdd = 1;
    }
    if (!found_vdd && n->GND && n->GND->e && list_length (n->GND->e) > 0) {
      p_in++;
      found_gnd = 1;
    }
    
    if (n->weak_supply_vdd > 0) {
      p_in++;
    }
    if (n->weak_supply_gnd > 0) {
      p_in++;
    }

    if ((p_in * _m_align_x->getPitch() > redge) ||
	(p_out * _m_align_x->getPitch() > redge)) {
      warning ("Can't fit ports!");
    }
    
    if (p_in > 0) {
      while ((_m_align_x->getPitch() + p_in * s_in * _m_align_x->getPitch())
	     <= redge)
	{
	  s_in++;
	}
      s_in--;
      if (s_in == 0) { s_in = 1; }
    }

    if (p_out > 0) {
      while ((_m_align_x->getPitch() + p_out * s_out * _m_align_x->getPitch())
	     <= redge)
	{
	  s_out++;
	}
      s_out--;
      if (s_out == 0) { s_out = 1; }
    }

    /* sin, sout: strides */

    p_in = _m_align_x->getPitch();
    p_out = _m_align_x->getPitch();

    Layout *pins = new Layout(n);

    found_vdd = 0;
    found_gnd = 0;
    for (int i=0; i < A_LEN (n->bN->ports); i++) {
      if (n->bN->ports[i].omit) continue;

      ihash_bucket_t *b;
      b = ihash_lookup (n->bN->cH, (long)n->bN->ports[i].c);
      if (!b) continue;
      
      Assert (b, "Hmm:");
      act_booleanized_var_t *v;
      struct act_nl_varinfo *av;
      v = (act_booleanized_var_t *) b->v;
      av = (struct act_nl_varinfo *)v->extra;
      Assert (av, "Problem..");

      if (n->bN->ports[i].input) {
	int w = _pin_metal->getLEFWidth ();
	pins->DrawMetalPin (_pin_layer,
			    b_bbox.llx() + p_in,
			    b_bbox.lly() + tedge - w, w, w, av->n, 0);
	p_in += _m_align_x->getPitch()*s_in;
      }
      else {
	int w = _pin_metal->getLEFWidth ();

	pins->DrawMetalPin (_pin_layer,
			    b_bbox.llx() + p_out,
			    b_bbox.lly() + _m_align_y->getPitch(),
			    w, w, av->n, 1);
	
	p_out += _m_align_x->getPitch()*s_out;
      }

      if (av->n == n->Vdd) {
	found_vdd = 1;
      }
      if (av->n == n->GND) {
	found_gnd = 1;
      }
    }

    /* globals */
    for (int i=0; i < A_LEN (n->bN->used_globals); i++) {
      ihash_bucket_t *b;
      b = ihash_lookup (n->bN->cH, (long)n->bN->used_globals[i].c);
      
      Assert (b, "Hmm:");

      act_booleanized_var_t *bv = (act_booleanized_var_t *)b->v;
      struct act_nl_varinfo *av = (struct act_nl_varinfo *)bv->extra;
      Assert (av, "Hmm");

      int w = _pin_metal->getLEFWidth ();
      pins->DrawMetalPin (_pin_layer,
			  b_bbox.llx() + p_in,
			  b_bbox.lly() + tedge - w, w, w, av->n, 0);
      p_in += _m_align_x->getPitch()*s_in;

      if (av->n == n->Vdd) {
	found_vdd = 1;
      }
      if (av->n == n->GND) {
	found_gnd = 1;
      }
    }
    if (!found_vdd && n->Vdd && n->Vdd->e && list_length (n->Vdd->e) > 0) {
      found_vdd = 1;
      int w = _pin_metal->getLEFWidth ();
      pins->DrawMetalPin (_pin_layer,
			  b_bbox.llx() + p_in,
			  b_bbox.lly() + tedge - w, w, w, n->Vdd, 0);
      p_in += _m_align_x->getPitch()*s_in;
      
    }
    if (!found_gnd && n->GND && n->GND->e && list_length (n->GND->e) > 0) {
      found_gnd = 1;
      int w = _pin_metal->getLEFWidth ();
      pins->DrawMetalPin (_pin_layer, b_bbox.llx() + p_in,
			  b_bbox.lly() + tedge - w, w, w, n->GND, 0);
      p_in += _m_align_x->getPitch()*s_in;
    }

    /*--- XXX: but this is not the end of the pins... ---*/

    
    LayoutBlob *bl = new LayoutBlob (BLOB_LIST);
    bl->appendBlob (BLOB, BLOB_MERGE);
    bl->appendBlob (new LayoutBlob (BLOB_BASE, pins), BLOB_MERGE);
    BLOB = bl;
  }

  BLOB = LayoutBlob::delBBox (BLOB);
  if (BLOB) {
    BLOB = computeLEFBoundary (BLOB);
  }

  return BLOB;
}


LayoutBlob *ActStackLayout::_readwelltap (int flavor)
{
  char cname[128];
  char *tmpname;
  
  if (!_rect_import) {
    return NULL;
  }

  snprintf (cname, 128, "welltap_%s.rect", act_dev_value_to_string (flavor));

  LayoutBlob *b = LayoutBlob::ReadRect (cname, dummy_netlist);

  if (!b) {
    return NULL;
  }

  /* now shift all the tiles to line up 0,0 in the middle of the
     ppdiff/nndiff diffusion section */
  DiffMat *d = NULL;
  list_t *tiles = NULL;
  int type;
  long ymin, ymax;
  long updiff, dndiff;
  int set_diff = 0;

  updiff = 0;
  dndiff = 0;

  
  for (int j=0; j < 2; j++) {
    tiles = b->search (TILE_FLGS_TO_ATTR(flavor,j,WDIFF_OFFSET));
    if (list_isempty (tiles)) {
      list_free (tiles);
    }
    else {
      /* done! */
      long xmin, xmax;
      d = Technology::T->welldiff[j][flavor];
      if (d) {
	type = j;
	LayoutBlob::searchBBox (tiles, &xmin, &ymin, &xmax, &ymax);
	/* calculate ymin, ymax */
	LayoutBlob::searchFree (tiles);
	set_diff++;
	if (type == EDGE_PFET) {
	  updiff = ymin;
	}
	else {
	  dndiff = ymax;
	}
      }
    }
  }
  if (d == NULL) {
    warning ("Read %s; no well diffusion found?", cname);
  }
  else {
    /* 
       We align this so that y-coordinate of 0 can be used
       to consistently align the wells, with the p-diff region on 
       top and the n-diff region on the bottom.
    */
    
    int diffspace = d->getOppDiffSpacing (flavor);

    if (set_diff == 2) {
      if ((updiff - dndiff) != diffspace) {
	warning ("welltap_%s: center diffusion spacing asjusted (orig: %d; .rect: %d); using .rect file value", act_dev_value_to_string (flavor), diffspace, updiff-dndiff);
	diffspace = updiff - dndiff;
      }
    }
    
    int p = +diffspace/2;
    int n = p - diffspace;
    int xlate;

    if (type == EDGE_NFET) {
      xlate = n - ymax;
    }
    else {
      xlate = p - ymin;
    }
    if (xlate != 0) {
      LayoutBlob *tmp = new LayoutBlob (BLOB_LIST);
      tmp->appendBlob (b, BLOB_VERT, xlate);
      b = tmp;
    }
  }
  
  b = computeLEFBoundary (b);
  b->markRead ();
  
  return b;
}


LayoutBlob *ActStackLayout::_createwelltap (int flavor)
{
  Layout *l;
  DiffMat *nplusdiff = Technology::T->welldiff[EDGE_NFET][flavor];
  DiffMat *pplusdiff = Technology::T->welldiff[EDGE_PFET][flavor];
  LayoutBlob *BLOB;

  BLOB = _readwelltap (flavor);
  if (BLOB) {
    return BLOB;
  }

  /* no well tap */
  if (!nplusdiff && !pplusdiff) {
    return NULL;
  }
    
  int diffspace;
  
  if (nplusdiff) {
    diffspace = nplusdiff->getOppDiffSpacing(flavor);
  }
  else {
    diffspace = pplusdiff->getOppDiffSpacing(flavor);
  }
  
  int p = +diffspace/2;
  int n = p - diffspace;

  l = new Layout (dummy_netlist);

  if (nplusdiff) {
    int mina = nplusdiff->minArea ();
    WellMat *w;
    if (mina > 0) {
      mina = (mina + nplusdiff->minWidth() - 1)/ nplusdiff->minWidth();
    }
    if (mina < nplusdiff->minWidth()) {
      mina = nplusdiff->minWidth();
    }
    w = Technology::T->well[EDGE_NFET][flavor];
    if (w) {
      if (w->getOverhangWelldiff() > p) {
	p = w->getOverhangWelldiff();
      }
    }
    l->DrawWellDiff (flavor, EDGE_PFET, 0, p, nplusdiff->minWidth (),
		     mina, dummy_netlist->nsc);
  }
  if (pplusdiff) {
    WellMat *w;
    int mina = pplusdiff->minArea ();
    if (mina > 0) {
      mina = (mina + pplusdiff->minWidth() - 1)/ pplusdiff->minWidth();
    }
    if (mina < pplusdiff->minWidth()) {
      mina = pplusdiff->minWidth();
    }
    w = Technology::T->well[EDGE_PFET][flavor];
    if (w) {
      if (w->getOverhangWelldiff() > -n) {
	n = -w->getOverhangWelldiff();
      }
    }
    l->DrawWellDiff (flavor, EDGE_NFET, 0, n - mina + 1,
		     pplusdiff->minWidth(), mina, dummy_netlist->psc);
  }

  BLOB = new LayoutBlob (BLOB_BASE, l);

  BLOB = computeLEFBoundary (BLOB);

  /* add pins */
  Rectangle b_bbox;
  b_bbox = BLOB->getBBox ();

  int tedge;
  tedge = snap_up_y (b_bbox.ury() - b_bbox.lly() + 1);

  while (tedge - _pin_metal->getLEFWidth() <=
	 _m_align_y->getPitch() + _pin_metal->getLEFWidth() +
	 _pin_metal->minSpacing())
    {
      tedge += _m_align_y->getPitch();
    }

  p = _m_align_x->getPitch();
  Layout *pins = new Layout (dummy_netlist);
  int w = _pin_metal->getLEFWidth();
  pins->DrawMetalPin (_pin_layer, b_bbox.llx() + p,
		      b_bbox.lly() + tedge - w, w, w,
		      dummy_netlist->nsc, 0);
    
  pins->DrawMetalPin (_pin_layer,
		      b_bbox.llx() + p,
		      b_bbox.lly() + _m_align_y->getPitch(), w, w,
		      dummy_netlist->psc, 0);

  LayoutBlob *bl = new LayoutBlob (BLOB_LIST);
  bl->appendBlob (new LayoutBlob (BLOB_BASE, pins), BLOB_MERGE);
  bl->appendBlob (BLOB, BLOB_MERGE);

  bl = LayoutBlob::delBBox (bl);

  BLOB = computeLEFBoundary (bl);

  return BLOB;
}


void ActStackLayout::_emitwelltaprect (int flavor)
{
  if (!wellplugs[flavor]) {
    return;
  }
    
  LayoutBlob *b = wellplugs[flavor];
  char name[1024];

  snprintf (name, 1019, "welltap_%s", act_dev_value_to_string (flavor));

  TransformMat mat;
  Rectangle bloatbox;
  bloatbox = b->getBloatBBox ();
  mat.translate (-bloatbox.llx(), -bloatbox.lly());
      
  /* emit rectangles */
  strcat (name, ".rect");

  FILE *tfp;

  const char *outdir;
  if (b->getRead()) {
    outdir = _rect_outdir;
  }
  else {
    outdir = _rect_outinitdir;
  }
  
  if (outdir) {
    char *outname;
    int sz = strlen (name) + strlen (outdir) + 2;
    MALLOC (outname, char, sz);
    snprintf (outname, sz, "%s/%s", outdir, name);
    tfp = fopen (outname, "w");
    if (!tfp) {
      fatal_error ("Could not open file `%s' for writing", outname);
    }
    FREE (outname);
  }
  else {
    tfp = fopen (name, "w");
    if (!tfp) {
      fatal_error ("Could not open file `%s' for writing", name);
    }
  }
  b->PrintRect (tfp, &mat);

  if (_rect_wells) {
    for (int j=0; j < 2; j++) {
      long wllx, wlly, wurx, wury;
      _computeWell (b, flavor, j, &wllx, &wlly, &wurx, &wury, 1);
      if (wllx < wurx && wlly < wury) {
	fprintf (tfp, "rect # %s %ld %ld %ld %ld\n",
		 Technology::T->well[j][flavor]->getName(),
		 wllx, wlly, wurx, wury);
      }
    }
  }
  fclose (tfp);
}

void layout_run (ActPass *_ap, Process *p)
{
  ActDynamicPass *ap = dynamic_cast<ActDynamicPass *> (_ap);
  ActStackLayout *lp;
  Assert (ap, "What?");

  lp = (ActStackLayout *)ap->getPtrParam ("raw");
  lp->run_post ();
}

void ActStackLayout::run_post (void)
{
  if (!dummy_netlist) {
    fatal_error ("Layout generation: could not find both power supplies for substrate contacts!");
  }

  /* create welltap cells */
  int ntaps = config_get_table_size ("act.dev_flavors");
  MALLOC (wellplugs, LayoutBlob *, ntaps);
  for (int flavor=0; flavor < ntaps; flavor++) {
    wellplugs[flavor] = _createwelltap (flavor);
  }
}

void ActStackLayout::_emitlocalRect (Process *p)
{
  LayoutBlob *blob = getLayout (p);

  if (!blob) {
    return;
  }

  Rectangle bloatbox;
  bloatbox = blob->getBloatBBox ();

  if (bloatbox.empty()) {
    /* no layout */
    return;
  }

  TransformMat mat;
  mat.translate (-bloatbox.llx(), -bloatbox.lly());

  FILE *fp;
  char cname[10240];

  if (p) {
    a->msnprintfproc (cname, 10240, p);
  }
  else {
    snprintf (cname, 10240, "toplevel");
  }
  int len = strlen (cname);
  snprintf (cname + len, 10240-len, ".rect");


  const char *outdir;
  if (blob->getRead()) {
    outdir = _rect_outdir;
  }
  else {
    outdir = _rect_outinitdir;
  }

  if (outdir) {
    char *outname;
    int sz = strlen (cname) + strlen (outdir) + 2;
    MALLOC (outname, char, sz);
    snprintf (outname, sz, "%s/%s", outdir, cname);
    fp = fopen (outname, "w");
    if (!fp) {
      fatal_error ("Could not open file `%s' for writing", outname);
    }
    FREE (outname);
  }
  else {
    fp = fopen (cname, "w");
    if (!fp) {
      fatal_error ("Could not open file `%s' for writing", cname);
    }
  }
  blob->PrintRect (fp, &mat);

  if (_rect_wells) {

    for (int i=0; i < Technology::T->num_devs; i++) {
      for (int j=0; j < 2; j++) {
	long wllx, wlly, wurx, wury;
	_computeWell (blob, i, j, &wllx, &wlly, &wurx, &wury);
	if (wllx < wurx && wlly < wury) {
	  fprintf (fp, "rect # %s %ld %ld %ld %ld\n",
		   Technology::T->well[j][i]->getName(),
		   wllx, wlly, wurx, wury);
	}
      }
    }
  }
  
  fclose (fp);
}

static void emit_header (FILE *fp, const char *name, const char *lefclass,
			 LayoutBlob *blob)
{
  double scale = Technology::T->scale/1000.0;
  
  fprintf (fp, "MACRO %s\n", name);
  fprintf (fp, "    CLASS %s ;\n", lefclass);
  fprintf (fp, "    FOREIGN %s %.6f %.6f ;\n", name, 0.0, 0.0);
  fprintf (fp, "    ORIGIN %.6f %.6f ;\n", 0.0, 0.0);

  Rectangle bloatbox;
  bloatbox = blob->getBloatBBox ();

#if 0  
  printf ("SIZE: %ld x %ld\n", burx - bllx + 1, bury - blly + 1);
#endif
  
  fprintf (fp, "    SIZE %.6f BY %.6f ;\n",
	   bloatbox.wx()*scale, bloatbox.wy()*scale);
  fprintf (fp, "    SYMMETRY X Y ;\n");
  fprintf (fp, "    SITE CoreSite ;\n");
}


static void emit_footer (FILE *fp, const char *name)
{
  fprintf (fp, "END %s\n\n", name);
}

static int emit_layer_rects (FILE *fp, list_t *tiles, node_t **io = NULL,
			      int num_io = 0)
{
  double scale = Technology::T->scale/1000.0;
  listitem_t *tli;
  int emit_obs = 0;

  for (tli = list_first (tiles); tli; tli = list_next (tli)) {
    struct tile_listentry *tle = (struct tile_listentry *) list_value (tli);
    listitem_t *xi;
    Layer *lprev = NULL;

    for (xi = list_first (tle->tiles); xi; xi = list_next (xi)) {
      Layer *lname = (Layer *) list_value (xi);
      xi = list_next (xi);
      Assert (xi, "Hmm");

      if (!lname->isMetal()) {
	continue;
      }

      list_t *actual_tiles = (list_t *) list_value (xi);
      listitem_t *ti;
      int first = 1;
      
      for (ti = list_first (actual_tiles); ti; ti = list_next (ti)) {
	long tllx, tlly, turx, tury;
	Tile *tmp = (Tile *) list_value (ti);

	if (tmp->getNet()) {
	  int k;
	  for (k=0; k < num_io; k++) {
	    if (tmp->getNet() == io[k])
	      break;
	  }
	  if (k != num_io) {
	    /* skip! */
	    continue;
	  }
	}

	if (first) {
	  if (!emit_obs && io != NULL) {
	    fprintf (fp, "    OBS\n");
	    emit_obs = 1;
	  }
	  if (lname == lprev) {
	    fprintf (fp, "        LAYER %s ;\n", lname->getViaName());
	  }
	  else {
	    fprintf (fp, "        LAYER %s ;\n", lname->getRouteName());
	  }
	}
	first = 0;
	
	tle->m.apply (tmp->getllx(), tmp->getlly(), &tllx, &tlly);
	tle->m.apply (tmp->geturx(), tmp->getury(), &turx, &tury);

	if (tllx > turx) {
	  long x = tllx;
	  tllx = turx;
	  turx = x;
	}
	  
	if (tlly > tury) {
	  long x = tlly;
	  tlly = tury;
	  tury = x;
	}
	
	fprintf (fp, "        RECT %.6f %.6f %.6f %.6f ;\n",
		 scale*tllx, scale*tlly, scale*(1+turx), scale*(1+tury));
      }
      lprev = lname;
    }
  }
  return emit_obs;
}

static void emit_antenna_area (FILE *fp, list_t *tiles)
{
  double scale = Technology::T->scale/1000.0;
  listitem_t *tli;
  double ant_area = 0.0;
  double ant_diffarea = 0.0;

  for (tli = list_first (tiles); tli; tli = list_next (tli)) {
    struct tile_listentry *tle = (struct tile_listentry *) list_value (tli);
    listitem_t *xi;

    for (xi = list_first (tle->tiles); xi; xi = list_next (xi)) {
      Layer *lname = (Layer *) list_value (xi);
      xi = list_next (xi);
      Assert (xi, "Hmm");

      if (lname->isMetal()) {
	continue;
      }

      list_t *actual_tiles = (list_t *) list_value (xi);
      listitem_t *ti;
      int first = 1;
      
      for (ti = list_first (actual_tiles); ti; ti = list_next (ti)) {
	long tllx, tlly, turx, tury;
	Tile *tmp = (Tile *) list_value (ti);

	tle->m.apply (tmp->getllx(), tmp->getlly(), &tllx, &tlly);
	tle->m.apply (tmp->geturx(), tmp->getury(), &turx, &tury);
	
	if (tllx > turx) {
	  long x = tllx;
	  tllx = turx;
	  turx = x;
	}

	if (tlly > tury) {
	  long x = tlly;
	  tlly = tury;
	  tury = x;
	}
	
	if (tmp->isFet()) {
	  ant_area += (turx-tllx+1)*scale*(tury-tlly+1)*scale;
	}
	else if (tmp->isDiff()) {
	  ant_diffarea += (turx-tllx+1)*scale*(tury-tlly+1)*scale;
	}
      }
    }
  }
  if (ant_area > 0) {
    fprintf (fp, "        ANTENNAGATEAREA %.6f ;\n", ant_area);
  }
  if (ant_diffarea > 0) {
    fprintf (fp, "        ANTENNADIFFAREA %.6f ;\n", ant_diffarea);
  }
}  


static void emit_one_pin (Act *a, FILE *fp, const char *name, int isinput,
			  const char *sigtype, LayoutBlob *blob,
			  node_t *signode)
{
  double scale = Technology::T->scale/1000.0;

  Rectangle bloatbox = blob->getBloatBBox ();
  
  fprintf (fp, "    PIN ");
  a->mfprintf (fp, "%s\n", name);
  
  //printf ("pin %s [node 0x%lx]\n", name, (unsigned long)signode);

  fprintf (fp, "        DIRECTION %s ;\n", isinput ? "INPUT" : "OUTPUT");
  fprintf (fp, "        USE %s ;\n", sigtype);

  fprintf (fp, "        PORT\n");

  /* -- find all pins of this name! -- */
  TransformMat mat;
  mat.translate (-bloatbox.llx(), -bloatbox.lly());
  list_t *tiles = blob->search (signode, &mat);
  emit_layer_rects (fp, tiles);

  fprintf (fp, "        END\n");

  // now we emit just the fet area for antennas
  emit_antenna_area (fp, tiles);

  LayoutBlob::searchFree (tiles);

  fprintf (fp, "    END ");
  a->mfprintf (fp, "%s", name);
  fprintf (fp, "\n");
}


void ActStackLayout::runrec (int mode, UserDef *u)
{
  if (mode == 1) {
    /* emitLEF */
    
    /*-- emit lef for the welltap cells --*/
    double scale = Technology::T->scale/1000.0;
    for (int i=0; i < config_get_table_size ("act.dev_flavors"); i++) {
      if (wellplugs[i]) {
	LayoutBlob *b = wellplugs[i];
	char name[1024], nodename[1024];

	snprintf (name, 1024, "welltap_%s", act_dev_value_to_string (i));
	emit_header (_fp, name, "CORE WELLTAP", b);

	ActNetlistPass::sprint_node (nodename, 1024, dummy_netlist,
				   dummy_netlist->nsc);
	emit_one_pin (a, _fp, nodename, 1, "POWER", b, dummy_netlist->nsc);

	ActNetlistPass::sprint_node (nodename, 1024, dummy_netlist,
				     dummy_netlist->psc);
	emit_one_pin (a, _fp, nodename, 1, "GROUND", b, dummy_netlist->psc);
	
	emit_footer (_fp, name);

	TransformMat mat;
	Rectangle bloatbox;
	bloatbox = b->getBloatBBox ();
	mat.translate (-bloatbox.llx(), -bloatbox.lly());

	if (_fpcell) {
	  /* emit local well lef */
	  WellMat *w;
	  DiffMat *d;
	
	  int adjust = Technology::T->welltap_adjust;

	  fprintf (_fpcell, "MACRO %s\n", name);
	  fprintf (_fpcell, "   VERSION %s\n", name);
	  fprintf (_fpcell, "   PLUG\n");

	  for (int j=0; j < 2; j++) {
	    long wllx, wlly, wurx, wury;
	    _computeWell (b, i, j, &wllx, &wlly, &wurx, &wury, 1);

	    if (wllx < wurx && wlly < wury) {
	      fprintf (_fpcell, "   LAYER %s ;\n", Technology::T->well[j][i]->getName());
	      fprintf (_fpcell, "   RECT %.6f %.6f %.6f %.6f\n",
		       wllx*scale, wlly*scale, wurx*scale, wury*scale);
	      fprintf (_fpcell, "   END\n");
	    }
	  }
	  fprintf (_fpcell, "   END VERSION\n");
	  fprintf (_fpcell, "END %s\n", name);
	}
      }
    }

    /* done with LEF */
    _lef_header = 0;
    _cell_header = 0;
  }
  else if (mode == 2) {
    /* nothing */
  }
  else if (mode == 3) {
    _maxht = _ymax - _ymin + 1;
  }
  else if (mode == 4) {
    /* emitRect */
    for (int i=0; i < config_get_table_size ("act.dev_flavors"); i++) {
      _emitwelltaprect (i);
    }
  }
  else if (mode == 5) {
    /* emitDEF */
    FILE *fp;
    Process *p = dynamic_cast<Process *> (u);
    double area_mult;
    double aspect_ratio;
    int do_pins;
    ActDynamicPass *dp = dynamic_cast<ActDynamicPass *>(me);
    if (!p || !dp) {
      return;
    }

    dp->run_recursive (p, 3);
    dp->setParam ("cell_maxheight", _maxht);
    
    fp = (FILE *) dp->getPtrParam ("def_file");
    do_pins = dp->getIntParam ("do_pins");
    area_mult = dp->getRealParam ("area_mult");
    aspect_ratio = dp->getRealParam ("aspect_ratio");
    emitDEF (fp, p, area_mult, aspect_ratio, do_pins);

    dp->setParam ("total_area", _total_area);
    dp->setParam ("stdcell_area", _total_stdcell_area);

  }
}

void layout_recursive (ActPass *_ap, UserDef *u, int mode)
{
  ActDynamicPass *dp;
  if (!_ap->completed()) {
    return;
  }
  dp = dynamic_cast<ActDynamicPass *>(_ap);
  Assert (dp, "What?");
  ActStackLayout *lp = (ActStackLayout *)dp->getPtrParam ("raw");
  lp->runrec (mode, u);
}

/*
 *
 * Emit LEF corresponding to this process, adding it to the file
 * specified by the file pointer fp
 *
 */
int ActStackLayout::_emitlocalLEF (Process *p)
{
  char macroname[10240];
  FILE *fp = _fp;
  FILE *fpcell = _fpcell;
  A_DECL (node_t *, iopins);
  
  /* emit self */
  netlist_t *n;

  LayoutBlob *blob = getLayout (p);
  if (!blob) {
    return 0;
  }

  if (blob->isMacro()) {
    /* insert LEF */
    FILE *bfp;

    if (!blob->getLEFFile()) {
      warning ("Macro %s is missing LEF\n", blob->getMacroName());
      return 0;
    }

    bfp = fopen (blob->getLEFFile(), "r");
    if (!bfp) {
      fprintf (stderr, "Macro %s: LEF %s could not be opened\n",
	       blob->getMacroName(), blob->getLEFFile());
      return 0;
    }

    char buf[10240];
    
    while (!feof (bfp)) {
      long sz;
      sz = fread (buf, 1, 10240, bfp);
      if (sz > 0) {
	fwrite (buf, 1, sz, fp);
      }
    }
    fprintf (fp, "\n");
    fclose (bfp);
    return 1;
  }

  n = nl->getNL (p);
  if (!n) {
    return 0;
  }

  Rectangle bloatbox;
  bloatbox = blob->getBloatBBox ();

  if (bloatbox.empty()) {
    /* no layout */
    return 1;
  }

  /* if this has weak gates only, skip it... 
     need to systematize this to synthesize new ports 
  */
  {
    node_t *nd;
    for (nd = n->hd; nd; nd = nd->next) {
      listitem_t *li;
      edge_t *ed;
      for (li = list_first (nd->e); li; li = list_next (li)) {
	ed = (edge_t *) list_value (li);
	if (!ed->keeper)
	  break;
      }
      if (li) {
	break;
      }
    }
    if (!nd) {
      return 0;
    }
  }

  A_INIT (iopins);

  double scale = Technology::T->scale/1000.0;
  
  a->msnprintfproc (macroname, 10240, p);
  emit_header (fp, macroname, "CORE", blob);
  
  /* find pins */
  int found_vdd = 0;
  int found_gnd = 0;
  for (int i=0; i < A_LEN (n->bN->ports); i++) {
    if (n->bN->ports[i].omit) continue;

    /* generate name */
    char tmp[1024];
    ActId *id = n->bN->ports[i].c->toid();
    id->sPrint (tmp, 1024);
    delete id;

    /* and signal type + node pointer */
    const char *sigtype;
    sigtype = "SIGNAL";
    ihash_bucket_t *b;
    b = ihash_lookup (n->bN->cH, (long)n->bN->ports[i].c);
    Assert (b, "What on earth");
    act_booleanized_var_t *v;
    struct act_nl_varinfo *av;
    v = (act_booleanized_var_t *) b->v;
    av = (struct act_nl_varinfo *)v->extra;
    Assert (av, "Huh");
    if (av->n == n->Vdd) {
      sigtype = "POWER";
      found_vdd = 1;
    }
    else if (av->n == n->GND) {
      sigtype = "GROUND";
      found_gnd = 1;
    }
    emit_one_pin (a, fp, tmp, n->bN->ports[i].input, sigtype, blob, av->n);
    A_NEW (iopins, node_t *);
    A_NEXT (iopins) = av->n;
    A_INC (iopins);
  }

  /* add globals as input pins */
  for (int i=0; i < A_LEN (n->bN->used_globals); i++) {
    /* generate name */
    char tmp[1024];
    ActId *id = n->bN->used_globals[i].c->toid();
    id->sPrint (tmp, 1024);
    delete id;

    /* and signal type + node pointer */
    const char *sigtype;
    sigtype = "SIGNAL";
    ihash_bucket_t *b;
    b = ihash_lookup (n->bN->cH, (long)n->bN->used_globals[i].c);
    Assert (b, "What on earth");
    act_booleanized_var_t *v;
    struct act_nl_varinfo *av;
    v = (act_booleanized_var_t *) b->v;
    av = (struct act_nl_varinfo *)v->extra;
    Assert (av, "Huh");
    if (av->n == n->Vdd) {
      found_vdd = 1;
      sigtype = "POWER";
    }
    else if (av->n == n->GND) {
      found_gnd = 1;
      sigtype = "GROUND";
    }
    emit_one_pin (a, fp, tmp, 1 /* input */, sigtype, blob, av->n);
    A_NEW (iopins, node_t *);
    A_NEXT (iopins) = av->n;
    A_INC (iopins);
  }

  /* check Vdd/GND */
  if (!found_vdd && n->Vdd) {
    found_vdd = 1;
    if (n->Vdd->e && list_length (n->Vdd->e) > 0) {
      emit_one_pin (a, fp, config_get_string ("net.global_vdd"),
		    1, "POWER", blob, n->Vdd);

    A_NEW (iopins, node_t *);
    A_NEXT (iopins) = n->Vdd;
    A_INC (iopins);
    }
  }

  if (!found_gnd && n->GND) {
    found_gnd = 1;
    if (n->GND->e && list_length (n->GND->e) > 0) {
      emit_one_pin (a, fp, config_get_string ("net.global_gnd"),
		    1, "GROUND", blob, n->GND);

      A_NEW (iopins, node_t *);
      A_NEXT (iopins) = n->GND;
      A_INC (iopins);
    }
  }

  /* read non-pin metal */

  if (blob->getRead ()) {
    list_t *l;
    Rectangle bloatbox = blob->getBloatBBox ();
    TransformMat mat;
    mat.translate (-bloatbox.llx(), -bloatbox.lly());
    l = blob->searchAllMetal (&mat);
    if (emit_layer_rects (fp, l, iopins, A_LEN (iopins))) {
      fprintf (fp, "    END\n");
    }
    LayoutBlob::searchFree (l);
  }
  else {
    /* XXX: add obstructions for metal layers; in reality we need to
       add the routed metal and then grab that here */
    RoutingMat *m1 = Technology::T->metal[0];
    int pinspc = MAX (m1->getPitch(), _pin_metal->getPitch());
    Rectangle rbloatbox = blob->getBloatBBox ();
    if ((rbloatbox.wy() > 6*pinspc) &&
	(rbloatbox.wx() > 2*_pin_metal->getPitch())) {
      fprintf (fp, "    OBS\n");
      fprintf (fp, "      LAYER %s ;\n", m1->getLEFName());
      fprintf (fp, "         RECT %.6f %.6f %.6f %.6f ;\n",
	       scale*((rbloatbox.llx() - bloatbox.llx()) + _pin_metal->getPitch()),
	       scale*((rbloatbox.lly() - bloatbox.lly()) + 3*pinspc),
	       scale*((rbloatbox.urx() - bloatbox.llx()) - _pin_metal->getPitch()),
	       scale*((rbloatbox.ury() - bloatbox.lly()) - 3*pinspc));
      fprintf (fp, "    END\n");
    }
  }

  emit_footer (fp, macroname);

  if (fpcell) {
    _emitLocalWellLEF (fpcell, p);
  }

  A_FREE (iopins);
  
  return 1;
}


void ActStackLayout::_computeWell (LayoutBlob *blob, int flavor, int type,
				       long *llx, long *lly, long *urx, long *ury, int is_welltap)
{
  TransformMat mat;

  WellMat *w = Technology::T->well[type][flavor];

  if (!blob || !w) {
    *llx = 0;
    *lly = 0;
    *urx = -1;
    *ury = -1;
    return;
  }

  Rectangle bloatbox = blob->getBloatBBox ();
  mat.translate (-bloatbox.llx(), -bloatbox.lly());

  list_t *tiles;
  if (is_welltap) {
    tiles = blob->search (TILE_FLGS_TO_ATTR(flavor,type,WDIFF_OFFSET), &mat);
  }
  else {
    tiles = blob->search (TILE_FLGS_TO_ATTR(flavor,type,DIFF_OFFSET), &mat);
  }
  
  long wllx, wlly, wurx, wury;

  LayoutBlob::searchBBox (tiles, &wllx, &wlly, &wurx, &wury);
  LayoutBlob::searchFree (tiles);
  if (wurx >= wllx) {
    /* bloat the region based on well overhang */
    if (is_welltap) {
      wllx -= w->getOverhangWelldiff();
      wlly -= w->getOverhangWelldiff();
      wurx += w->getOverhangWelldiff();
      wury += w->getOverhangWelldiff();
    }
    else {
      wllx -= w->getOverhang();
      wlly -= w->getOverhang();
      wurx += w->getOverhang();
      wury += w->getOverhang();
    }

    if (type == EDGE_PFET) {
      if (wlly+bloatbox.lly() > 0) {
	wlly = -bloatbox.lly();
      }
    }
    else {
      if (wury+bloatbox.lly() < 0) {
	wury = -bloatbox.lly();
      }
    }
    if (is_welltap) {
      if (type == EDGE_PFET) {
	wlly += Technology::T->welltap_adjust;
      }
      else {
	wury += Technology::T->welltap_adjust;
      }
    }
    *llx = wllx;
    *lly = wlly;
    *urx = wurx;
    *ury = wury;
  }
  else {
    *llx = 0;
    *lly = 0;
    *urx = -1;
    *ury = -1;
  }
}

/*
 * Called from LEF generation pass
 */
void ActStackLayout::_emitLocalWellLEF (FILE *fp, Process *p)
{
  netlist_t *n;

  if (!p) {
    return;
  }

  LayoutBlob *blob = getLayout (p);
  if (!blob) {
    return;
  }

  n = nl->getNL (p);
  if (!n) {
    return;
  }

  Rectangle bloatbox = blob->getBloatBBox ();

  if (bloatbox.empty ()) {
    /* no layout */
    return;
  }

  double scale = Technology::T->scale/1000.0;

  fprintf (fp, "MACRO ");
  a->mfprintfproc (fp, p);
  fprintf (fp, "\n");

  /*for (int lef=0; lef < 2; lef++)*/ {
  int lef = 0;
  fprintf (fp, "    VERSION ");
  a->mfprintfproc (fp, p);
  if (lef == 1) {
    fprintf (fp, "_plug");
  }
  fprintf (fp, "\n");
  if (lef == 0) {
    fprintf (fp, "        UNPLUG\n");
  }
  else {
    fprintf (fp, "        PLUG\n");
  }

  for (int i=0; i < Technology::T->num_devs; i++) {
    for (int j=0; j < 2; j++) {
      long wllx, wlly, wurx, wury;
      _computeWell (blob, i, j, &wllx, &wlly, &wurx, &wury);

      if (wllx < wurx && wlly < wury) {
	fprintf (fp, "        LAYER %s ;\n", Technology::T->well[j][i]->getName());
	fprintf (fp, "        RECT %.6f %.6f %.6f %.6f ;\n",
		 scale*wllx, scale*wlly, scale*wurx, scale*wury);
	fprintf (fp, "        END\n");
      }
    }
  }
  fprintf (fp, "    END VERSION\n");
  }
  
  fprintf (fp, "END ");
  a->mfprintfproc (fp, p);
  fprintf (fp, "\n\n");

  return;
}


void ActStackLayout::emitLEFHeader (FILE *fp)
{
  double scale = Technology::T->scale/1000.0;

  if (_lef_header) {
    return;
  }
  _lef_header = 1;
  
  /* -- lef header -- */
  fprintf (fp, "VERSION %s ;\n\n", _version);
  fprintf (fp, "BUSBITCHARS \"[]\" ;\n\n");
  fprintf (fp, "DIVIDERCHAR \"/\" ;\n\n");
  fprintf (fp, "UNITS\n");
  fprintf (fp, "    DATABASE MICRONS %d ;\n", _micron_conv);
  fprintf (fp, "END UNITS\n\n");

  fprintf (fp, "MANUFACTURINGGRID %.6f ; \n\n", _manufacturing_grid);
  fprintf (fp, "CLEARANCEMEASURE EUCLIDEAN ; \n\n");
  fprintf (fp, "USEMINSPACING OBS ON ; \n\n");

  fprintf (fp, "SITE CoreSite\n");
  fprintf (fp, "    CLASS CORE ;\n");
  fprintf (fp, "    SIZE %.6f BY %.6f ;\n",
	   _m_align_x->getPitch()*scale,
	   _m_align_y->getPitch()*scale);
  fprintf (fp, "END CoreSite\n\n");

  int metal_start, metal_end;

  metal_start = 0;
  metal_end = Technology::T->nmetals-1;

  if (config_exists ("lefdef.routing_metal")) {
    if (config_get_table_size ("lefdef.routing_metal") != 2) {
      warning ("lefdef.routing_metal: invalid config, needs two values only");
    }
    else {
      int *tmp = config_get_table_int ("lefdef.routing_metal");
      if (tmp[0] <= tmp[1] && tmp[0] >= 1) {
	metal_start = tmp[0]-1;
	metal_end = tmp[1]-1;
      }
      else {
	warning ("lefdef.routing_metal: invalid routing metal range; ignored");
      }
    }
  }
  
  int i;
  for (i=0; i < Technology::T->nmetals; i++) {
    RoutingMat *mat = Technology::T->metal[i];
    fprintf (fp, "LAYER %s\n", mat->getLEFName());

    if (i < metal_start || i > metal_end) {
      fprintf (fp, "   TYPE MASTERSICE ;\n");
    }
    else {
      fprintf (fp, "   TYPE ROUTING ;\n");

      fprintf (fp, "   DIRECTION %s ;\n",
	       IS_METAL_HORIZ(i+1) ? "HORIZONTAL" : "VERTICAL");
      fprintf (fp, "   MINWIDTH %.6f ;\n", mat->getLEFWidth()*scale);
      if (mat->minArea() > 0) {
	fprintf (fp, "   AREA %.6f ;\n", mat->minArea()*scale*scale);
      }
      fprintf (fp, "   WIDTH %.6f ;\n", mat->getLEFWidth()*scale);

      RangeTable *maxwidths = NULL;

      switch (mat->complexSpacingMode()) {
      case -1:
	/* even in this case, emit a spacing table since the open
	   source tools don't seem to work otherwise (?)
	*/
#if 0
	fprintf (fp, "   SPACING %.6f ;\n", mat->minSpacing()*scale);
#endif    
	fprintf (fp, "   SPACINGTABLE\n");
	fprintf (fp, "      PARALLELRUNLENGTH 0.0\n");
	fprintf (fp, "      WIDTH 0.0 %.6f ;\n", mat->minSpacing()*scale);
	break;

      case 0:
	maxwidths = mat->getRunTable (mat->numRunLength());
	/* parallel run length */
	fprintf (fp, "   SPACINGTABLE\n");
	fprintf (fp, "      PARALLELRUNLENGTH 0.0");
	for (int k=0; k < mat->numRunLength(); k++) {
	  fprintf (fp, " %.6f", mat->getRunLength(k)*scale);
	}
	fprintf (fp, "\n");
	/* XXX: assumption: widths are always in the range table for
	   the maximum width parallel run length rules */
	for (int k=0; k < maxwidths->size(); k++) {
	  int width;

	  if (k == 0) {
	    width = 0;
	  }
	  else {
	    /* the start of the next range */
	    width = maxwidths->range_threshold (k-1)+1;
	  }
	  fprintf (fp, "      WIDTH %.6f ", width*scale);
	  for (int l=0; l <= mat->numRunLength(); l++) {
	    RangeTable *sp = mat->getRunTable (l);
	    fprintf (fp, " %.6f", (*sp)[width+1]*scale);
	  }
	  if (k == maxwidths->size()-1) {
	    fprintf (fp, " ;");
	  }
	  fprintf (fp, "\n");
	}
	break;
      case 1:
	maxwidths = mat->getRunTable (mat->numRunLength()-1);
	fprintf (fp, "   SPACINGTABLE TWOWIDTHS\n");
	for (int k=0; k < maxwidths->size(); k++) {
	  int width;

	  if (k == 0) {
	    width = 0;
	  }
	  else {
	    width = maxwidths->range_threshold (k-1);
	  }
	
	  RangeTable *sp = mat->getRunTable (k);

	  fprintf (fp, "      WIDTH %.6f ", width*scale);
	  if (mat->getRunLength (k) != -1) {
	    fprintf (fp, "   PRL %.6f ", mat->getRunLength (k)*scale);
	  }
	  else {
	    fprintf (fp, "              ");
	  }
	  for (int l=0; l < maxwidths->size(); l++) {
	    if (l == 0) {
	      width = 0;
	    }
	    else {
	      width = maxwidths->range_threshold (l-1);
	    }
	    fprintf (fp, " %.6f", (*sp)[width+1]*scale);
	  }
	  if (k == maxwidths->size()-1) {
	    fprintf (fp, " ;");
	  }
	  fprintf (fp, "\n");
	}
	break;
      default:
	fatal_error ("Unknown runlength_mode %d\n", mat->complexSpacingMode());
	break;
      }

      if (mat->numInfluence() > 0) {
	int *table = mat->getInfluence();
	fprintf (fp, "   SPACINGTABLE INFLUENCE\n");
	for (int j=0; j < mat->numInfluence(); j++) {
	  fprintf (fp, "      WIDTH %.6f WITHIN %.6f SPACING %.6f ",
		   table[3*j]*scale, table[3*j+1]*scale, table[3*j+2]*scale);
	  if (j == mat->numInfluence()-1) {
	    fprintf (fp, " ;");
	  }
	  fprintf (fp, "\n");
	}
      }
    
      fprintf (fp, "   PITCH %.6f %.6f ;\n",
	       mat->getPitch()*scale, mat->getPitch()*scale);

      /* antenna rules */
      if (mat->getAntenna() > 0 || mat->getAntennaDiff() > 0) {
	fprintf (fp, "   ANTENNAMODEL OXIDE1 ;\n");
	if (mat->getAntenna() > 0) {
	  fprintf (fp, "   ANTENNAAREARATIO %.6f ;\n", mat->getAntenna());
	}
	if (mat->getAntennaDiff() > 0) {
	  fprintf (fp, "   ANTENNADIFFAREARATIO %.6f ;\n", mat->getAntennaDiff());
	}
      }
    }
    
    fprintf (fp, "END %s\n\n", mat->getLEFName());


    if (i != Technology::T->nmetals - 1) {
      Contact *vup = mat->getUpC();
      fprintf (fp, "LAYER %s\n", vup->getName());
      fprintf (fp, "    TYPE CUT ;\n");
      fprintf (fp, "    SPACING %.6f ;\n", scale*vup->minSpacing());
      fprintf (fp, "    WIDTH %.6f ;\n",  scale*vup->getLEFWidth());
      /* enclosure rules */
      if (vup->isSym()) {
	fprintf (fp, "    ENCLOSURE ABOVE %.6f %.6f ;\n",
		 scale*vup->getSymUp(), scale*vup->getSymUp());
	fprintf (fp, "    ENCLOSURE BELOW %.6f %.6f ;\n",
		 scale*vup->getSym(), scale*vup->getSym());
      }
      else {
	fprintf (fp, "    ENCLOSURE ABOVE %.6f %.6f ;\n",
		 scale*vup->getAsymUp(), scale*vup->getSymUp());
	fprintf (fp, "    ENCLOSURE BELOW %.6f %.6f ;\n",
		 scale*vup->getAsym(), scale*vup->getSym());
      }

      /* antenna rules */
      if (vup->getAntenna() > 0 || vup->getAntennaDiff() > 0) {
	fprintf (fp, "   ANTENNAMODEL OXIDE1 ;\n");
	if (vup->getAntenna() > 0) {
	  fprintf (fp, "   ANTENNAAREARATIO %.6f ;\n", vup->getAntenna());
	}
	if (vup->getAntennaDiff() > 0) {
	  fprintf (fp, "   ANTENNADIFFAREARATIO %.6f ;\n", vup->getAntennaDiff());
	}
      }
      
      fprintf (fp, "END %s\n\n", vup->getName());
    }
  }

  /* -- overlap layer -- */
  fprintf (fp, "LAYER OVERLAP\n");
  fprintf (fp, "   TYPE OVERLAP ;\n");
  fprintf (fp, "END OVERLAP\n");
  
  fprintf (fp, "\n");

  for (i=0; i < Technology::T->nmetals-1; i++) {
    RoutingMat *mat = Technology::T->metal[i];
    Contact *vup = mat->getUpC();
    double scale = Technology::T->scale/1000.0;
    double w, w2;
    int nvias = 1;

    if (vup->isAsym()) {
      nvias = 3;
    }

    for (int j=0; j < nvias; j++) {
      fprintf (fp, "VIA %s_C%s\n", vup->getName(),
	       (j == 0 ? " DEFAULT" : (j == 1 ? "h" : "v")));

      w = (vup->getLEFWidth() + 2*vup->getSym())*scale/2;
      if (vup->isAsym()) {
	w2 = (vup->getLEFWidth() + 2*vup->getAsym())*scale/2;
      }
      else {
	w2 = w;
      }
      if (w2 < w) {
	fatal_error ("Asymmetric via overhang for %s is smaller than the minimum overhang", vup->getName());
      }
      
      fprintf (fp, "   LAYER %s ;\n", mat->getLEFName());
      if (IS_METAL_HORIZ (i+1) || (j == 1)) {
	fprintf (fp, "     RECT %.6f %.6f %.6f %.6f ;\n", -w2, -w, w2, w);
      }
      else {
	fprintf (fp, "     RECT %.6f %.6f %.6f %.6f ;\n", -w, -w2, w, w2);
      }

      w = vup->getLEFWidth()*scale/2;
      fprintf (fp, "   LAYER %s ;\n", vup->getName());
      fprintf (fp, "     RECT %.6f %.6f %.6f %.6f ;\n", -w, -w, w, w);

      w = (vup->getLEFWidth() + 2*vup->getSymUp())*scale/2;
      if (vup->isAsym()) {
	w2 = (vup->getLEFWidth() + 2*vup->getAsymUp())*scale/2;
      }
      else {
	w2 = w;
      }
      if (w2 < w) {
	fatal_error ("Asymmetric via overhang for %s is smaller than the minimum overhang", vup->getName());
      }
    
      fprintf (fp, "   LAYER %s ;\n", Technology::T->metal[i+1]->getLEFName());
      if (IS_METAL_HORIZ (i+2) || (j == 1)) {
	fprintf (fp, "     RECT %.6f %.6f %.6f %.6f ;\n", -w2, -w, w2, w);
      }
      else {
	fprintf (fp, "     RECT %.6f %.6f %.6f %.6f ;\n", -w, -w2, w, w2);
      }      
      fprintf (fp, "END %s_C%s\n\n", vup->getName(),
	       (j == 0 ? "" : (j == 1 ? "h" : "v")));

    }
    
    if (vup->viaGenerate()) {
      fprintf (fp, "VIARULE %s_gen GENERATE\n", vup->getName());
      fprintf (fp, "   LAYER %s ;\n", mat->getLEFName());
      if (vup->isAsym()) {
	fprintf (fp, "   ENCLOSURE %.6f %.6f ;\n",
		 vup->getAsym()*scale, vup->getSym()*scale);
      }
      else {
	fprintf (fp, "   ENCLOSURE %.6f %.6f ;\n",
		 vup->getSym()*scale, vup->getSym()*scale);
      }
      fprintf (fp, "   LAYER %s ;\n", Technology::T->metal[i+1]->getLEFName());
      if (vup->isAsym()) {
	fprintf (fp, "   ENCLOSURE %.6f %.6f ;\n",
		 vup->getAsymUp()*scale, vup->getSymUp()*scale);

      }
      else {
	fprintf (fp, "   ENCLOSURE %.6f %.6f ;\n",
		 vup->getSymUp()*scale, vup->getSymUp()*scale);
      }

      w = vup->getLEFWidth()*scale/2;
      fprintf (fp, "   LAYER %s ;\n", vup->getName());
      fprintf (fp, "     RECT %.6f %.6f %.6f %.6f ;\n", -w, -w, w, w);

      fprintf (fp, "   SPACING %.6f BY %.6f ;\n",
	       vup->viaGenX()*scale, vup->viaGenY()*scale);

      fprintf (fp, "END %s_gen\n\n", vup->getName());
    }
  }

}

void ActStackLayout::emitWellHeader (FILE *fp)
{
  double scale = Technology::T->scale/1000.0;

  if (_cell_header) {
    return;
  }
  _cell_header = 1;

  fprintf (fp, "LAYER LEGALIZER\n");
  fprintf (fp, "   SAME_DIFF_SPACING %.6f ;\n", Technology::T->getMaxSameDiffSpacing()*scale);
  fprintf (fp, "   ANY_DIFF_SPACING %.6f ;\n", Technology::T->getMaxDiffSpacing()*scale);
  fprintf (fp, "   WELLTAP_ADJUST %.6f ;\n", snap_up_y (Technology::T->welltap_adjust)*scale);
  fprintf (fp, "END LEGALIZER\n\n");
  
  for (int i=0; i < Technology::T->num_devs; i++) {
    for (int j = 0 ; j < 2; j++) {
      WellMat *w = Technology::T->well[j][i];
      if (w) {
	fprintf (fp, "LAYER %s\n", w->getName());
	fprintf (fp, "    MINWIDTH %.6f ;\n", w->minWidth()*scale);
	fprintf (fp, "    SPACING %.6f ;\n", w->minSpacing(i)*scale);
	fprintf (fp, "    OPPOSPACING %.6f ;\n", w->oppSpacing(i)*scale);
	if (w->maxPlugDist() > 0) {
	  fprintf (fp, "    MAXPLUGDIST %.6f ;\n", w->maxPlugDist()*scale);
	}
	fprintf (fp, "    OVERHANG %.6f ;\n", w->getOverhang()*scale);
	fprintf (fp, "END %s\n\n", w->getName());
      }
    }
  }
}


LayoutBlob *ActStackLayout::getLayout (Process *p)
{
  if (!me->completed ()) {
    return 0;
  }
  if (!p) return NULL;
  return (LayoutBlob *) me->getMap (p);
}


/*
  Returns the max height of all layout blocks within p that have not
  been visited yet 
*/
void ActStackLayout::_maxHeightlocal (Process *p)
{
  LayoutBlob *b;
  long llx, lly, urx, ury;

  if (getBBox (p, &llx, &lly, &urx, &ury)) {
    if (lly < _ymin) {
      _ymin = lly;
    }
    if (ury > _ymax) {
      _ymax = ury;
    }
  }
}


/*------------------------------------------------------------------------
 *  
 *  Routines for DEF file generation
 *
 *------------------------------------------------------------------------
 */
static int _instcount;
static double _areacount;
static double _areastdcell;
static int _maximum_height;

static void count_inst (void *x, ActId *prefix, UserDef *u)
{
  ActStackLayout *ap = (ActStackLayout *)x;
  LayoutBlob *b;
  long llx, lly, urx, ury;
  Process *p;

  p = dynamic_cast<Process *>(u);
  if (!p) {
    return;
  }

  b = ap->getLayout (p);
  if (ap->getBBox (p, &llx, &lly, &urx, &ury)) {
    if ((llx > urx) || (lly > ury)) return;

    if (b) {
      b->incCount();
    }
    else {
      ap->incBBox (p);
    }
    _instcount++;
    
    _areacount += (urx - llx + 1)*(ury - lly + 1);
    _areastdcell += (urx - llx + 1)*_maximum_height;
  }
}

/*
 * Flat instance dump
 */
static Act *global_act;
static ActStackLayout *_alp;

static void dump_inst (void *x, ActId *prefix, UserDef *u)
{
  FILE *fp = (FILE *)x;
  char buf[10240];
  LayoutBlob *b;
  long llx, lly, urx, ury;

  Process *p = dynamic_cast<Process *>(u);
  if (!p) {
    return;
  }
  
  if (_alp->getBBox (p, &llx, &lly, &urx, &ury)) {
    if ((llx > urx) || (lly > ury)) return;

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


static int print_net (Act *a, FILE *fp, ActId *prefix, act_local_net_t *net,
		      int toplevel, int pins)
{
  char buf[10240];
  Assert (net, "Why are you calling this function?");
  if (net->skip) return 0;
  if (net->port && (!toplevel || !pins)) return 0;

  if (A_LEN (net->pins) < 1) return 0;

  fprintf (fp, "- ");
  if (prefix) {
    prefix->sPrint (buf, 10240);
    global_act->mfprintf (fp, "%s.", buf);
    //prefix->Print (fp);
    //fprintf (fp, ".");
  }
  ActId *tmp = net->net->primary()->toid();
  tmp->sPrint (buf, 10240);
  global_act->mfprintf (fp, "%s", buf);
  //tmp->Print (fp);
  delete tmp;

  fprintf (fp, "\n  ");

  if (net->port) {
    //fprintf (fp, " ( PIN top_iopin%d )", toplevel-1);
    ActId *tmp = net->net->toid();
    fprintf (fp, " ( PIN ");
    tmp->Print (fp);
    fprintf (fp, " )");
    delete tmp;
  }
  else if (net->net->isglobal() && pins) {
    ActId *tmp = net->net->toid();
    tmp->sPrint (buf, 10240);
    delete tmp;
    if ((strcmp (buf, "Vdd") == 0) || (strcmp (buf, "GND") == 0)) {
      /* omit */
    }
    else {
      fprintf (fp, " ( PIN %s )", buf);
    }
  }

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

static unsigned long netcount;

static ActBooleanizePass *boolinfo;

void _collect_emit_nets (Act *a, ActId *prefix, Process *p, FILE *fp, int do_pins)
{
  Assert (p->isExpanded(), "What are we doing");

  act_boolean_netlist_t *n = boolinfo->getBNL (p);
  Assert (n, "What!");

  /* first, print my local nets */
  for (int i=0; i < A_LEN (n->nets); i++) {
    if (print_net (a, fp, prefix, &n->nets[i], prefix == NULL ? (i+1) : 0, do_pins)) {
      netcount++;
    }
  }

  ActUniqProcInstiter i(p->CurScope());

  for (i = i.begin(); i != i.end(); i++) {
    ValueIdx *vx = (*i);
    ActId *newid;
    ActId *cpy;
    
    Process *instproc = dynamic_cast<Process *>(vx->t->BaseType ());

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
	if (vx->isPrimary (as->index())) {
	  Array *x = as->toArray();
	  newid->setArray (x);
	  _collect_emit_nets (a, cpy, instproc, fp, do_pins);
	  delete x;
	  newid->setArray (NULL);
	}
	as->step();
      }
      delete as;
    }
    else {
      _collect_emit_nets (a, cpy, instproc, fp, do_pins);
    }
    delete cpy;
  }
  return;
}


void ActStackLayout::emitDEFHeader (FILE *fp, Process *p)
{
  /* -- def header -- */
  fprintf (fp, "VERSION %s ;\n\n", _version);
  fprintf (fp, "BUSBITCHARS \"[]\" ;\n\n");
  fprintf (fp, "DIVIDERCHAR \"/\" ;\n\n");
  fprintf (fp, "DESIGN ");

  a->mfprintfproc (fp, p);
  fprintf (fp, " ;\n");
  
  fprintf (fp, "\nUNITS DISTANCE MICRONS %d ;\n\n", _micron_conv);
}

void ActStackLayout::emitDEF (FILE *fp, Process *p, double pad,
				  double ratio, int do_pins)
{
  ActDynamicPass *dp = dynamic_cast<ActDynamicPass *>(me);
  Assert (dp, "What?");
  
  emitDEFHeader (fp, p);
  
  /* -- get area -- */
  ActPass *tap = a->pass_find ("apply");
  if (!tap) {
    tap = new ActApplyPass (a);
  }
  ActApplyPass *ap = dynamic_cast<ActApplyPass *>(tap);

  _instcount = 0;
  _areacount = 0;
  _areastdcell = 0;
  _maximum_height = dp->getIntParam ("cell_maxheight");
  ap->setCookie (this);
  ap->setInstFn (count_inst);
  ap->run (p);
  count_inst (this, NULL, p); // oops!

  _total_instances = _instcount;
  _total_area = _areacount;
  _total_stdcell_area = _areastdcell;

  _total_area *= pad;
  _total_stdcell_area *= pad;

  double sidey = sqrt (_total_area/ratio);
  double sidex = sidey*ratio;

  double unit_conv = Technology::T->scale*_micron_conv/1000.0;

  sidex *= unit_conv;
  sidey *= unit_conv;

  int pitchx, pitchy, track_gap;
  int nx, ny;

  pitchx = _m_align_x->getPitch();
  pitchy = _m_align_y->getPitch();

  pitchx *= unit_conv;
  pitchy *= unit_conv;

#define TRACK_HEIGHT 18

  track_gap = pitchy * TRACK_HEIGHT;

  nx = (sidex + pitchx - 1)/pitchx;
  ny = (sidey + track_gap - 1)/track_gap;

  fprintf (fp, "DIEAREA ( %d %d ) ( %d %d ) ;\n",
	   10*pitchx, track_gap,
	   (10+nx)*pitchx, (1+ny)*track_gap);
  
  //  fprintf (fp, "\nROW CORE_ROW_0 CoreSite %d %d N DO %d BY 1 STEP %d 0 ;\n\n",
  //	   10*pitchx, pitchy, ny, track_gap);

  /* routing tracks: metal1, metal2 */

  for (int i=0; i < Technology::T->nmetals; i++) {
    RoutingMat *mx = Technology::T->metal[i];
    int pitchxy = mx->getPitch()*unit_conv;
    int startxy = mx->getLEFWidth()*unit_conv/2;
    
    int ntracksx = (pitchx*nx)/pitchxy;
    int ntracksy = (track_gap*ny)/pitchxy;

    /* vertical tracks */
    if (ntracksx > 0) {
      fprintf (fp, "TRACKS X %d DO %d STEP %d LAYER %s ;\n",
	       10*pitchx + startxy, ntracksx, pitchxy, mx->getLEFName());
    }
    /* horizontal tracks */
    if (ntracksy > 0) {
      fprintf (fp, "TRACKS Y %d DO %d STEP %d LAYER %s ;\n",
	       track_gap + startxy, ntracksy, pitchxy, mx->getLEFName());
    }
    fprintf (fp, "\n");
  }

  /* -- instances  -- */
  fprintf (fp, "COMPONENTS %d ;\n", _total_instances);
  ap->setCookie (fp);
  ap->setInstFn (dump_inst);
  global_act = a;
  _alp = this;
  ap->run (p);
  fprintf (fp, "END COMPONENTS\n\n");


  /* -- pins -- */
  ActPass *anlp = a->pass_find ("prs2net");
  Assert (anlp, "What?");
  ActNetlistPass *nl = dynamic_cast<ActNetlistPass *>(anlp);
  Assert (nl, "What?");

  netlist_t *act_ckt = nl->getNL (p);
  Assert (act_ckt, "No circuit?");
  act_boolean_netlist_t *act_bnl = act_ckt->bN;

  boolinfo = dynamic_cast<ActBooleanizePass *>(a->pass_find ("booleanize"));

  if (do_pins) {
    int num_pins = 0;
    const char *gvdd = config_get_string ("net.global_vdd");
    const char *ggnd = config_get_string ("net.global_gnd");

    for (int i=0; i < A_LEN (act_bnl->ports); i++) {
      if (act_bnl->ports[i].omit) continue;
      num_pins++;
    }

        /* gloal nets */
    for (int i=0; i < A_LEN (act_bnl->nets); i++) {
      if (act_bnl->nets[i].net->isglobal()) {
	char buf[100];
	ActId *tmp = act_bnl->nets[i].net->toid();
	tmp->sPrint (buf, 100);
	if (strcmp (buf, gvdd) == 0 || strcmp (buf, ggnd) == 0) {
	  /* nothing for power supplies */
	}
	else {
	  num_pins++;
	}
	delete tmp;
      }
    }

    fprintf (fp, "PINS %d ;\n", num_pins);
    num_pins = 0;
    for (int i=0; i < A_LEN (act_bnl->ports); i++) {
      if (act_bnl->ports[i].omit) continue;
      Assert (act_bnl->ports[i].netid != -1, "What?");
      ActId *tmp = act_bnl->nets[act_bnl->ports[i].netid].net->toid();
      //fprintf (fp, "- top_iopin%d + NET ", act_bnl->ports[i].netid);
      // we can use the port name here! no weird numbers now!
      fprintf (fp, "- ");
      tmp->Print (fp);
      fprintf (fp, " + NET ");
      tmp->Print (fp);
      delete tmp;
      if (act_bnl->ports[i].input) {
	fprintf (fp, " + DIRECTION INPUT + USE SIGNAL ");
      }
      else {
	fprintf (fp, " + DIRECTION OUTPUT + USE SIGNAL ");
      }
      /* placement directives will go here */
      fprintf (fp, " ;\n");
    }

    /* gloal nets */
    for (int i=0; i < A_LEN (act_bnl->nets); i++) {
      if (act_bnl->nets[i].net->isglobal()) {
	char buf[100];
	ActId *tmp = act_bnl->nets[i].net->toid();
	tmp->sPrint (buf, 100);
	if (strcmp (buf, gvdd) == 0 || strcmp (buf, ggnd) == 0) {
	  /* nothing */
	}
	else {
	  //fprintf (fp, "- top_iopin%d + NET ", i);
	  fprintf (fp, "- ");
	  tmp->Print (fp);
	  fprintf (fp, " + NET ");
	  tmp->Print (fp);
	  fprintf (fp, " + DIRECTION INPUT + USE SIGNAL ;\n");
	}
	delete tmp;
      }
    }
  }
  else {
    fprintf (fp, "PINS 0 ;\n");
  }
  fprintf (fp, "END PINS\n\n");

  netcount = 0;
  unsigned long pos = 0;

  /* -- nets -- */
  pos = ftell (fp);
  fprintf (fp, "NETS %012lu ;\n", netcount);
  /*
    Output format: 

    - net1237
    ( inst5638 A ) ( inst4678 Y )
    ;
  */
  _collect_emit_nets (a, NULL, p, fp, do_pins);
  
  fprintf (fp, "END NETS\n\n");
  fprintf (fp, "END DESIGN\n");

  fseek (fp, pos, SEEK_SET);
  
  fprintf (fp, "NETS %12lu ;\n", netcount);
  fseek (fp, 0, SEEK_END);
  global_act = NULL;
}


void ActStackLayout::_reportLocalStats(Process *p)
{
  LayoutBlob *blob = getLayout (p);
  ActDynamicPass *dp = dynamic_cast<ActDynamicPass *>(me);

  long bllx, blly, burx, bury;
  long count;
  if (!getBBox (p, &bllx, &blly, &burx, &bury)) {
    return;
  }
  if (bllx > burx || blly > bury) return;

  char *tmp = p->getns()->Name();

  if (tmp[0] == ':' && tmp[1] == ':' && tmp[2] == '\0') {
    tmp[0] = '\0';
  }
  double all_area = dp->getRealParam ("total_area");
  printf ("--- Cell %s::%s ---\n", tmp, p->getName());
  FREE (tmp);

  unsigned long area = (burx-bllx+1)*(bury-blly+1);
  if (blob) {
    count = blob->getCount();
  }
  else {
    count = getBBoxCount (p);
  }
  printf ("  count=%lu; ", count);
  printf ("cell_area=%.3g um^2; ", area*Technology::T->scale/1000.0*
	  Technology::T->scale/1000.0);
  printf ("area: %.2f%%\n", area*count*100.0/all_area);

  if (blob) {
    netlist_t *mynl = nl->getNL (p);
    node_t *n;
    unsigned long ncount = 0;
    unsigned long ecount = 0;
    unsigned long keeper = 0;
    for (n = mynl->hd; n; n = n->next) {
      ncount++;
      listitem_t *li;
      edge_t *e;
      for (li = list_first (n->e); li; li = list_next (li)) {
	e = (edge_t *) list_value (li);
	if (e->keeper) {
	  keeper++;
	}
	else {
	  ecount++;
	}
      }
    }
    ecount /= 2;
    keeper /= 2;
  
    printf ("  nodes=%lu; ", ncount);
    printf ("fets: std=%lu; ", ecount);
    printf ("keeper=%lu\n", keeper);
  }
}
  

/*
  Returns LEF boundary in blob coordinate system
*/
LayoutBlob *ActStackLayout::computeLEFBoundary (LayoutBlob *b)
{
  long nllx, nlly, nurx, nury;

  if (!b) return NULL;

  Rectangle bloatbox = b->getBloatBBox ();
  if (bloatbox.empty()) {
    return b;
  }

#if 0
  printf ("\n");
  printf ("original: (%ld,%ld) -> (%ld,%ld)\n",
	  bloatbox.llx(), bloatbox.lly(),
	  bloatbox.urx(), bloatbox.ury());
  printf ("SNAP: %d and %d\n",  Technology::T->metal[1]->getPitch(),
	  Technology::T->metal[0]->getPitch());
#endif


  Assert (Technology::T->nmetals >= 3, "Hmm");

  nllx = snap_dn_x (bloatbox.llx()) - _extra_tracks_left*_m_align_x->getPitch();
  nurx = snap_up_x (bloatbox.urx()+1)-1 + _extra_tracks_right*_m_align_x->getPitch();

  nlly = snap_dn_y (bloatbox.lly()) - _extra_tracks_bot*_m_align_y->getPitch();
  nury = snap_up_y (bloatbox.ury()+1)-1 + _extra_tracks_top*_m_align_y->getPitch();

  LayoutBlob *box = new LayoutBlob (BLOB_BASE, NULL);
  box->setBBox (nllx, nlly, nurx, nury);

#if 0
  printf (" set: (%ld,%ld) -> (%ld,%ld)\n",
	  nllx, nlly, nurx, nury);
  box->getBloatBBox (&llx, &lly, &urx, &ury);
  printf ("test: (%ld,%ld) -> (%ld,%ld)\n", llx, lly, urx, ury);
  if (llx != nllx || lly != nlly ||
      urx != nurx || ury != nury) {
    printf ("++++++\n");
  }
#endif  
  
  /* add the boundary to the blob */
  LayoutBlob *bl = new LayoutBlob (BLOB_LIST);
  bl->appendBlob (b, BLOB_MERGE);

#if 0
  bl->getBloatBBox (&llx, &lly, &urx, &ury);
  printf ("next: (%ld,%ld) -> (%ld,%ld)\n",
	  llx,lly,urx, ury);
#endif
  
  bl->appendBlob (box, BLOB_MERGE);

#if 0
  bl->getBloatBBox (&llx, &lly, &urx, &ury);

  if (nllx != llx || nlly != lly ||
      nurx != urx || nury != ury) {
    printf ("*****\n");
  }
  printf ("    : (%ld,%ld) -> (%ld,%ld)\n",
	  llx, lly, urx,ury);

  printf ("SIZE = (%ld, %ld)\n", urx - llx + 1, ury - lly + 1);
#endif  
  
  return bl;
}


/*
 * Assumed that if there is a notch, then the poly overhang out of the
 * notch is not more than the normal poly overhang...
 */
int ActStackLayout::_localdiffspace (Process *p)
{
  int poly_potential;
  int spc_default, spc2;
  int flavor = -1;
  int poly_overhang;
  PolyMat *pmat = Technology::T->poly;
  listitem_t *stki;

  list_t *stks = (list_t *)stk->getMap (p);
  netlist_t *n = nl->getNL (p);

  double la = n->leak_correct ? Layout::getLeakAdjust() : 0;
  
  if (!stks || list_length (stks) == 0) {
    return 0;
  }
#if 0
  printf ("computing local diffspace for %s...\n", p->getName());
#endif
  
  spc_default = 0;
  poly_overhang = 0;

  stki = list_first (stks);
  list_t *stklist = (list_t *) list_value (stki);

  poly_potential = 0;
  
  if (list_length (stklist) > 0) {
    listitem_t *si;
    /* dual stacks */

    for (si = list_first (stklist); si; si = list_next (si)) {
      struct gate_pairs *gp;
      gp = (struct gate_pairs *) list_value (si);
      if (gp->basepair) {
	if (gp->u.e.n && gp->u.e.p) {
	  poly_overhang = MAX (poly_overhang,
			       pmat->getOverhang (getlength (gp->u.e.n, la)));
	  poly_overhang = MAX (poly_overhang,
			       pmat->getOverhang (getlength (gp->u.e.p, la)));
	  if (gp->u.e.n->g != gp->u.e.p->g) {
	    poly_potential = 1;
	  }
	}
	if (gp->u.e.n) {
	  if (flavor != gp->u.e.n->flavor) {
	    flavor = gp->u.e.n->flavor;
	    int x = Technology::T->diff[EDGE_NFET][flavor]->getOppDiffSpacing(flavor);
	    spc_default = MAX (spc_default, x);
	  }
	}
	if (gp->u.e.p) {
	  if (flavor != gp->u.e.n->flavor) {
	    flavor = gp->u.e.n->flavor;
	    int x = Technology::T->diff[EDGE_PFET][flavor]->getOppDiffSpacing(flavor);
	    spc_default = MAX (spc_default, x);
	  }
	}
      }
      else {
	listitem_t *li;
	for (li = list_first (gp->u.gp); li; li = list_next (li)) {
	  struct gate_pairs *tmp;
	  tmp = (struct gate_pairs *) list_value (li);
	  Assert (tmp->basepair, "What?");
	  if (tmp->u.e.n && tmp->u.e.p) {
	    poly_overhang = MAX (poly_overhang,
				 pmat->getOverhang (getlength (tmp->u.e.n, la)));
	    poly_overhang = MAX (poly_overhang,
				 pmat->getOverhang (getlength (tmp->u.e.p, la)));
				 
	    if (tmp->u.e.n->g != tmp->u.e.p->g) {
	      poly_potential = 1;
	    }
	  }
	  if (tmp->u.e.n) {
	    if (flavor != tmp->u.e.n->flavor) {
	      flavor = tmp->u.e.n->flavor;
	      int x = Technology::T->diff[EDGE_NFET][flavor]->getOppDiffSpacing(flavor);
	      spc_default = MAX (spc_default, x);
	    }
	  }
	  if (tmp->u.e.p) {
	    if (flavor != tmp->u.e.p->flavor) {
	      flavor = tmp->u.e.p->flavor;
	      int x = Technology::T->diff[EDGE_PFET][flavor]->getOppDiffSpacing(flavor);
	      spc_default = MAX (spc_default, x);
	    }
	  }
	}
      }
    }
  }

  stki = list_next (stki);
  stklist = (list_t *) list_value (stki);
 
  if (stklist && list_length (stklist) > 0) {
    list_t *l = (list_t *) list_value (list_first (stklist));
    edge_t *e = (edge_t *) list_value (list_next (list_first (l)));
    int x = Technology::T->diff[EDGE_NFET][e->flavor]->getOppDiffSpacing(e->flavor);
    spc_default = MAX (spc_default, x);
  }

  stki = list_next (stki);
  list_t *stklist2 = (list_t *)  list_value (stki);

  if (stklist2 && list_length (stklist2) > 0) {
    list_t *l = (list_t *) list_value (list_first (stklist2));
    edge_t *e = (edge_t *) list_value (list_next (list_first (l)));
    int x = Technology::T->diff[EDGE_NFET][e->flavor]->getOppDiffSpacing(e->flavor);
    spc_default = MAX (spc_default, x);
  }

  if (stklist && stklist2 &&
      list_length (stklist) > 0 && list_length (stklist2) > 0) {
    poly_potential = 1;
  }
  
#if 0
  printf (" poly_pot = %d; def = %d; ", poly_potential, spc_default);
#endif  
  if (poly_potential) {
    spc_default = MAX (poly_overhang*2 + MAX (pmat->getEol(),
					      pmat->getSpacing(0)),
		       spc_default);
  }
#if 0
  printf (" final = %d\n", spc_default);
#endif  
  return spc_default;
}

int ActStackLayout::isEmpty (list_t *stk)
{
  if (!stk) return 1;
  
  list_t *dual, *n, *p;
  
  dual = (list_t *) list_value (list_first (stk));
  n = (list_t *) list_value (list_next (list_first (stk)));
  p = (list_t *) list_value (list_next (list_next (list_first (stk))));

  if (list_length (dual) > 0) return 0;
  if (n && list_length (n) > 0) return 0;
  if (p && list_length (p) > 0) return 0;
  
  return 1;
}


struct bbox_elem {
  long llx, lly, urx, ury;
  long count;
};

void ActStackLayout::setBBox (Process *p,
			      long llx, long lly, long urx, long ury)
{
  LayoutBlob *b;
  phash_bucket_t *pb;
  struct bbox_elem *be;

  b = getLayout (p);
  if (b) {
    warning ("Process `%s': setting bounding box for a cell with existing layout!",
	     p ? p->getName() : "-top-");
  }
  if (!boxH) {
    boxH = phash_new (4);
  }
  pb = phash_lookup (boxH, p);
  if (pb) {
    warning ("Process `%s': already has a BBox set!", p ? p->getName() : "-top-");
  }
  else {
    pb = phash_add (boxH, p);
    NEW (be, struct bbox_elem);
    pb->v = be;
  }
  be = (struct bbox_elem *) pb->v;
  be->llx = llx;
  be->lly = lly;
  be->urx = urx;
  be->ury = ury;
  be->count = 0;
}


int ActStackLayout::getBBox (Process *p, long *llx, long *lly,
			     long *urx, long *ury)
{
  LayoutBlob *b;
  phash_bucket_t *pb;
  struct bbox_elem *be;
  
  b = getLayout (p);
  if (b) {
    /* layout exists, use the bounding box from here */
    Rectangle r = b->getBloatBBox ();
    *llx = r.llx();
    *lly = r.lly();
    *urx = r.urx();
    *ury = r.ury();
    return 1;
  }
  if (boxH) {
    pb = phash_lookup (boxH, p);
    if (pb) {
      be = (struct bbox_elem *) pb->v;
      *llx = be->llx;
      *lly = be->lly;
      *urx = be->urx;
      *ury = be->ury;
      return 1;
    }
  }
  return 0;
}

void ActStackLayout::incBBox (Process *p) 
{
  phash_bucket_t *pb;
  struct bbox_elem *be;

  Assert (boxH, "What?");
  pb = phash_lookup (boxH, p);
  Assert (pb, "What?");
  be = (struct bbox_elem *) pb->v;
  be->count++;
}

long ActStackLayout::getBBoxCount (Process *p)
{
  phash_bucket_t *pb;
  struct bbox_elem *be;

  Assert (boxH, "What?");
  pb = phash_lookup (boxH, p);
  Assert (pb, "What?");
  be = (struct bbox_elem *) pb->v;
  return be->count;
}


int layout_runcmd (ActPass *_ap, const char *name)
{
  ActDynamicPass *ap = dynamic_cast<ActDynamicPass *> (_ap);
  ActStackLayout *lp;
  Process *p;
  Assert (ap, "What?");

  if (strcmp (name, "setbbox") != 0) {
    fprintf (stderr, "Unknown command `%s'!\n", name);
    return -1;
  }

  lp = (ActStackLayout *)ap->getPtrParam ("raw");

  if (!ap->completed()) {
    return 0;
  }

  const char *x = (const char *) ap->getPtrParam ("cell_name");
  if (!x) {
    return 0;
  }

  /* now find this process! */
  char buf[10240];

  ap->getAct()->unmangle_stringproc (x, buf, 10240);

  int w = ap->getIntParam ("cell_width");
  int h = ap->getIntParam ("cell_height");

  if (w == -1 || h == -1) {
    fprintf (stderr, "stk2layout pass: runcmd failed, w/h not found!\n");
    return 0;
  }

  int len = strlen (buf);
  if (buf[len-1] != '>' && len < 10238) {
    strcat (buf, "<>");
  }

  p = ap->getAct()->findProcess (buf);

  if (!p) {
    fprintf (stderr, "stk2layout pass: runcmd failed, could not find process `%s'", buf);
    return 0;
  }

  lp->setBBox (p, 0, 0, w, h);

  return 1;
}
