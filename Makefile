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

TARGETINCS=geom.h tile.h attrib.h subcell.h
TARGETINCSUBDIR=layout

TARGETS=$(EXE) $(EXE2)
TARGETSCRIPTS=mag.pl rect2lef.pl mag2rect.py
TARGETLIBS=libact_layout.so pass_stk.so pass_layout.so

OBJS_EXE=main.o
OBJS_EXE2=main2.o stk_pass.o stk_layout.o

SHOBJS=geom.os tile.os subcell.os \
	geom_layer.os \
	geom_blob.os attrib.os

SHOBJS_PASS=stk_pass.os 

SHOBJS_PASS2=stk_pass.os stk_layout.os 


OBJS=$(OBJS_EXE) $(OBJS_EXE2) $(SHOBJS) $(SHOBJS_PASS) $(SHOBJS_PASS2)

SRCS=$(OBJS_EXE:.o=.cc) $(OBJS_EXE2:.o=.cc) $(SHOBJS:.os=.cc) $(SHOBJS_PASS:.os=.cc) $(SHOBJS_PASS2:.os=.cc)

LAY_SH_INCL=-L$(ACT_HOME)/lib -lact_layout

include $(ACT_HOME)/scripts/Makefile.std

$(EXE): $(OBJS_EXE)
	$(CXX) $(SH_EXE_OPTIONS) $(CFLAGS) main.o -o $(EXE) $(SHLIBACTPASS)

libact_layout.so: $(SHOBJS) 
	$(ACT_HOME)/scripts/linkso libact_layout.so $(SHOBJS) $(SHLIBACTPASS)
	$(ACT_HOME)/scripts/install libact_layout.so $(INSTALLLIB)/libact_layout.so

$(EXE2): $(OBJS_EXE2) $(ACTPASSDEPEND) libact_layout.so
	$(CXX) $(SH_EXE_OPTIONS) $(CFLAGS) $(OBJS_EXE2) $(LAY_SH_INCL) -o $(EXE2) $(LIBACTPASS)

pass_stk.so: $(SHOBJS_PASS) $(ACTPASSDEPEND)
	$(ACT_HOME)/scripts/linkso pass_stk.so $(SHOBJS_PASS) $(SHLIBACTPASS)

pass_layout.so: $(SHOBJS_PASS2) $(ACTPASSDEPEND) libact_layout.so
	$(ACT_HOME)/scripts/linkso pass_layout.so $(SHOBJS_PASS2) $(LAY_SH_INCL)  $(SHLIBACTPASS)

mag.pl: 
	git checkout mag.pl

rect2lef.pl:
	git checkout rect2lef.pl

-include Makefile.deps
