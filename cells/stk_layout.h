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

  Layout *getLayout (Process *p = NULL);
  int emitLEF (FILE *fp, Process *p);
#ifdef INTEGRATED_PLACER
  int createBlocks (circuit_t *ckt, Process *p);
#endif

  void emitLEFHeader (FILE *fp);
  int emitRect (FILE *fp, Process *p);

 private:
  int init ();
  void cleanup();

  void _createlayout (Process *p);
  void _createlocallayout (Process *p);

  ActStackPass *stk;

  int min_width;
  int fold_n_width;
  int fold_p_width;
  int lambda_to_scale;

  std::map<Process *, LayoutBlob *> *layoutmap;
};


#endif /* __ACT_STK_LAYOUT_PASS_H__ */
