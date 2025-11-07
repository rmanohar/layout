#!/bin/python3
"""
This script converts rect files to GDS depending on a ACT tech configuration

requires gdsfactory, tested with version 6.11 and 9.20(+)

Usage: rect2gds.py -T<tech> [-i]<input.rect> [-o<output.gds>]
       rect2gds.py -cnf<tech.conf> [-i]<input.rect> [-o<output.gds>]

Accepted options:
  -T<tech> or -T <tech> or -cnf<path/layout.conf> or -cnf <path/layout.conf>
  -i<inputfile> or -i <inputfile>
  -o<outputfile> or -o <outputfile>
  <inputfile> 

Note+TODO: Pins are just drawn as rectangles, the direction property is not maintained
merging all the drawn rectangles is recomended in EDA tools for cleaner use.

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
import gdsfactory as gf

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

    if you fix something here also fix the version in the gds2rect.py
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
    metals = {}           # metal_name -> list of gds strings
    metal_text = {}           # metal_name -> string
    metal_pin = {}           # metal_name -> string
    metals_bloat = {}     # metal_name -> list of ints (_gds_bloat)
    vias = {}             # via_name -> list of gds strings
    via_text = {}             # via_name -> string
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
                        Warning('something other then metals should not have a pin '+str(key)+' => '+str(stack))
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

    scale = scale/1000 # convert to um
    return (
        scale,
        gds,
        materials,
        material_text,
        materials_bloat,
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
      -T<tech> or -T <tech> -cnf<path/layout.conf> or -cnf <path/layout.conf>
      -i<inputfile> or -i <inputfile>
      -o<outputfile> or -o <outputfile>
      <inputfile> 

    Returns:
      (layout_conf_path, input_path, output_path)

    Raises SystemExit with usage/error messages on failure.
    """
    tech = None
    techfile = None
    inp = None
    out = None

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
        elif inp is None:
            inp = a
        i += 1

    if (tech is None and techfile is None) or inp is None:
        raise SystemExit("Usage: rect2gds.py -T<tech> [-i]<input.rect> [-o<output.gds>] or rect2gds.py -cnf<layout.conf> [-i]<input.rect> [-o<output.gds>]")
    elif out is None:
        #assuming file name ends with ".rect"
        out = inp[:-5]+str(".gds")

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

    return layout, inp, out

def add_box(component, x0, y0, x1, y1, layer):
    component.add_polygon([(x0, y0), (x1, y0), (x1, y1), (x0, y1)], layer=layer)

def add_pin(component, x0, y0, x1, y1, layer, direction):
    # so far pins are ignored, i dont know if gds has a notion of what a pin is, and gdsfactory has a non compatible notion of ports
    component.add_polygon([(x0, y0), (x1, y0), (x1, y1), (x0, y1)], layer=layer)


def parse_layout_file(filepath: str, gds, materials, vias, metals, materials_bloat, vias_bloat, metals_bloat, material_text, via_text, metal_text, metal_pin, align, scale: float = 1.0):
    filename = os.path.basename(filepath)
    name = os.path.splitext(filename)[0]
    c = gf.Component(name)

    with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue

            parts = line.split()
            cmd = parts[0]

            if cmd == "bbox":
                # Ignore bbox line completely (no geometry added)
                continue
            # if pin handeling is implemented in the layout.conf inrect and outrect will be pins, not metals
            elif cmd in {"rect", "inrect", "outrect"}:
                port_name = parts[1]
                layer_name = parts[2]
                x0, y0, x1, y1 = map(int, parts[3:7])

                # Scale coordinates
                x0, y0, x1, y1 = x0 * scale, y0 * scale, x1 * scale, y1 * scale

                # Special handling for transistor layers
                if layer_name in materials:
                    for i, layer in enumerate(materials[layer_name]):
                        gds_layer = gds[layer]
                        if layer_name in materials_bloat.keys():
                            gds_bloat = materials_bloat[layer_name][i]
                        else:
                            gds_bloat = 0
                    
                        # Draw both layers
                        add_box(c, x0-gds_bloat*scale, y0-gds_bloat*scale, x1+gds_bloat*scale, y1+gds_bloat*scale, gds_layer)
                    if layer_name in material_text:
                        layer_text = gds[material_text[layer_name]]
                    else:
                        layer_text = gds[materials[layer_name][0]]
                    if port_name != "#":
                        center = ((x0 + x1) / 2, (y0 + y1) / 2)
                        c.add_label(
                            port_name,
                            position=center,
                            layer=layer_text,
                        )
                elif layer_name in metals:
                    if cmd in {"inrect", "outrect"}:
                        # this is bad as it assumes metal pin naming
                        if cmd == "outrect":
                            direction = 0;
                        else:
                            direction = 1;
                        if layer_name in metal_pin:
                            add_pin(c, x0, y0, x1, y1, gds[metal_pin[layer_name]], direction)
                    for i, layer in enumerate(metals[layer_name]):
                        gds_layer = gds[layer]
                        if layer_name in metals_bloat.keys():
                            gds_bloat = metals_bloat[layer_name][i]
                        else:
                            gds_bloat = 0
                    
                        add_box(c, x0-gds_bloat*scale, y0-gds_bloat*scale, x1+gds_bloat*scale, y1+gds_bloat*scale, gds_layer)
                    if layer_name in metal_text:
                        layer_text = gds[metal_text[layer_name]]
                    else:
                        layer_text = gds[metals[layer_name][0]]
                    if port_name != "#":
                        center = ((x0 + x1) / 2, (y0 + y1) / 2)
                        c.add_label(
                            port_name,
                            position=center,
                            layer=layer_text,
                        )
                elif layer_name in vias:
                    for i, layer in enumerate(vias[layer_name]):
                        gds_layer = gds[layer]
                        if layer_name in vias_bloat.keys():
                            gds_bloat = vias_bloat[layer_name][i]
                        else:
                            gds_bloat = 0

                        add_box(c, x0-gds_bloat*scale, y0-gds_bloat*scale, x1+gds_bloat*scale, y1+gds_bloat*scale, gds_layer)
                    if layer_name in via_text:
                        layer_text = gds[via_text[layer_name]]
                    else:
                        layer_text = gds[vias[layer_name][0]]
                    center = ((x0 + x1) / 2, (y0 + y1) / 2)
                    if port_name != "#":
                        center = ((x0 + x1) / 2, (y0 + y1) / 2)
                        c.add_label(
                            port_name,
                            position=center,
                            layer=layer_text,
                        )
                elif layer_name == "$align":
                    if not align:
                        raise ValueError("align statement found but align_gds not configured in layout.conf")
                    if port_name == "#":
                        add_box(c, x0, y0, x1, y1, gds[align])
                    else:
                        c.add_label(
                            port_name,
                            position=(x0, y0),
                            layer=gds[align],
                        )
                else:
                    print("else")
                    layer = gds[layer_name]
                    if not layer:
                        raise ValueError(f"Unknown layer '{layer_name}'")

                    add_box(c, x0, y0, x1, y1, layer)
                    if port_name != "#":
                        center = ((x0 + x1) / 2, (y0 + y1) / 2)
                        c.add_label(
                            port_name,
                            position=center,
                            layer=layer_text,
                        )
    return c


if __name__ == '__main__':
    path, inputfile, outputfile = resolve_paths_from_Targ(sys.argv)
    scale, gds, materials, material_text, materials_bloat, metals, metal_pin, metal_text, metals_bloat, vias, via_text, vias_bloat, align = parse_conf_file(path)
    gds_object = parse_layout_file(filepath=inputfile, 
        gds=gds, materials=materials, 
        materials_bloat=materials_bloat, 
        material_text=material_text,
        vias=vias, 
        vias_bloat=vias_bloat,
        via_text=via_text,
        metals=metals,
        metals_bloat=metals_bloat,
        metal_pin=metal_pin,
        metal_text=metal_text,
        align=align,
        scale=scale
        )
    gds_object.write_gds(outputfile)
