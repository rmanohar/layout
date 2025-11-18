/*************************************************************************
 *
 *  Copyright (c) 2019-2021 Rajit Manohar
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
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
#ifndef __ACT_STK_LAYOUT_PASS_H__
#define __ACT_STK_LAYOUT_PASS_H__

#include <act/act.h>
#include <map>
#include <unordered_set>
#include "geom.h"
#include <common/path.h>

/*-- data structures --*/

class ActStackLayout {
public:
  ActStackLayout (ActPass *a);

  LayoutBlob *getLayout (Process *p = NULL);

  long snap_up_x (long);
  long snap_up_y (long);
  long snap_dn_x (long);
  long snap_dn_y (long);

  void *localop (ActPass *ap, Process *p, int mode);

  void run_post (void);
  void runrec (int mode, UserDef *u);

  void setBBox (Process *p, long llx, long lly, long urx, long ury);
  int getBBox (Process *p, long *llx, long *lly, long *urx, long *ury);
  void incBBox (Process *p);
  long getBBoxCount (Process *p);


  int getImport () { return _rect_import; }
  void reportDirs (FILE *fp);
  void cacheConfig ();

 private:
  int _localdiffspace (Process *p);

  LayoutBlob *_readlocalRect (Process *p);

  /* mode 0 */
  LayoutBlob *_createlocallayout (Process *p);

  /* mode 1 */
  int _lef_header;
  int _cell_header;
  int _emitlocalLEF (Process *p);
  void _emitLocalWellLEF (FILE *fp, Process *p);

  void _computeWell (LayoutBlob *blob, int flavor, int type,
		     long *llx, long *lly, long *urx, long *ury,
		     int is_welltap = 0);

  void emitLEFHeader (FILE *fp);
  void emitWellHeader (FILE *fp);

  /* mode 2 */
  void _reportLocalStats(Process *p);

  /* mode 3 */
  void _maxHeightlocal (Process *p);

  /* mode 4 */
  void _emitlocalRect (Process *p);

  /* this is mode 5 */
  void emitDEFHeader (FILE *fp, Process *p);
  void emitDEF (FILE *fp, Process *p, double pad = 1.4, double ratio = 1.0, int do_pins = 1);
  
  /* welltap */
  LayoutBlob **wellplugs;
  netlist_t *dummy_netlist;	// dummy netlist

  LayoutBlob *_createwelltap (int flavor);
  LayoutBlob *_readwelltap (int flavor);
  void _emitwelltaprect (int flavor);

  /* layoutblob list following the shared staticizer type list */
  list_t *_weak_supplies;
  LayoutBlob *_create_weaksupply (ActNetlistPass::shared_stat *s);


  /* compute aligned LEF boundary */
  LayoutBlob *computeLEFBoundary (LayoutBlob *b);

  ActDynamicPass *stk;
  ActNetlistPass *nl;

  /*-- bounding box for black boxes --*/
  struct pHashtable *boxH;

  int isEmpty (list_t *stk);

  Act *a;
  ActPass *me;
  
  int lambda_to_scale;

  /* temporary variables */
  int _total_instances;
  double _total_area;
  double _total_stdcell_area;
  int _maxht;
  int _ymax, _ymin; // aux vars 

  /* arguments */
  FILE *_fp, *_fpcell;
  int _do_rect;

  const char *_version;
  unsigned int _micron_conv;
  double _manufacturing_grid;
  RoutingMat *_m_align_x;
  RoutingMat *_m_align_y;
  int _horiz_metal;
  int _pin_layer;
  RoutingMat *_pin_metal;
  int _rect_wells;

  /* -- .rect file management -- */
  int _rect_import;  /* set to 1 if .rects can be read in from files,
			0 otherwise */
  path_info_t *_rect_inpath;	// input path for rectangles, if any
  const char *_rect_outdir;	// rect output directory, if any
  const char *_rect_outinitdir; // rect output directory for initial
				// unwired layout

  int _extra_tracks_top;
  int _extra_tracks_bot;
  int _extra_tracks_left;
  int _extra_tracks_right;

  std::unordered_set<Process *> *visited;
};

extern "C" {

  void layout_init (ActPass *ap);
  void layout_run (ActPass *ap, Process *p);
  void layout_recursive (ActPass *ap, UserDef *u, int mode);
  void *layout_proc (ActPass *ap, Process *p, int mode);
  void *layout_data (ActPass *ap, Data *d, int mode);
  void *layout_chan (ActPass *ap, Channel *c, int mode);
  void layout_free (ActPass *ap, void *v);
  void layout_done (ActPass *ap);
  int layout_runcmd (ActPass *ap, const char *nm);

}

#endif /* __ACT_STK_LAYOUT_PASS_H__ */
