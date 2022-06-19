#!/usr/bin/env python3

"""python python mag2rect <mag_file> <mag layers/rect layers>

Written by: Ruslan Dashkin
Modified by: Rajit Manohar

the script takes a magic file and translates it into a rect file with
the same filename in the same location.

:param <mag_file>: the path of the magic file to be converted
"""

import sys
import re
from collections import defaultdict 

if (len(sys.argv) != 2):
  print ("Usage python mag2rect <mag_file>")
  sys.exit (1)

if (sys.argv[1][-4:] != ".mag"):
  print ("Expecting .mag extension for filename (replaced by .rect for output)")
  sys.exit (1)

# both files have the same name
mag_file = open(sys.argv[1], "r")
rect_file = open(sys.argv[1][:-3] + "rect", "w")

#
# map from mag -> rect format
#
# - metal names are m#, so shorten any long names
# - contacts also use short names
# - contacts also have vias
# - substrate diff is abbreviated as well
# - these are conventions from layout.conf
#
# unspecified layers : default is the identity map
#
rect_map =  {
  'metal1'      :   ['m1'],
  'metal2'      :   ['m2'],
  'metal3'      :   ['m3'],
  'ndcontact'   :   ['ndc','ndiffusion', 'm1'],
  'ndc'         :   ['ndc','ndiffusion', 'm1'],
  'pdcontact'   :   ['pdc','pdiffusion', 'm1' ],
  'pdc'         :   ['pdc','pdiffusion', 'm1'],
  'polycontact' :   ['pc','polysilicon', 'm1'],
  'pc'          :   ['pc','polysilicon', 'm1' ],
  'm2contact'   :   ['m2c','m1','m2', 'v1'],
  'm2c'         :   ['m2c','m1','m2', 'v1'],
  'psubstratepdiff':['ppdiff'],
  'psc'         :   ['psc', 'ppdiff', 'm1' ],
  'nsubstratendiff':['nndiff'],
  'nsc'         :   ['nsc', 'nndiff', 'm1' ],
}

#These are for mag file
parse_layer = '<<\s+(?P<layer>\w+)\s+>>'
parse_coord = 'rect\s+(?P<xl>[0-9-]+)\s+(?P<yl>[0-9-]+)\s+(?P<xh>[0-9-]+)\s+(?P<yh>[0-9-]+)'
parse_label = 'rlabel\s*(?P<layer>\w+)\s*(?P<xl>[0-9-]+)\s+(?P<yl>[0-9-]+)\s+(?P<xh>[0-9-]+)\s+(?P<yh>[0-9-]+)\s+\d+\s+(?P<label>[a-zA-Z0-9_()#]+)'

#label_map = defaultdict(list)
label_map = {}
label_layer = {}

labels_flag = 0
hash_need = 0

cur_layer_mag = ''
cur_layer = []
cur_bbox = ()
cur_label = ''

#
# Step 1: collect all the labels and their locations
#
# process labels
for line in mag_file:
  # skip line if not layer with label
  chk_line = re.search(parse_layer, line)
  if (chk_line):
    if (chk_line.group('layer').find('label') != -1):
      labels_flag = 1
    else:
      labels_flag = 0

  if (labels_flag == 0):
    continue

  #skip if no label parsed
  res = re.search(parse_label, line)
  if (res):
    # parse and save to local vars
    # extract lower left coordinate, label name, and layer name
    cur_bbox = (int(res.group('xl')),int(res.group('yl')))
    cur_label = res.group('label')
    cur_layer = res.group('layer')

    # map layer to list of layers
    if cur_layer in rect_map:
      cur_layer = rect_map[cur_layer][0]

    # ?? invalidate label for resricted addition? 
    if (cur_label.find('restr_') != -1):
      cur_label = '#'

    # ?? emove coordinate from label
    if (cur_label.find(res.group('xl')) != -1):
      cur_label = cur_label.replace('_' + res.group('xl'), '')
    if (cur_label.find(str(int(res.group('xl'))-1)) != -1):
      cur_label = cur_label.replace('_' + str(int(res.group('xl'))-1), '')

    # arrays are in square brackets; reformat labels
    cur_label = cur_label.replace('(','[')
    cur_label = cur_label.replace(')',']')

    # add # to internal nets
    if (cur_label[0].isdigit() == True):
      hash_need = 1
      for char in cur_label:
        if (char.isdigit() != True):
          hash_need = 0
          break

    if (hash_need == 1):
      cur_label = '#' + cur_label
      hash_need = 0
    
    # put label in list
    label_layer[cur_bbox] = cur_layer
    label_map[cur_bbox] = cur_label



#
# Second pass: emit the .rect file, now that we have collected all the labels
#
    
# start from the beginning again
mag_file.seek(0)

cur_layer = []
cur_label = ''
cur_bbox = ()
skip_layer = 0

# process all rectangles/layer content
for line in mag_file:
        # read layer and map, stop processing if arriving at
        # labels/end, skip on checkpaint
	if (re.search(parse_layer, line)):
		res = re.search(parse_layer, line)
		cur_layer_mag = res.group('layer')
		skip_layer = 0
		if (cur_layer_mag.find('labels') != -1):
                        skip_layer = 1
                        continue
		elif (cur_layer_mag.find('error') != -1):
			skip_layer = 1
			continue
		elif (cur_layer_mag.find('checkpaint') != -1):
			skip_layer = 1
			continue
		elif (cur_layer_mag.find('end') != -1):
			break

		if cur_layer_mag in rect_map:
			cur_layer = rect_map[cur_layer_mag]
		else:
			cur_layer = [ cur_layer_mag ]

	if (skip_layer != 1):
	  # get label for coordinates
	  if (re.search(parse_coord, line)):
	    res = re.search(parse_coord, line)
	    cur_bbox = (int(res.group('xl')),int(res.group('yl')),int(res.group('xh')),int(res.group('yh')))
	    # search for label inside the rectangle and on the same layer
	    for key in label_map:
	      if (key[0] >= cur_bbox[0] and key[0] <= cur_bbox[2]):
	        if (key[1] >= cur_bbox[1] and key[1] <= cur_bbox[3]):
	          if (label_layer[key] == cur_layer[0]):
	            cur_label = label_map[key]
	            break
	        else:
	          cur_label = '#'
	      else:
	        cur_label = '#'
	  
	  #skip line if cororinates were not parsed
	  if (len(cur_bbox) == 0):
	    continue
	  if (len(cur_label) == 0):
	    continue
	
	  # for each layer associated with the rectangle write line to rect file
	  for layer in cur_layer:
	    rect_file.write("rect " + cur_label + " " + layer + " " + str(cur_bbox[0]) + " " + str(cur_bbox[1]) + " " + str(cur_bbox[2]) + " " + str(cur_bbox[3]) + " " + "\n")
	
	  cur_bbox = ()
	  cur_label = ''
