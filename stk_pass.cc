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
#include <act/act.h>
#include <act/iter.h>
#include <act/passes/netlist.h>
#include <act/layout/stk_pass.h>
#include "stk_pass_dyn.h"


ActStackPass::ActStackPass(Act *a) : ActPass (a, "net2stk")
{
  stk_init (this);
  raw = stk_get ();
}


ActStackPass::~ActStackPass()
{
  stk_done ();
}


netlist_t *ActStackPass::getNL (Process *p)
{
  return raw->getNL (p);
}

void *ActStackPass::local_op (Process *proc, int mode)
{
  return stk_proc (proc, mode);
}


int ActStackPass::run (Process *p)
{
  int ret = ActPass::run (p);
  stk_run (p);
  return ret;
}


list_t *ActStackPass::getStacks(Process *p)
{
  return raw->getStacks (p);
}


int ActStackPass::isEmpty (list_t *stk)
{
  return raw->isEmpty (stk);
}


void ActStackPass::free_local (void *v)
{
  stk_free (v);
}


int RawActStackPass::isEmpty (list_t *stk)
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

list_t *RawActStackPass::getStacks (Process *p)
{
  Assert (p->isExpanded(), "What?");
  if (!getPass()->completed()) {
    warning ("ActStackPass: has not been run yet!");
    return NULL;
  }
  return (list_t *)getMap (p);
}
