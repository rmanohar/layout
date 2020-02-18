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
#include <act/layout/stk_pass.h>
#include <act/layout/stk_layout.h>
#include <act/iter.h>
#include <act/passes.h>
#include <config.h>
#include <math.h>
#include <string.h>

#include <pwd.h>
#include <ctype.h>


static int first = 1;

static 
struct pathlist {
  char *path;
  struct pathlist *next;
} *hd = NULL, *tl;

static
char *expand (char *s)
{
  const char *path;
  char *t;
  char *ret;
  struct passwd *pwd;

  if (s[0] == '~' && *(s+1) == '/') {
    path = getenv ("HOME");
    if (!path) path = "";
    MALLOC (ret, char, strlen (s) + strlen(path)+ 5);
    strcpy (ret, path);
    strcat (ret, s+1);
  }
  else if (s[0] == '~') {
    t = s+1;
    while (*t && *t != '/') t++;
    if (!*t) fatal_error ("Invalid pathname!");
    *t = '\0';
    if (strcmp (s+1, "cad") == 0 && getenv ("CAD_HOME"))
      path = getenv ("CAD_HOME");
    else {
      pwd  = getpwnam (s+1);
      if (pwd->pw_dir == NULL) path = "";
      else path = pwd->pw_dir;
    }
    *t = '/';
    MALLOC (ret, char, strlen (t) + strlen (path) + 6);
    strcpy (ret, path);
    strcat (ret, t);
  }
  else {
    MALLOC (ret, char, strlen (s) + 6);
    strcpy (ret, s);
  }
  return ret;
}

static int addpath (char *s, int i)
{
  char *t;
  char c;

  if (s[i] == '"') i++;
  i--;
  do {
    i++;
    t = s+i;
    while (s[i] && !isspace(s[i]) && s[i] != '"' && s[i] != ':')
      i++;
    if (t != s+i) {
      c = s[i];
      s[i] = '\0';
      if (!hd) {
	MALLOC (hd, struct pathlist, 1);
	tl = hd;
	tl->next = NULL;
      }
      else {
	MALLOC (tl->next, struct pathlist, 1);
	tl = tl->next;
	tl->next = NULL;
      }
      MALLOC (tl->path, char, strlen (t)+1);
      strcpy (tl->path, t);
      s[i] = c;
    }
  } while (s[i] == ':');
  if (s[i] == '"') i++;
  return i;
}


/*
 * Initialize the search path
 */
static void _path_init (void)
{
  char buf[2];
  char *env;
  char *file;

  if (first) {
    /* first thing in the search path is the current directory */
    buf[0] = '.';
    buf[1] = '\0';
    addpath (buf, 0);

    /* next thing in the search path is $ACT_PATH */
    env = getenv ("ACT_PATH");
    if (env)
      addpath (env, 0);
    
    /* last item in the search path is $ACT_HOME */
    env = getenv ("ACT_HOME");
    if (env) {
      addpath (env,0);
    }
  }
  first = 0;
}

/*------------------------------------------------------------------------
 *
 *  lef_path_open --
 *
 *    Search through $ACT_PATH and $ACT_HOME/act to find a path name
 * where the file is located.
 *
 *------------------------------------------------------------------------
 */
static char *lef_path_open (char *name)
{
  struct pathlist *p;
  char *file, *trial;
  FILE *fp;

  _path_init ();

  p = hd;
  trial = expand(name);

  if ((fp = fopen (trial, "r"))) {
    fclose (fp);
    return trial;
  }
  FREE (trial);
  MALLOC (file, char, strlen (name) + 5);
  sprintf (file, "%s.lef", trial);
  if ((fp = fopen (file, "r"))) {
    fclose (fp);
    return file;
  }
  while (p) {
    MALLOC (file, char, strlen (p->path)+strlen(name)+11);
    strcpy (file, p->path);
    strcat (file, "/lef/");
    strcat (file, name);
    trial = expand (file);
    FREE (file);
    fp = fopen (trial, "r");
    if (fp) { 
      fclose (fp);
      return trial;
    }
    strcat (trial, ".lef");
    fp = fopen (trial, "r");
    if (fp) {
      fclose (fp);
      return trial;
    }
    FREE (trial);
    p = p->next;
  }
  return NULL;
}



static unsigned long snap_to (unsigned long w, unsigned long pitch)
{
  if (w % pitch != 0) {
    w += pitch - (w % pitch);
  }
  return w;
}

ActStackLayoutPass::ActStackLayoutPass(Act *a) : ActPass (a, "stk2layout")
{
  layoutmap = NULL;
  
  if (!a->pass_find ("net2stk")) {
    stk = new ActStackPass (a);
  }
  AddDependency ("net2stk");

  ActPass *pass = a->pass_find ("net2stk");
  Assert (pass, "Hmm...");
  stk = dynamic_cast<ActStackPass *>(pass);
  Assert (stk, "Hmm too...");

  _total_area = -1;
  _total_stdcell_area = -1;
  _total_instances = -1;
  _maxht = -1;
}

void ActStackLayoutPass::cleanup()
{
  if (layoutmap) {
    std::map<Process *, LayoutBlob *>::iterator it;
    for (it = (*layoutmap).begin(); it != (*layoutmap).end(); it++) {
      /* something here */
    }
    delete layoutmap;
  }
}

ActStackLayoutPass::~ActStackLayoutPass()
{
  cleanup();
}

#define EDGE_FLAGS_LEFT 0x1
#define EDGE_FLAGS_RIGHT 0x2

#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif


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
static int _lambda_to_scale;
static int getwidth (int idx, edge_t *e)
{
  if (e->type == EDGE_NFET) {
    return EDGE_WIDTH (e,idx)*_lambda_to_scale;
  }
  else {
    return EDGE_WIDTH (e,idx)*_lambda_to_scale;
  }
}


static int getlength (edge_t *e)
{
  return e->l*_lambda_to_scale;
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

  int e_w = getwidth (0, e);

  /* XXX: THIS CODE IS COPIED FROM emit_rectangle!!!!! */

  d = L->getDiff (e->type, e->flavor);
  f = L->getFet (e->type, e->flavor);
  p = L->getPoly ();

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
      rect = f->getSpacing (e_w);
      if (left->contact) {
	rect = MAX (rect, d->viaSpaceMid());
      }
    }
    else if (prev_w < e_w) {
      /* upward notch */
      fet_type = 1;
      rect = d->getNotchSpacing();
      if (left->contact &&
	  (rect + d->effOverhang (e_w) < d->viaSpaceMid())) {
	rect = d->viaSpaceMid() - d->effOverhang (e_w);
      }
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
      if (left->contact &&
	  (rect + d->effOverhang (e_w) < d->viaSpaceMid())) {
	rect = d->viaSpaceMid() - d->effOverhang (e_w);
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
static int emit_rectangle (Layout *L,
			   int pad,
			   int dx, int dy,
			   unsigned int flags,
			   edge_t *prev, int previdx,
			   node_t *left, edge_t *e, int eidx, int yup,
			   BBox *ret)
{
  DiffMat *d;
  FetMat *f;
  PolyMat *p;
  int rect;
  int fet_type; /* -1 = downward notch, +1 = upward notch, 0 = same
		   width */

  BBox b;

  int e_w = getwidth (eidx, e);
  
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
      rect = f->getSpacing (e_w);
      if (left->contact) {
	rect = MAX (rect, d->viaSpaceMid());
      }
    }
    else if (prev_w < e_w) {
      /* upward notch */
      fet_type = 1;
      rect = d->getNotchSpacing();
      if (left->contact &&
	  (rect + d->effOverhang (e_w) < d->viaSpaceMid())) {
	rect = d->viaSpaceMid() - d->effOverhang (e_w);
      }
    }
    else {
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
      if (left->contact &&
	  (rect + d->effOverhang (e_w) < d->viaSpaceMid())) {
	rect = d->viaSpaceMid() - d->effOverhang (e_w);
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
    L->DrawFet (e->flavor, e->type, dx, dy + yup*e_w, getlength (e),
		-yup*e_w, NULL);
  }
  else {
    L->DrawFet (e->flavor, e->type, dx, dy, getlength (e), yup*e_w, NULL);
  }

  int poverhang = p->getOverhang (getlength (e));
  int uoverhang = poverhang;

  if (fet_type != 0) {
    uoverhang = MAX (uoverhang, p->getNotchOverhang (getlength (e)));
  }

  /* now print poly edges */
  if (yup < 0) {
    L->DrawPoly (dx, dy, getlength (e), -yup*poverhang, e->g);
    L->DrawPoly (dx, dy + yup*(e_w+uoverhang), getlength(e), -yup*uoverhang, NULL);
  }
  else {
    L->DrawPoly (dx, dy - yup*poverhang, getlength (e), yup*poverhang, e->g);
    L->DrawPoly (dx, dy + yup*e_w, getlength (e), yup*uoverhang, NULL);
  }
  

  dx += getlength (e);
  
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

static BBox print_dualstack (Layout *L, struct gate_pairs *gp)
{
  int flavor;
  int xpos, xpos_p;
  int diffspace;
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

  diffspace = ndiff->getOppDiffSpacing (flavor);
  Assert (diffspace == pdiff->getOppDiffSpacing (flavor), "Hmm?!");

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
    fposn = locate_fetedge (L, xpos, EDGE_FLAGS_LEFT|EDGE_FLAGS_RIGHT,
			    NULL, 0, gp->l.n, gp->u.e.n, gp->n_start);
    fposp = locate_fetedge (L, xpos, EDGE_FLAGS_LEFT|EDGE_FLAGS_RIGHT,
			    NULL, 0, gp->l.p, gp->u.e.p, gp->p_start);
    
    if (fposn > fposp) {
      padp = fposn - fposp;
      padn = 0;
    }
    else {
      padn = fposp - fposn;
      padp = 0;
    }

    xpos = emit_rectangle (L, padn, xpos, yn,
			   EDGE_FLAGS_LEFT|EDGE_FLAGS_RIGHT,
			   NULL, 0,
			   gp->l.n, gp->u.e.n, gp->n_start, -1, &b);
    
    xpos_p = emit_rectangle (L, padp, xpos_p, yp,
			     EDGE_FLAGS_LEFT|EDGE_FLAGS_RIGHT,
			     NULL, 0,
			     gp->l.p, gp->u.e.p, gp->p_start, 1, &b);
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
      
      if (firstp && tmp->u.e.p) {
	flagsp |= EDGE_FLAGS_LEFT;
	firstp = 0;
      }
      if (firstn && tmp->u.e.n) {
	firstn = 0;
	flagsn |= EDGE_FLAGS_LEFT;
      }
      if (!list_next (li)) {
	flagsp |= EDGE_FLAGS_RIGHT;
	flagsn |= EDGE_FLAGS_RIGHT;
      }
      else {
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
				tmp->n_start);
	fposp = locate_fetedge (L, xpos_p, flagsp,
				prevp, prevpidx, leftp, tmp->u.e.p,
				tmp->p_start);
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
			       tmp->n_start, -1, &b);
	prevn = tmp->u.e.n;
	prevnidx = tmp->n_start;
	if (!tmp->u.e.p) {
	  xpos_p = xpos;
	}
      }
      
      if (tmp->u.e.p) {
	xpos_p = emit_rectangle (L, padp, xpos_p, yp, flagsp,
				 prevp, prevpidx, leftp, tmp->u.e.p,
				 tmp->p_start, 1, &b);

	prevp = tmp->u.e.p;
	prevpidx = tmp->p_start;
	if (!tmp->u.e.n) {
	  xpos = xpos_p;
	}
      }
      
    }
  }
  return b;
}


static BBox print_singlestack (Layout *L, list_t *l)
{
  int flavor;
  int type;
  node_t *n;
  edge_t *e;
  edge_t *prev;
  int xpos = 0;
  int ypos = 0;
  BBox b;
  int idx = 0;
  int previdx = 0;

  b.p.llx = 0;
  b.p.lly = 0;
  b.p.urx = 0;
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
			   n, e, idx, 1, &b);
    prev = e;
    previdx = idx;

    li = list_next (list_next (list_next (li)));
  }
  Assert (li && !list_next (li), "Eh?");
  n = (node_t *) list_value (li);
  return b;
}




void ActStackLayoutPass::_createlocallayout (Process *p)
{
  list_t *stks;
  BBox b;

  Assert (stk, "What?");

  stks = stk->getStacks (p);
  if (!stks || list_length (stks) == 0) {
    (*layoutmap)[p] = NULL;
    return;
  }

  listitem_t *li;

  li = list_first (stks);
  list_t *stklist = (list_t *) list_value (li);

  _lambda_to_scale = lambda_to_scale;

  LayoutBlob *BLOB = new LayoutBlob (BLOB_HORIZ);

  if (list_length (stklist) > 0) {
    /* dual stacks */
    listitem_t *si;

    for (si = list_first (stklist); si; si = list_next (si)) {
      struct gate_pairs *gp;
      Layout *l = new Layout(stk->getNL (p));
      gp = (struct gate_pairs *) list_value (si);

      /*--- process gp ---*/
      b = print_dualstack (l, gp);

      l->DrawDiffBBox (b.flavor, EDGE_PFET,
		       b.p.llx, b.p.lly, b.p.urx-b.p.llx, b.p.ury-b.p.lly);
      l->DrawDiffBBox (b.flavor, EDGE_NFET,
		       b.n.llx, b.n.lly, b.n.urx-b.n.llx, b.n.ury-b.n.lly);

      BLOB->appendBlob (new LayoutBlob (BLOB_BASE, l));
    }
  }

  li = list_next (li);
  stklist = (list_t *) list_value (li);

  if (stklist && (list_length (stklist) > 0)) {
    /* n stacks */
    listitem_t *si;

    for (si = list_first (stklist); si; si = list_next (si)) {
      list_t *sl = (list_t *) list_value (si);
      Layout *l = new Layout (stk->getNL (p));

      b = print_singlestack (l, sl);
      
      l->DrawDiffBBox (b.flavor, EDGE_NFET, b.n.llx, b.n.lly,
		       b.n.urx - b.n.llx, b.n.ury - b.n.lly);

      BLOB->appendBlob (new LayoutBlob (BLOB_BASE, l)); 
    }
  }

  li = list_next (li);
  stklist = (list_t *) list_value (li);
  if (stklist && (list_length (stklist) > 0)) {
    /* p stacks */
    listitem_t *si;

    for (si = list_first (stklist); si; si = list_next (si)) {
      list_t *sl = (list_t *) list_value (si);
      Layout *l = new Layout (stk->getNL (p));

      b = print_singlestack (l, sl);
      
      l->DrawDiffBBox (b.flavor, EDGE_PFET, b.n.llx, b.n.lly,
		       b.n.urx - b.n.llx, b.n.ury - b.n.lly);

      BLOB->appendBlob (new LayoutBlob (BLOB_BASE, l)); 
    }
  }

  /* --- add pins --- */
  netlist_t *n = stk->getNL (p);

  long bllx, blly, burx, bury;
  BLOB->getBBox (&bllx, &blly, &burx, &bury);

  if (n && (bllx <= burx && blly <= bury)) {
    /* we have a netlist + layout */
    int p_in = 0;
    int p_out = 0;
    int s_in = 1;
    int s_out = 1;

    RoutingMat *m1 = Technology::T->metal[0];
    RoutingMat *m2 = Technology::T->metal[1];

    int redge = (burx - bllx + 1);
    int tedge = (bury - blly + 1);

    redge = snap_to (redge, m2->getPitch());
    tedge = snap_to (tedge, m1->getPitch());

    /* move the top edge if there isn't enough space for two
       rows of pins 
    */
    while (blly + tedge - m2->minWidth() <=
	blly + m1->getPitch() + m2->minWidth()) {
      tedge += m1->getPitch();
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
      
      b = ihash_lookup (n->bN->cH, (long)n->bN->used_globals[i]);
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

    if ((p_in * m2->getPitch() > redge) ||(p_out * m2->getPitch() > redge)) {
      warning ("Can't fit ports!");
    }
    
    if (p_in > 0) {
      while ((m2->getPitch() + p_in * s_in * m2->getPitch()) <= redge) {
	s_in++;
      }
      s_in--;
      if (s_in == 0) { s_in = 1; }
    }

    if (p_out > 0) {
      while ((m2->getPitch() + p_out * s_out * m2->getPitch()) <= redge) {
	s_out++;
      }
      s_out--;
      if (s_out == 0) { s_out = 1; }
    }

    p_in = m2->getPitch();
    p_out = m2->getPitch();

    char tmp[1024];

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
	int w = m2->minWidth ();
	pins->DrawMetalPin (1, bllx + p_in, blly + tedge - w, w, w, av->n, 0);
	p_in += m2->getPitch()*s_in;
      }
      else {
	int w = m2->minWidth ();
	pins->DrawMetalPin (1, bllx + p_out, blly + m1->getPitch(), w, w, av->n, 1);
	p_out += m2->getPitch()*s_out;
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
      b = ihash_lookup (n->bN->cH, (long)n->bN->used_globals[i]);
      
      Assert (b, "Hmm:");

      p_in++;
      
      act_booleanized_var_t *bv = (act_booleanized_var_t *)b->v;
      struct act_nl_varinfo *av = (struct act_nl_varinfo *)bv->extra;
      Assert (av, "Hmm");

      int w = m2->minWidth ();
      pins->DrawMetalPin (1, bllx + p_in, blly + tedge - w, w, w, av->n, 0);
      p_in += m2->getPitch()*s_in;

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
      int w = m2->minWidth ();
      pins->DrawMetalPin (1, bllx + p_in, blly + tedge - w, w, w, n->Vdd, 0);
      p_in += m2->getPitch()*s_in;
      
    }
    if (!found_vdd && n->GND && n->GND->e && list_length (n->GND->e) > 0) {
      p_in++;
      found_gnd = 1;
      int w = m2->minWidth ();
      pins->DrawMetalPin (1, bllx + p_in, blly + tedge - w, w, w, n->GND, 0);
      p_in += m2->getPitch()*s_in;
    }

    /*--- XXX: but this is not the end of the pins... ---*/

    
    LayoutBlob *bl = new LayoutBlob (BLOB_MERGE);
    bl->appendBlob (BLOB);
    bl->appendBlob (new LayoutBlob (BLOB_BASE, pins));
    BLOB = bl;
  }

  (*layoutmap)[p] = BLOB;
}


void ActStackLayoutPass::_createlayout (Process *p)
{
  if (layoutmap->find (p) != layoutmap->end()) {
    return;
  }

  ActInstiter i(p->CurScope ());
  for (i = i.begin(); i != i.end(); i++) {
    ValueIdx *vx = (*i);
    if (TypeFactory::isProcessType (vx->t)) {
      Process *x = dynamic_cast<Process *> (vx->t->BaseType());
      if (x->isExpanded()) {
	_createlayout (x);
      }
    }
  }
  _createlocallayout (p);
}


int ActStackLayoutPass::run (Process *p)
{
  init ();

  if (!rundeps (p)) {
    return 0;
  }

  if (!p) {
    ActNamespace *g = ActNamespace::Global();
    ActInstiter i(g->CurScope());

    for (i = i.begin(); i != i.end(); i++) {
      ValueIdx *vx = (*i);
      if (TypeFactory::isProcessType (vx->t)) {
	Process *x = dynamic_cast<Process *>(vx->t->BaseType());
	if (x->isExpanded()) {
	  _createlayout (x);
	}
      }
    }
  }
  else {
    _createlayout (p);
  }

  _finished = 2;
  return 1;
}

int ActStackLayoutPass::init ()
{
  double net_lambda;
  cleanup();

  layoutmap = new std::map<Process *, LayoutBlob *>();

  net_lambda = config_get_real ("net.lambda");
  lambda_to_scale = (int)(net_lambda*1e9/Technology::T->scale + 0.5);

  if (fabs(lambda_to_scale*Technology::T->scale - net_lambda*1e9) > 0.001) {
    warning ("Lambda (%g) and technology scale factor (%g) are not integer multiples; rounding down", net_lambda, Technology::T->scale);
  }

  _finished = 1;
  return 1;
}

int ActStackLayoutPass::emitRect (FILE *fp, Process *p)
{
  if (!completed ()) {
    return 0;
  }
  if (!p) {
    return 0;
  }

  LayoutBlob *blob = (*layoutmap)[p];
  if (!blob) {
    return 0;
  }

  long bllx, blly, burx, bury;
  blob->calcBoundary (&bllx, &blly, &burx, &bury);

  if (bllx > burx || blly > bury) {
    /* no layout */
    return 0;
  }

  TransformMat mat;

  mat.applyTranslate (-bllx, -blly);

  blob->PrintRect (fp, &mat);

  return 1;
}

int ActStackLayoutPass::haveRect (Process *p)
{
  if (!completed ()) {
    return 0;
  }
  if (!p) {
    return 0;
  }

  LayoutBlob *blob = (*layoutmap)[p];
  if (!blob) {
    return 0;
  }
  else {
    return 1;
  }
}

int ActStackLayoutPass::emitLEF (FILE *fp, FILE *fpcell, Process *p, int dorect)
{
  if (!completed ()) {
    return 0;
  }

  visited = new std::unordered_set<Process *>();

  int ret = _emitLEF (fp, fpcell, p, dorect);

  
  delete visited;

  return ret;
}

static void emit_one_pin (Act *a, FILE *fp, const char *name, int isinput,
			  const char *sigtype, LayoutBlob *blob,
			  node_t *signode)
{
  long bllx, blly, burx, bury;
  double scale = Technology::T->scale/1000.0;

  blob->calcBoundary (&bllx, &blly, &burx, &bury);
  
  fprintf (fp, "    PIN ");
  a->mfprintf (fp, "%s\n", name);

  fprintf (fp, "        DIRECTION %s ;\n", isinput ? "INPUT" : "OUTPUT");
  fprintf (fp, "        USE %s ;\n", sigtype);

  fprintf (fp, "        PORT\n");

  /* -- find all pins of this name! -- */
  TransformMat mat;
  mat.applyTranslate (-bllx, -blly);
  list_t *tiles = blob->search (signode, &mat);
  listitem_t *tli;
  for (tli = list_first (tiles); tli; tli = list_next (tli)) {
    struct tile_listentry *tle = (struct tile_listentry *) list_value (tli);
    listitem_t *xi;
    for (xi = list_first (tle->tiles); xi; xi = list_next (xi)) {
      Layer *lname = (Layer *) list_value (xi);
      xi = list_next (xi);
      Assert (xi, "Hmm");
	
      if (!lname->isMetal()) continue;
      
      list_t *actual_tiles = (list_t *) list_value (xi);
      listitem_t *ti;
      for (ti = list_first (actual_tiles); ti; ti = list_next (ti)) {
	long tllx, tlly, turx, tury;
	Tile *tmp = (Tile *) list_value (ti);
	
	/* only use pin tiles */
	if (!TILE_ATTR_ISPIN(tmp->getAttr())) continue;
	  
	  
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
	
	fprintf (fp, "        LAYER %s ;\n", lname->getRouteName());
	fprintf (fp, "        RECT %.6f %.6f %.6f %.6f ;\n",
		 scale*tllx, scale*tlly, scale*(1+turx), scale*(1+tury));
      }
    }
  }
  fprintf (fp, "        END\n");
  fprintf (fp, "    END ");
  a->mfprintf (fp, "%s", name);
  fprintf (fp, "\n");
}

int ActStackLayoutPass::_emitLEF (FILE *fp, FILE *fpcell, Process *p, int dorect)
{
  Scope *sc;
  int ret;

  /* now: see if process has been emitted already */
  visited->insert (p);
  
  if (!p) {
    ActNamespace *g = ActNamespace::Global();
    sc = g->CurScope();
  }
  else {
    sc = p->CurScope();
  }
  ActInstiter i(sc);

  ret = 0;

  /* emit sub-circuits */
  for (i = i.begin(); i != i.end(); i++) {
    ValueIdx *vx = (*i);
    if (TypeFactory::isProcessType (vx->t)) {
      Process *x = dynamic_cast<Process *>(vx->t->BaseType());
      if (x->isExpanded()) {
	if (visited->find (x) == visited->end()) {
	  if (_emitLEF (fp, fpcell, x, dorect)) {
	    ret = 1;
	  }
	}
      }
    }
  }

  /* emit self */
  
  int padx = 0;
  int pady = 0;
  netlist_t *n;

  LayoutBlob *blob = (*layoutmap)[p];
  if (!blob) {
    return ret;
  }

  n = stk->getNL (p);
  if (!n) {
    return ret;
  }

  if (n->bN->isempty) {
    /* blackbox */
    FILE *bfp;
    char name[10240];

    a->msnprintfproc (name, 10240, p);
    char *tpath = lef_path_open (name);
    if (!tpath) {
      fatal_error ("Could not find LEF file for black box module `%s'", name);
    }

    bfp = fopen (tpath, "r");
    if (!bfp) {
      fatal_error ("Could not open file `%s'", tpath);
    }
    FREE (tpath);
    while (!feof (bfp)) {
      long sz;
      sz = fread (name, 1, 10240, bfp);
      if (sz > 0) {
	fwrite (name, 1, sz, fp);
      }
    }
    fprintf (fp, "\n");
    fclose (bfp);
    return 1;
  }

  RoutingMat *m1 = Technology::T->metal[0];
  RoutingMat *m2 = Technology::T->metal[1];
  long bllx, blly, burx, bury;
  blob->calcBoundary (&bllx, &blly, &burx, &bury);

  if (bllx > burx || blly > bury) {
    /* no layout */
    return ret;
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

  for (int lef=0; lef < 2; lef++) {

  if (lef != 0) { fprintf (fp, "\n"); }

  fprintf (fp, "MACRO ");
  a->mfprintfproc (fp, p);
  if (lef == 1) {
    fprintf (fp, "_plug");
  }
  
  fprintf (fp, "\n");

  fprintf (fp, "    CLASS CORE ;\n");
  fprintf (fp, "    FOREIGN ");
  a->mfprintfproc (fp, p);
  if (lef == 1) {
    fprintf (fp, "_plug");
  }

  fprintf (fp, " %.6f %.6f ;\n", 0.0, 0.0);
  fprintf (fp, "    ORIGIN %.6f %.6f ;\n", 0.0, 0.0);

  double scale = Technology::T->scale/1000.0;

  fprintf (fp, "    SIZE %.6f BY %.6f ;\n", (burx - bllx + (lef == 0 ? 0 : 4*m2->getPitch()))*scale, (bury - blly )*scale);
  fprintf (fp, "    SYMMETRY X Y ;\n");
  fprintf (fp, "    SITE CoreSite ;\n");

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
  }

  /* add globals as input pins */
  for (int i=0; i < A_LEN (n->bN->used_globals); i++) {
    /* generate name */
    char tmp[1024];
    ActId *id = n->bN->used_globals[i]->toid();
    id->sPrint (tmp, 1024);
    delete id;

    /* and signal type + node pointer */
    const char *sigtype;
    sigtype = "SIGNAL";
    ihash_bucket_t *b;
    b = ihash_lookup (n->bN->cH, (long)n->bN->used_globals[i]);
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
  }

  /* check Vdd/GND */
  if (!found_vdd && n->Vdd) {
    found_vdd = 1;
    if (n->Vdd->e && list_length (n->Vdd->e) > 0) {
      emit_one_pin (a, fp, config_get_string ("net.global_vdd"),
		    1, "POWER", blob, n->Vdd);

    }
  }

  if (!found_gnd && n->GND) {
    found_gnd = 1;
    if (n->GND->e && list_length (n->GND->e) > 0) {
      emit_one_pin (a, fp, config_get_string ("net.global_gnd"),
		    1, "GROUND", blob, n->GND);

    }
  }
  

  /* XXX: add obstructions for metal layers; in reality we need to
     add the routed metal and then grab that here */
  long rllx, rlly, rurx, rury;
  blob->getBBox (&rllx, &rlly, &rurx, &rury);
  if (((rury - rlly) > 6*m1->getPitch()) &&
      ((rurx - rllx) > 2*m2->getPitch())) {
    fprintf (fp, "    OBS\n");
    fprintf (fp, "      LAYER %s ;\n", m1->getName());
    fprintf (fp, "         RECT %.6f %.6f %.6f %.6f ;\n",
	     scale*((rllx - bllx) + m2->getPitch()),
	     scale*((rlly - blly) + 3*m1->getPitch()),
	     scale*((rurx - bllx) - m2->getPitch()),
	     scale*((rury - blly) - 3*m1->getPitch()));
    fprintf (fp, "    END\n");
  }
  fprintf (fp, "END ");
  a->mfprintfproc (fp, p);
  if (lef == 1) {
    fprintf (fp, "_plug");
  }
  fprintf (fp, "\n");

  }

  fprintf (fp, "\n");
  

  if (fpcell) {
    emitWellLEF (fpcell, p);
  }

  if (dorect) {
    FILE *tfp;
    char cname[10240];
    int len;

    a->msnprintfproc (cname, 10240, p);
    len = strlen (cname);
    snprintf (cname + len, 10240-len, ".rect");
    tfp = fopen (cname, "w");
    emitRect (tfp, p);
    fclose (tfp);
  }

  return 1;
}

int ActStackLayoutPass::emitWellLEF (FILE *fp, Process *p)
{
  int padx = 0;
  int pady = 0;
  netlist_t *n;
  if (!completed ()) {
    return 0;
  }

  if (!p) {
    return 0;
  }

  LayoutBlob *blob = (*layoutmap)[p];
  if (!blob) {
    return 0;
  }

  n = stk->getNL (p);
  if (!n) {
    return 0;
  }

  long bllx, blly, burx, bury;
  blob->calcBoundary (&bllx, &blly, &burx, &bury);

  if (bllx > burx || blly > bury) {
    /* no layout */
    return 0;
  }

  RoutingMat *m1 = Technology::T->metal[0];
  RoutingMat *m2 = Technology::T->metal[1];
  double scale = Technology::T->scale/1000.0;

  fprintf (fp, "MACRO ");
  a->mfprintfproc (fp, p);
  fprintf (fp, "\n");

  for (int lef=0; lef < 2; lef++) {

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

  TransformMat mat;
  mat.applyTranslate (-bllx, -blly);

  for (int i=0; i < Technology::T->num_devs; i++) {
    for (int j=0; j < 2; j++) {

      WellMat *w = Technology::T->well[j][i];
      list_t *tiles = blob->search (TILE_FLGS_TO_ATTR(i,j,DIFF_OFFSET), &mat);
      long wllx, wlly, wurx, wury;
      int init = 0;

      listitem_t *tli;
      for (tli = list_first (tiles); tli; tli = list_next (tli)) {
	struct tile_listentry *tle = (struct tile_listentry *) list_value (tli);

	/* a transform matrix + list of (layer,tile-list) pairs */
	
	listitem_t *xi;
	for (xi = list_first (tle->tiles); xi; xi = list_next (xi)) {
	  Layer *name = (Layer *) list_value (xi);
	  xi = list_next (xi);
	  Assert (xi, "What?");

	  list_t *actual_tiles = (list_t *) list_value (xi);
	  listitem_t *ti;

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
	    if (!init) {
	      wllx = tllx;
	      wlly = tlly;
	      wurx = turx;
	      wury = tury;
	      init = 1;
	    }
	    else {
	      wllx = MIN(wllx, tllx);
	      wlly = MIN(wlly, tlly);
	      wurx = MAX(wurx, turx);
	      wury = MAX(wury, tury);
	    }
	  }
	}
      }

      if (init && w) {
	wurx ++;
	wury ++;
	
	//wllx -= w->getOverhang();
	wlly -= w->getOverhang();
	//wurx += w->getOverhang();
	wury += w->getOverhang();

	wllx = 0; // XXX: FIXME MAX(wllx, 0);
	wlly = MAX(wlly, 0);

	// XXX: FIXME wurx = MIN(wurx, burx - bllx);
	wurx = burx - bllx + (lef == 0 ? 0 : 4*m2->getPitch());
	
	wury = MIN(wury, bury - blly);
	
	fprintf (fp, "        LAYER %s ;\n", w->getName());
	fprintf (fp, "        RECT %.6f %.6f %.6f %.6f ;\n",
		 scale*wllx, scale*wlly, scale*wurx, scale*wury);
	fprintf (fp, "        END\n");
      }
      list_free (tiles);
    }
  }
  fprintf (fp, "    END VERSION\n");

  }

  fprintf (fp, "END ");
  a->mfprintfproc (fp, p);
  fprintf (fp, "\n\n");

  return 1;
}


void ActStackLayoutPass::emitLEFHeader (FILE *fp)
{
  double scale = Technology::T->scale/1000.0;
  
  /* -- lef header -- */
  fprintf (fp, "VERSION 5.6 ;\n\n");
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
  fprintf (fp, "    SIZE %.6f BY %.6f ;\n",
	   Technology::T->metal[1]->getPitch()*scale, // m2
	   Technology::T->metal[0]->getPitch()*scale  // m1
	   );
  fprintf (fp, "END CoreSite\n\n");
  
  int i;
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

void ActStackLayoutPass::emitWellHeader (FILE *fp)
{
  double scale = Technology::T->scale/1000.0;
  
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
	fprintf (fp, "END %s\n\n", w->getName());
      }
    }
  }
}


LayoutBlob *ActStackLayoutPass::getLayout (Process *p)
{
  if (!completed ()) {
    return 0;
  }
  if (!p) return NULL;
  return (*layoutmap)[p];
}


/*
  Returns the max height of all layout blocks within p that have not
  been visited yet 
*/
int ActStackLayoutPass::_maxHeight (Process *p)
{
  int maxval = 0;
  LayoutBlob *b;

  visited->insert (p);

  b = (*layoutmap)[p];
  if (b) {
    long llx, lly, urx, ury;
    b->calcBoundary (&llx, &lly, &urx, &ury);
    maxval = (ury - lly + 1);
  }
  
  ActInstiter i(p->CurScope ());
  for (i = i.begin(); i != i.end(); i++) {
    ValueIdx *vx = (*i);
    if (TypeFactory::isProcessType (vx->t)) {
      Process *x = dynamic_cast<Process *> (vx->t->BaseType());
      if (x->isExpanded()) {
	if (visited->find (x) == visited->end()) {
	  int tmp = _maxHeight (x);
	  if (tmp > maxval) {
	    maxval = tmp;
	  }
	}
      }
    }
  }
  return maxval;
}


int ActStackLayoutPass::maxHeight (Process *p)
{
  int maxval = 0;

  if (!completed()) {
    return 0;
  }

  visited = new std::unordered_set<Process *>();
  
  if (!p) {
    ActNamespace *g = ActNamespace::Global();
    ActInstiter i(g->CurScope());

    for (i = i.begin(); i != i.end(); i++) {
      ValueIdx *vx = (*i);
      if (TypeFactory::isProcessType (vx->t)) {
	Process *x = dynamic_cast<Process *>(vx->t->BaseType());
	if (x->isExpanded()) {
	  if (visited->find (x) == visited->end()) {
	    int tmp = _maxHeight (x);
	    if (tmp > maxval) {
	      maxval = tmp;
	    }
	  }
	}
      }
    }
  }
  else {
    maxval = _maxHeight (p);
  }
  delete visited;
  return maxval;
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

static void count_inst (void *x, ActId *prefix, Process *p)
{
  ActStackLayoutPass *ap = (ActStackLayoutPass *)x;
  LayoutBlob *b;
  
  if ((b = ap->getLayout (p))) {
    /* there is a circuit */
    long llx, lly, urx, ury;

    b->calcBoundary (&llx, &lly, &urx, &ury);
    if ((llx > urx) || (lly > ury)) return;
    
    b->incCount();
    _instcount++;
    
    _areacount += (urx - llx + 1)*(ury - lly + 1);
    _areastdcell += (urx - llx + 1)*_maximum_height;
  }
}

/*
 * Flat instance dump
 */
static Act *global_act;
static ActStackLayoutPass *_alp;

static void dump_inst (void *x, ActId *prefix, Process *p)
{
  FILE *fp = (FILE *)x;
  char buf[10240];
  LayoutBlob *b;
  

  if ((b = _alp->getLayout (p))) {
    long llx, lly, urx, ury;
    
    b->calcBoundary (&llx, &lly, &urx, &ury);
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
  Assert (net, "Why are you calling this function?");
  if (net->skip) return 0;
  if (net->port && (!toplevel || !pins)) return 0;

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

  if (net->port) {
    fprintf (fp, " ( PIN top_iopin%d )", toplevel-1);
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
	_collect_emit_nets (a, cpy, instproc, fp, do_pins);
	delete x;
	newid->setArray (NULL);
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


void ActStackLayoutPass::emitDEFHeader (FILE *fp, Process *p)
{
  /* -- def header -- */
  fprintf (fp, "VERSION 5.6 ;\n\n");
  fprintf (fp, "BUSBITCHARS \"[]\" ;\n\n");
  fprintf (fp, "DIVIDERCHAR \"/\" ;\n\n");
  fprintf (fp, "DESIGN ");

  a->mfprintfproc (fp, p);
  fprintf (fp, " ;\n");
  
  fprintf (fp, "\nUNITS DISTANCE MICRONS %d ;\n\n", MICRON_CONVERSION);

  _maxht = maxHeight (p);
}

void ActStackLayoutPass::emitDEF (FILE *fp, Process *p, double pad,
				  int do_pins)
{
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
  _maximum_height = _maxht;
  ap->setCookie (this);
  ap->setInstFn (count_inst);
  ap->run (p);
  count_inst (this, NULL, p); // oops!

  _total_instances = _instcount;
  _total_area = _areacount;
  _total_stdcell_area = _areastdcell;

  _total_area *= pad;
  _total_stdcell_area *= pad;

  double side = sqrt (_total_area);

  double unit_conv = Technology::T->scale*MICRON_CONVERSION/1000.0;

  side *= unit_conv;

  int pitchx, pitchy, track_gap;
  int nx, ny;

  pitchx = Technology::T->metal[1]->getPitch();
  pitchy = Technology::T->metal[0]->getPitch();

  pitchx *= unit_conv;
  pitchy *= unit_conv;

#define TRACK_HEIGHT 18

  track_gap = pitchy * TRACK_HEIGHT;

  nx = (side + pitchx - 1)/pitchx;
  ny = (side + track_gap - 1)/track_gap;

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
  fprintf (fp, "COMPONENTS %d ;\n", _total_instances);
  ap->setCookie (fp);
  ap->setInstFn (dump_inst);
  global_act = a;
  _alp = this;
  ap->run (p);
  global_act = NULL;
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

    for (int i=0; i < A_LEN (act_bnl->ports); i++) {
      if (act_bnl->ports[i].omit) continue;
      num_pins++;
    }
    fprintf (fp, "PINS %d ;\n", num_pins);
    num_pins = 0;
    for (int i=0; i < A_LEN (act_bnl->ports); i++) {
      if (act_bnl->ports[i].omit) continue;
      Assert (act_bnl->ports[i].netid != -1, "What?");
      fprintf (fp, "- top_iopin%d + NET ", act_bnl->ports[i].netid);
      ActId *tmp = act_bnl->nets[act_bnl->ports[i].netid].net->toid();
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
}




/*
 * Layout generation statistics
 *
 */
void ActStackLayoutPass::reportStats (Process *p)
{
  if (!completed ()) {
    return;
  }

  visited = new std::unordered_set<Process *>();

  _reportStats (p);

  
  delete visited;

  return;
}


void ActStackLayoutPass::_reportStats(Process *p)
{
  Scope *sc;

  /* now: see if process has been emitted already */
  visited->insert (p);
  
  if (!p) {
    ActNamespace *g = ActNamespace::Global();
    sc = g->CurScope();
  }
  else {
    sc = p->CurScope();
  }
  ActInstiter i(sc);

  /* emit sub-circuits */
  for (i = i.begin(); i != i.end(); i++) {
    ValueIdx *vx = (*i);
    if (TypeFactory::isProcessType (vx->t)) {
      Process *x = dynamic_cast<Process *>(vx->t->BaseType());
      if (x->isExpanded()) {
	if (visited->find (x) == visited->end()) {
	  _reportStats (x);
	}
      }
    }
  }

  LayoutBlob *blob = (*layoutmap)[p];
  if (!blob) {
    return;
  }
  long bllx, blly, burx, bury;
  blob->calcBoundary (&bllx, &blly, &burx, &bury);
  if (bllx > burx || blly > bury) return;

  char *tmp = p->getns()->Name();
  printf ("--- Cell %s::%s ---\n", tmp, p->getName());
  FREE (tmp);

  unsigned long area = (burx-bllx+1)*(bury-blly+1);
  printf ("  count=%lu; ", blob->getCount());
  printf ("cell_area=%.3g um^2; ", area*Technology::T->scale/1000.0*
	  Technology::T->scale/1000.0);
  printf ("area: %.2f%%\n", (area*blob->getCount()*100.0/getArea()));

  netlist_t *nl = stk->getNL (p);
  node_t *n;
  unsigned long ncount = 0;
  unsigned long ecount = 0;
  unsigned long keeper = 0;
  for (n = nl->hd; n; n = n->next) {
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
  
