/*************************************************************************
 *
 *  Copyright (c) 2019 Rajit Manohar
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
#include <act/layout/geom.h>
#include <map>
#include <unordered_set>
#include <hash.h>

/*-- data structures --*/

class ActStackLayoutPass : public ActPass {
 public:
  ActStackLayoutPass (Act *a);
  ~ActStackLayoutPass ();

  int run (Process *p = NULL);

  /* computes the maximum cell height needed for all the cells
     involved in process p */
  int maxHeight (Process *p = NULL);

  LayoutBlob *getLayout (Process *p = NULL);

  /* this is mode 1 */
  void emitLEFHeader (FILE *fp);
  void emitWellHeader (FILE *fp);
  void emitLEF (FILE *fp, FILE *fpcell, Process *p, int do_rect = 0);

  void emitDEFHeader (FILE *fp, Process *p);
  void emitDEF (FILE *fp, Process *p, double pad = 1.4, int do_pins = 1);

  int emitRect (FILE *fp, Process *p);
  int haveRect (Process *p);

  double getArea () { return _total_area; }
  double getStdCellArea() { return _total_stdcell_area; }
  int getStdCellHeight() { if (_maxht == -1) { _maxht = maxHeight (NULL); } return _maxht; }

  /* this is mode 2 */
  void reportStats(Process *p);


 private:
  void *local_op (Process *p, int mode = 0);
  void free_local (void *v);

  long snap_up_x (long);
  long snap_up_y (long);
  long snap_dn_x (long);
  long snap_dn_y (long);
  

  /* mode 0 */
  LayoutBlob *_createlocallayout (Process *p);

  /* mode 1 */
  int _emitlocalLEF (Process *p);
  void _emitlocalRect (Process *p);
  void _emitLocalWellLEF (FILE *fp, Process *p);

  /* mode 2 */
  void _reportLocalStats(Process *p);

  /* mode 3 */
  void _maxHeightlocal (Process *p);

  /* welltap */
  LayoutBlob **wellplugs;
  netlist_t *dummy_netlist;	// dummy netlist

  /* aligned LEF boundary */
  LayoutBlob *computeLEFBoundary (LayoutBlob *b);

  ActStackPass *stk;

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

  std::unordered_set<Process *> *visited;
};


#endif /* __ACT_STK_LAYOUT_PASS_H__ */
