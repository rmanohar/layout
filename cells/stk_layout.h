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

#ifdef INTEGRATED_PLACER
#include "placer.h"
#endif


/* lef/def conversion factor to microns */
#define MICRON_CONVERSION 2000

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
  int emitLEF (FILE *fp, Process *p);
  
#ifdef INTEGRATED_PLACER
  int createBlocks (circuit_t *ckt, Process *p);
#endif

  void emitLEFHeader (FILE *fp);
  void emitDEFHeader (FILE *fp, Process *p);
  void emitDEF (FILE *fp, Process *p, double pad = 1.4, int do_pins = 1);
  
  int emitRect (FILE *fp, Process *p);
  int haveRect (Process *p);

 private:
  int init ();
  void cleanup();

  int _maxHeight (Process *p);
  void _createlayout (Process *p);
  void _createlocallayout (Process *p);

  ActStackPass *stk;

  int min_width;
  int fold_n_width;
  int fold_p_width;
  int lambda_to_scale;

  /* temporary variables */
  int _total_instances;
  double _total_area;

  std::map<Process *, LayoutBlob *> *layoutmap;
  std::unordered_set<Process *> *visited;
};


#endif /* __ACT_STK_LAYOUT_PASS_H__ */
