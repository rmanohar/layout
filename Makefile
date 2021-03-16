#-------------------------------------------------------------------------
#
#  Copyright (c) 2018 Rajit Manohar
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 51 Franklin Street, Fifth Floor,
#  Boston, MA  02110-1301, USA.
#
#-------------------------------------------------------------------------
EXE=act2lef.$(EXT)
EXE2=actrectbbox.$(EXT)

TARGETS=$(EXE) $(EXE2) mag.pl
TARGETLIBS=pass_stk.so

OBJS1=main.o geom.o stk_layout.o tile.o stk_pass_dyn.o
OBJS2=main2.o geom.o stk_layout.o tile.o stk_pass_dyn.o

SRCS=$(OBJS1:.o=.cc) main2.cc

include $(VLSI_TOOLS_SRC)/scripts/Makefile.std

$(EXE): $(OBJS1) $(ACTPASSDEPEND)
	$(CXX) $(CFLAGS) $(OBJS1) -o $(EXE) $(LIBACTPASS)

pass_stk.so: stk_pass_dyn.os $(ACTPASSDEPEND)
	$(CXX) $(SH_LINK_OPTIONS) -o pass_stk.so stk_pass_dyn.os $(LIBACTPASS)

mag.pl: 
	git checkout mag.pl

$(EXE2): $(OBJS2) $(ACTPASSDEPEND)
	$(CXX) $(CFLAGS) $(OBJS2) -o $(EXE2) $(LIBACTPASS)

-include Makefile.deps
