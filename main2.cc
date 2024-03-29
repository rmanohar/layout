/*************************************************************************
 *
 *  Copyright (c) 2020 Rajit Manohar
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
#include "stk_layout.h"

int main (int argc, char **argv)
{
  Act::Init (&argc, &argv, "layout:layout.conf");
  {
    char *tmpfile = config_file_name ("macros.conf");
    if (tmpfile) {
      FREE (tmpfile);
      config_read ("macros.conf");
    }
  }
  
  if (argc != 3) {
    fatal_error ("Usage: %s <actfile> <rectfile>\n", argv[0]);
  }

  Act *a = new Act (argv[1]);

  new ActNetlistPass (a);
  new ActDynamicPass (a, "net2stk", "pass_stk.so", "stk");
  ActDynamicPass *dp = new ActDynamicPass(a, "stk2layout", "pass_layout.so", "layout");

  Layout *l = new Layout (NULL);
  l->ReadRect (argv[2], 1);

  ActStackLayout *lp = (ActStackLayout *)dp->getPtrParam ("raw");
  
  long llx, lly, urx, ury;

  /* NOTE: this has to be kept consistent with computeLEFboundary */
  l->getBloatBBox (&llx, &lly, &urx, &ury);
  
  Assert (Technology::T->nmetals >= 3, "Hmm");

  llx = lp->snap_dn_x (llx);
  urx = lp->snap_up_x (urx+1);

  lly = lp->snap_dn_y (lly);
  ury = lp->snap_up_y (ury+1);

  printf ("bbox %ld %ld %ld %ld\n", llx, lly, urx, ury);
  return 0;
}
