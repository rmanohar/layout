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
}

void ActStackLayoutPass::cleanup()
{
  if (layoutmap) {
    std::map<Process *, Layout *>::iterator it;
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


void ActStackLayoutPass::_createlayout (Process *p)
{
  /* now what */
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
  cleanup();

  layoutmap = new std::map<Process *, Layout *>();

  _finished = 1;
  return 1;
}

