#-------------------------------------------------------------------------
#
#  Copyright (c) 2019 Rajit Manohar
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
# 
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 51 Franklin Street, Fifth Floor,
#  Boston, MA  02110-1301, USA.
#
#-------------------------------------------------------------------------

LIB=libactlayout_$(EXT).a

TARGETLIBS=$(LIB)

TARGETINCS=geom.h tech.h
TARGETINCSUBDIR=act/layout

TARGETCONF=layout.conf


OBJS=geom.o tech.o

SRCS=$(OBJS:.o=.cc)

include $(VLSI_TOOLS_SRC)/scripts/Makefile.std

$(LIB): $(OBJS)
	ar ruv $(LIB) $(OBJS)
	$(RANLIB) $(LIB)

-include Makefile.deps

