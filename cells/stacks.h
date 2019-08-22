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
#ifndef __ACT_STACKS_H__
#define __ACT_STACKS_H__

#ifdef INTEGRATED_PLACER
#include "placer.h"
#else
class circuit_t;
#endif


list_t *stacks_create (netlist_t *N);
void geom_create_from_stack (Act *a, FILE *fplef,
			     circuit_t *ckt,
			     netlist_t *N, list_t *stacks,
			     int *sizex, int *sizey);


#endif /* __ACT_STACKS_H__ */
