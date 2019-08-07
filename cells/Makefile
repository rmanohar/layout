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
EXE=stk.$(EXT)

TARGETS=$(EXE)

TARGETCONF=layout.conf

LAYOUTLIB=libactlayout.a

OBJS=stk_pass.o cellgen.o main.o

SRCS=$(OBJS:.o=.cc)


include $(VLSI_TOOLS_SRC)/scripts/Makefile.std

DEPEND_FLAGS += -DINTEGRATED_PLACER
CFLAGS +=-std=c++11 -DINTEGRATED_PLACER
$(EXE): $(OBJS) $(ACT_HOME)/lib/$(LAYOUTLIB) $(ACTPASSDEPEND)
	$(CXX) $(CFLAGS) $(OBJS) -o $(EXE) -L. -lplacer -lactlayout $(LIBACTPASS)

simple2: stacks.o cellgen.o simple2.o
	$(CXX) $(CFLAGS) stacks.o cellgen.o simple2.o -o simple2 -L. -lplacer -lactlayout $(LIBACTPASS)

#$(EXE): $(OBJS) $(ACT_HOME)/lib/$(LAYOUTLIB) $(ACTPASSDEPEND)
#	$(CXX) $(CFLAGS) $(OBJS) -o $(EXE) -lactlayout $(LIBACTPASS)

-include Makefile.deps
