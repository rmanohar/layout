#!/bin/python3
"""
This script converts gds files to rect formart depending on a ACT tech configuration

The recomended way for .gds -> .rect is to use gds2rect.sh: .gds -> Magic -> .mag -> mag2rect -> rect with netgen -T<tech>,
this script gds2rect.py is an alternative if Magic has some behavoir that make it not viable.

This script gds2rect will work fine in a lot of cases but has limitations:
    This script will ignore and not reverse layer-bloating generation of complex layers like n- or p-transistor.
    All pins have no direction, so will become inrects.
    This script does not handle hirachical gds/rects (it flattens - hopefully)
    
depends on modern gdsfactory (Klayout back end) - tested with 9.20(+)

Usage: gds2rect.py -T<tech> [-i]<input.gds> [-o<output.rect>] [-f]
gds2rect.py -cnf<tech.conf> [-i]<input.gds> [-o<output.rect>] [-f]

Accepted options:
  -T<tech> or -T <tech> or -cnf<path/layout.conf> or -cnf <path/layout.conf>
  -i<inputfile> or -i <inputfile>
  -o<outputfile> or -o <outputfile>
  <inputfile> 
  -f ignore mapping errors and contiure 

Note+TODO: Pins are just drawn as inrect, the direction property is not maintained
the way the rectangles are cut out is not optimal, and gds bloating is ignored

----
Copyright (c) 2025 Thomans Jagielski - Yale University
Copyright (c) 2025 Ole Richter - Technical University of Denmark

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor,
Boston, MA  02110-1301, USA.
"""


import os
from warnings import warn
import gdsfactory as gf
from itertools import chain
import sys
import shlex
from pprint import pprint


def find_in_stack(stack, name):
    try:
        return len(stack) - 1 - stack[::-1].index(name)
    except ValueError:
        return -1

def parse_conf_file(path):
    """
    this function reads the layout.conf and extracts all need to know information for gds layer conversion

    if you fix something here also fix the version in the rect2gds.py
    """
    scale = None

    # GDS definitions
    gds_layers = []
    gds_major = []
    gds_minor = []

    # primary outputs
    materials = {}        # material_name -> list of gds strings
    material_text = {}    # material_name -> gds string
    materials_bloat = {}  # material_name -> list of ints (gds_bloat)
    materials_mask = {}   # material_name -> list of gds strings (gds_masking
    metals = {}           # metal_name -> list of gds strings
    metal_text = {}       # metal_name -> string
    metal_pin = {}        # metal_name -> string
    metals_bloat = {}     # metal_name -> list of ints (_gds_bloat)
    vias = {}             # via_name -> list of gds strings
    via_text = {}         # via_name -> string
    vias_bloat = {}       # via_name -> list of ints (_gds_bloat)
    names = {}            # to store the mapping of the via names
    align = None


    stack = []

    with open(path, 'r', encoding='utf-8') as f:
        for raw in f:
            # strip inline comments starting with #
            line = raw.split('#', 1)[0].strip()
            if not line:
                continue

            # tokenize preserving quoted strings
            try:
                toks = shlex.split(line, posix=True)
            except ValueError:
                # skip malformed lines
                continue
            if not toks:
                continue

            kw = toks[0].lower()

            # begin/end blocks
            if kw == 'begin' and len(toks) >= 2:
                stack.append(toks[1].lower())
                continue
            if kw == 'end':
                if stack:
                    stack.pop()
                continue

            # scale in general
            if kw == 'real' and len(toks) >= 3 and toks[1].lower() == 'scale':
                try:
                    scale = int(toks[2])
                except Exception:
                    try:
                        scale = float(toks[2])
                    except Exception:
                        scale = toks[2]
                continue

            # handle `string` lines
            if kw == 'string' and len(toks) >= 3:
                key = toks[1].lower()
                if key.endswith('_name'):
                    base = key[:-5]  # drop the "_name" suffix
                    names[base] = toks[2]
                    continue
                elif key.endswith('gds_pin'):
                    if find_in_stack(stack, 'metal') != -1:
                        metalname = key[:-8]
                        metal_pin[metalname] = toks[2]
                    else:
                        warn('something other then metals should not have a pin '+str(key)+' => '+str(stack))
                elif key.endswith('gds_text'):
                    if find_in_stack(stack, 'metal') != -1:
                        metalname = key[:-9]
                        metal_text[metalname] = toks[2]
                    elif find_in_stack(stack, 'materials') != -1:
                        matname = stack[-1]
                        material_text[matname] = toks[2]
                    elif find_in_stack(stack, 'vias') != -1:
                        vianame = key[:-9]
                        via_text[matname] = toks[2]
                elif key == "gds_align":
                    align = toks[2]

            # string_table handling
            if kw == 'string_table' and len(toks) >= 2:
                table_name = toks[1]
                entries = toks[2:]

                # top-level gds layers
                if find_in_stack(stack, 'gds') != -1 and stack and stack[-1] == 'gds' and table_name.lower() == 'layers':
                    gds_layers = entries
                    continue

                # materials: inside materials block, each material is a sub-block
                if find_in_stack(stack, 'materials') != -1 and len(stack) >= 2:
                    # top of stack is material name
                    matname = stack[-1]
                    if table_name.lower() == 'gds':
                        materials[matname] = entries
                        continue
                    elif table_name.lower() == 'gds_mask':
                        materials_mask[matname] = entries
                        continue

                # metals: string_table <metal>_gds under metal block
                if find_in_stack(stack, 'metal') != -1:
                    if table_name.endswith('_gds'):
                        metalname = table_name[:-4]
                        metals[metalname] = entries
                        continue

                # vias: string_table <via>_gds under vias block
                if find_in_stack(stack, 'vias') != -1:
                    if table_name.endswith('_gds'):
                        vianame = names[table_name[:-4]]
                        vias[vianame] = entries
                        continue

                continue

            # int_table handling (majors/minors and bloat factors)
            if kw == 'int_table' and len(toks) >= 3:
                key = toks[1].lower()
                nums = []
                for t in toks[2:]:
                    try:
                        nums.append(int(t))
                    except Exception:
                        # ignore non-int tokens
                        pass

                # gds major/minor in top-level gds block
                if find_in_stack(stack, 'gds') != -1 and stack and stack[-1] == 'gds':
                    if key == 'major':
                        gds_major = nums
                        continue
                    elif key == 'minor':
                        gds_minor = nums
                        continue

                # materials: int_table gds_bloat inside a material block (parent = materials)
                if find_in_stack(stack, 'materials') != -1 and len(stack) >= 2:
                    matname = stack[-1]
                    if key == 'gds_bloat':
                        materials_bloat[matname] = nums
                        continue

                # metals/vias: int_table <name>_gds_bloat
                if key.endswith('_gds_bloat'):
                    base = key[:-10]  # remove suffix
                    if find_in_stack(stack, 'metal') != -1:
                        metals_bloat[base] = nums
                        continue
                    if find_in_stack(stack, 'vias') != -1:
                        vias_bloat[base] = nums
                        continue

                # If none matched, ignore
                continue

            # ignore other lines

    # build gds mapping: layer -> (major, minor)
    gds = {}
    if gds_layers:
        for i, name in enumerate(gds_layers):
            gds[name] = (gds_major[i],
                         gds_minor[i])

    scale = scale*1000 # convert to nm
    return (
        scale,
        gds,
        materials,
        material_text,
        materials_bloat,
        materials_mask,
        metals,
        metal_pin,
        metal_text,
        metals_bloat,
        vias,
        via_text,
        vias_bloat,
        align
    )

def resolve_paths_from_Targ(argv):
    """
    Parse argv and return (layout_conf_path, input_path, output_path).

    Accepted forms:
      -T<tech> or -T <tech> or -cnf<path/layout.conf> or -cnf <path/layout.conf>
      -i<inputfile> or -i <inputfile>
      -o<outputfile> or -o <outputfile>
      <inputfile> 
      -f

    Returns:
      (layout_conf_path, input_path, output_path)

    Raises SystemExit with usage/error messages on failure.
    """
    tech = None
    techfile = None
    inp = None
    out = None
    force = False

    i = 1
    while i < len(argv):
        a = argv[i]
        if a.startswith('-T') and len(a) > 2:
            tech = a[2:]
        elif a == '-T' and i + 1 < len(argv):
            tech = argv[i + 1]
            i += 1
        elif a.startswith('-cnf') and len(a) > 4:
            techfile = a[4:]
        elif a == '-cnf' and i + 1 < len(argv):
            techfile = argv[i + 1]
            i += 1
        elif a.startswith('-i') and len(a) > 2:
            inp = a[2:]
        elif a == '-i' and i + 1 < len(argv):
            inp = argv[i + 1]
            i += 1
        elif a.startswith('-o') and len(a) > 2:
            out = a[2:]
        elif a == '-o' and i + 1 < len(argv):
            out = argv[i + 1]
            i += 1
        elif a == '-f':
            force = True
        elif inp is None:
            inp = a
        i += 1

    if (tech is None and techfile is None) or inp is None:
        raise SystemExit("Usage: rect2gds.py -T<tech> [-i]<input.gds> [-o<output.rect>] or rect2gds.py -cnf<layout.conf> [-i]<input.gds> [-o<output.rect>]")
    elif out is None:
        #assuming file name ends with ".gds"
        out = inp[:-4]+str(".rect")

    # Expand environment variables and ~
    inp = os.path.expanduser(os.path.expandvars(inp))
    out = os.path.expanduser(os.path.expandvars(out))

    if tech:
        act_home = os.environ.get('ACT_HOME')
        if not act_home:
            raise SystemExit("Environment variable ACT_HOME not set")

        layout = os.path.join(act_home, 'conf', tech, 'layout.conf')
    else:
        layout = os.path.expanduser(os.path.expandvars(techfile))
    if not os.path.isfile(layout):
        raise SystemExit(f"Layout conf not found: {layout}")

    if not os.path.isfile(inp):
        raise SystemExit(f"Input file not found: {inp}")

    out_dir = os.path.dirname(out) or '.'
    if not os.path.isdir(out_dir):
        raise SystemExit(f"Output directory does not exist: {out_dir}")

    return layout, inp, out, force

def decompose_and_write_align(align, align_layer, c, f, all_labels, scale, gf_layer_names, rect_type="rect", force = False):
    """
    This will take all constructed layer pairs of (rect layer name, gds_foundry layer object), 
    pass the read gds though them -> c
    take all labels form c and matching layers -> gf_layer_names
    cut all poligons in rectangles (hopefully)
    write rectangles into file
    """
    # this is a magic number that is adaped to how the klayout backend makes the conversion from unit (DPoint) to unit less (Point) points when decomposing
    INTERNAL_DB_SCALE = 1000    
    ## so normally there should be only one PR boundry that is square, but we run though the decomposition just to be sure   
    layer_region = align_layer.get_shapes(c)
    # here we lose database attachment
    polygons = layer_region.decompose_trapezoids()
    pr_writen = False
    for shape in polygons:
        polygon = None
        if shape.is_polygon() or shape.is_box() or shape.is_path() or shape.is_simple_polygon():
            polygon = shape.polygon
            #for a path i am unsure if it is decomposed
        else:
            warn("found a Shape of a type the script cant handle, Skipping -> output will be faulty")
            if not force:
                exit(3)
        if polygon:
            if  polygon.is_box():
                p1 =  polygon.bbox().p1
                p2 =  polygon.bbox().p2
                #the polygon shapes are not attached to a database so points are integer of scaling database unit (in this case nm at the time of writing)
                f.write(f"{rect_type} # $align {int(p1.x*scale/INTERNAL_DB_SCALE)} {int(p1.y*scale/INTERNAL_DB_SCALE)} {int(p2.x*scale/INTERNAL_DB_SCALE)} {int(p2.y*scale/INTERNAL_DB_SCALE)}\n")
                if pr_writen:
                    warn("More than one aligh/PR boundry\n")
                pr_writen = True
            elif polygon.is_rectilinear():
                warn("poligon is rectilinear but not a rectange, decomposition failed, Poligon will be skipped: "+str(polygon)+ " on layer "+str(layerk))
                if not force:
                    exit(4)
                continue
            elif polygon.is_halfmanhattan():
                warn("45 degree angles are not supported yet, Poligon will be skipped: "+str(polygon)+ " on layer "+str(layerk))
                if not force:
                    exit(5)
                continue
            else:
                warn("Non manhatten geometry dectected after rectangle decomposition, decomposition failed, Polygon will be skipped: "+str(polygon)+ " on layer "+str(layerk))
                if not force:
                    exit(6)
                continue
        else:
            warn("Polygon is None, this should not happen")
            if not force:
                exit(7)
        
        for label in all_labels[align]:
            pr_marker = label.position().to_itype(1/INTERNAL_DB_SCALE)
            f.write(f"{rect_type} {label.string.strip()} $align {int(pr_marker.x*scale/INTERNAL_DB_SCALE)} {int(pr_marker.y*scale/INTERNAL_DB_SCALE)} {int(pr_marker.x*scale/INTERNAL_DB_SCALE)} {int(pr_marker.y*scale/INTERNAL_DB_SCALE)}\n")

def decompose_and_write_rect(layer_touples, c, f, all_labels, scale, gf_layer_names, rect_type="rect", force = False):
    """
    This will take all constructed layer pairs of (rect layer name, gds_foundry layer object), 
    pass the read gds though them -> c
    take all labels form c and matching layers -> gf_layer_names
    cut all poligons in rectangles (hopefully)
    write rectangles into file
    """
    # this is a magic number that is adaped to how the klayout backend makes the conversion from unit (DPoint) to unit less (Point) points when decomposing
    INTERNAL_DB_SCALE = 1000       
    for layerk, layer in layer_touples:
        layer_region = layer.get_shapes(c)
        # here we lose database attachment
        polygons = layer_region.decompose_trapezoids()
        for shape in polygons:
            polygon = None
            if shape.is_polygon() or shape.is_box() or shape.is_path() or shape.is_simple_polygon():
                polygon = shape.polygon
                #for a path i am unsure if it is decomposed
            else:
                warn("found a Shape of a type the script cant handle, Skipping -> output will be faulty")
                if not force:
                    exit(3)
            if polygon:
                if  polygon.is_box():
                    text = "#"
                    #check if label sits on rectengle
                    for layer_name in gf_layer_names[layerk]:
                        for label in all_labels[layer_name]:
                            if polygon.inside(label.position().to_itype(1/INTERNAL_DB_SCALE)):
                                text = label.string.strip()
                            continue
                    #the rectangle
                    p1 =  polygon.bbox().p1
                    p2 =  polygon.bbox().p2
                    #the polygon shapes are not attached to a database so points are integer of scaling database unit (in this case nm at the time of writing)
                    f.write(f"{rect_type} {text} {layerk} {int(p1.x*scale/INTERNAL_DB_SCALE)} {int(p1.y*scale/INTERNAL_DB_SCALE)} {int(p2.x*scale/INTERNAL_DB_SCALE)} {int(p2.y*scale/INTERNAL_DB_SCALE)}\n")
                elif polygon.is_rectilinear():
                    warn("poligon is rectilinear but not a rectange, decomposition failed, Poligon will be skipped: "+str(polygon)+ " on layer "+str(layerk))
                    if not force:
                        exit(4)
                    continue
                elif polygon.is_halfmanhattan():
                    warn("45 degree angles are not supported yet, Poligon will be skipped: "+str(polygon)+ " on layer "+str(layerk))
                    if not force:
                        exit(5)
                    continue
                else:
                    warn("Non manhatten geometry dectected after rectangle decomposition, decomposition failed, Polygon will be skipped: "+str(polygon)+ " on layer "+str(layerk))
                    if not force:
                        exit(6)
                    continue
            else:
                warn("Polygon is None, this should not happen")
                if not force:
                    exit(7)


def get_all_labels(cell, used_layers, flat = True):
    labels = {}
    for lk,lv in used_layers.items():
        labels[lk]= cell.get_labels(lv.layer,flat)
    return labels


def gds_to_rect(gds_path: str, rect_path: str,
        gds, 
        materials, 
        material_text,
        material_mask,
        vias, 
        via_text,
        metals,
        metal_pin,
        metal_text,
        align,
        scale: float = 1000.0,
        force = False,
        flat = True):
    """
    The Idea is:
    construct corresonding layers in gdsfoundry for all rect layers, so also layer stacks like eg ptransistor:
    first with all the layout layer config for materials, vias, and metals,
    go and iterate over first all composit types (more than on GDS layer).
    create Derived layers for metal pins.
    for each complex layer create if not exsistant a DerivedLayer in gdsfactory, store it in a dict.

    ask the layers for thier shapes (poligons), pass them through poligon cutting, check if label is associated and assign to all cuts
    """
    c = gf.import_gds(gds_path)
    #base1 = gf.technology.LogicalLayer([3,0])

    used_layers = {}
    
    for act_layer in chain(materials.values(), metals.values(), vias.values()):
        for gds_layer in act_layer:
            if gds_layer not in used_layers:
                used_layers[gds_layer] = gf.technology.LogicalLayer(layer=gds[gds_layer], name=gds_layer)
    for gds_layer in chain(material_text.values(), metal_text.values(), via_text.values(), metal_pin.values()):
        if gds_layer not in used_layers:
                used_layers[gds_layer] = gf.technology.LogicalLayer(layer=gds[gds_layer], name=gds_layer)
    if align:
        if align not in used_layers:
                used_layers[align] = gf.technology.LogicalLayer(layer=gds[align], name=align)

    #check if all layers are specified
    end = False
    for layer in c.layers:
        test = False
        for llayer in used_layers.values():
            if layer == llayer.layer:
                test = True
                break
        if not test:
            warn("A used GDS later is not defind in Act layout.conf as a layer and will be skipped: "+str(layer))
            end = True
            
    if end and not force:
        exit(2)

    # this are the reverse lookup mappings
    gf_layers = {}
    gf_layer_names = {}
    gf_pinlayers = {}

    #construct layer stacks for composit types
    for mk, mv in vias.items():
        if len(mv) <= 1:
            gf_layers[mk] = used_layers[mv[0]]
        else:
            # via consisting out of layer stack
            tmp_layer = used_layers[mv[0]]
            for i in range(1,len(mv)):
                tmp_layer = gf.technology.DerivedLayer(layer1=tmp_layer,layer2=used_layers[mv[i]],operation='and')
            gf_layers[mk] = tmp_layer
        # label layer lookup
        gf_layer_names[mk] = mv
        # label layer for vias
        if mk in via_text:
            if via_text[mk] not in gf_layer_names[mk]:
                gf_layer_names[mk].append(via_text[mk])
    
    for mk, mv in metals.items():
        if len(mv) <= 1:
            metal_tmp = used_layers[mv[0]]
        else:
            # metal stacks (should be rare)
            tmp_layer = used_layers[mv[0]]
            for i in range(1,len(mv)):
                tmp_layer = gf.technology.DerivedLayer(layer1=tmp_layer,layer2=used_layers[mv[i]],operation='and')
            metal_tmp = tmp_layer
        # label layer lookup
        gf_layer_names[mk] = mv
        # metals can have pins, pins are pin marker + metal (stack)
        if mk in metal_pin:
            gf_layers[mk] = gf.technology.DerivedLayer(layer1=metal_tmp,layer2=used_layers[metal_pin[mk]],operation='not')
            gf_pinlayers[mk] = gf.technology.DerivedLayer(layer1=metal_tmp,layer2=used_layers[metal_pin[mk]],operation='and')
            if metal_pin[mk] not in gf_layer_names[mk]:
                gf_layer_names[mk].append(metal_pin[mk])
        #metal label layer
        #Hmm that assumtion might not be correct, handle text shapes as pins if pins not configured:
        elif mk in metal_text and mv[0] != metal_text[mk]:
            gf_layers[mk] = gf.technology.DerivedLayer(layer1=metal_tmp,layer2=used_layers[metal_text[mk]],operation='not')
            #gf_layers[mk] = metal_tmp
            gf_pinlayers[mk] = gf.technology.DerivedLayer(layer1=metal_tmp,layer2=used_layers[metal_text[mk]],operation='and')
        else:
            gf_layers[mk] = metal_tmp
        # add metal label
        if mk in metal_text:           
            if metal_text[mk] not in gf_layer_names[mk]:
                gf_layer_names[mk].append(metal_text[mk])

    for mk, mv in materials.items():
        if len(mv) <= 1:
            material_tmp = used_layers[mv[0]]
        else:
            # material stack
            tmp_layer = used_layers[mv[0]]
            for i in range(1,len(mv)):
                tmp_layer = gf.technology.DerivedLayer(layer1=tmp_layer,layer2=used_layers[mv[i]],operation='and')
            material_tmp = tmp_layer
        #label layer lookup
        gf_layer_names[mk] = mv
        # mask the complex layer by further layers:
        if mk in material_mask.keys():
            if len(materials_mask[mk]) <= 1:
                mask_tmp = used_layers[materials_mask[mk][0]]
            else:
                tmp_layer = used_layers[materials_mask[mk][0]]
                for i in range(1,len(materials_mask[mk])):
                    tmp_layer = gf.technology.DerivedLayer(layer1=tmp_layer,layer2=used_layers[materials_mask[mk][i]],operation='or')
                mask_tmp = tmp_layer
            gf_layers[mk] = gf.technology.DerivedLayer(layer1=material_tmp,layer2=mask_tmp,operation='not')
        else:
            gf_layers[mk] = material_tmp
        # add label layer
        if mk in material_text:
            if material_text[mk] not in gf_layer_names[mk]:
                gf_layer_names[mk].append(material_text[mk])

    all_labels = get_all_labels(c,used_layers=used_layers,flat=flat)

    with open(rect_path, "w") as f:
        # Write bbox
        # bbox is a database point (Dpoint) -> float in um
        f.write(f"bbox  {int(c.bbox().p1.x*scale)} {int(c.bbox().p1.y*scale)} {int(c.bbox().p2.x*scale)} {int(c.bbox().p2.y*scale)}\n")
        if align:
            decompose_and_write_align(align,used_layers[align],c,f,all_labels,scale,gf_layer_names,force=force)
        decompose_and_write_rect(gf_pinlayers.items(), c, f, all_labels, scale, gf_layer_names, "inrect",force)
        decompose_and_write_rect(gf_layers.items(), c, f, all_labels, scale, gf_layer_names, "rect",force)


if __name__ == '__main__':
    path, inputfile, outputfile, force = resolve_paths_from_Targ(sys.argv)
    scale, gds, materials, material_text, materials_bloat, materials_mask, metals, metal_pin, metal_text, metals_bloat, vias, via_text, vias_bloat, align = parse_conf_file(path)
    rect_object = gds_to_rect(gds_path=inputfile, rect_path=outputfile, 
        gds=gds, materials=materials, 
        material_text=material_text,
        material_mask=materials_mask,
        vias=vias, 
        via_text=via_text,
        metals=metals,
        metal_pin=metal_pin,
        metal_text=metal_text,
        align=align,
        scale=scale,
        force=force)
    print("output file written to "+str(outputfile))
