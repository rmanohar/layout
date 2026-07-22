#!/bin/bash
set -euo pipefail

# gds2rect.sh -T <tech> -i <input.gds> [-o <output.rect>]
# -T <tech>        : REQUIRED.
# -i <input.gds>   : REQUIRED.
# -o <output.rect> : Optional. If omitted, derived by replacing .gds with .rect.

usage() {
  cat <<USAGE
Usage: $(basename "$0") -T <tech> -i <input.gds> [-o <output.rect>]

Example:
  $(basename "$0") -T my_tech -i layout.gds -o layout.rect
USAGE
  exit 2
}

TECH=""
IN_GDS=""
OUT_RECT=""

# Parse args (getopts style)
while getopts "T:i:o:h" opt; do
  case "$opt" in
    T) TECH="$OPTARG" ;;
    i) IN_GDS="$OPTARG" ;;
    o) OUT_RECT="$OPTARG" ;;
    h|*) usage ;;
  esac
done

if [ -z "$TECH" ]; then
  echo "Error: -T <tech> is required."
  usage
fi

if [ -z "$IN_GDS" ]; then
  echo "Error: -i <input.gds> is required."
  usage
fi

if [ ! -f "$IN_GDS" ]; then
  echo "Error: input GDS file '$IN_GDS' not found."
  exit 3
fi

# If OUT_RECT not provided, replace extension of IN_GDS with .rect
if [ -z "$OUT_RECT" ]; then
  # Strip trailing path and extension safely:
  # If IN_GDS has an extension, remove it; otherwise just append .rect
  basename_noext="${IN_GDS%.*}"
  OUT_RECT="${basename_noext}.rect"
fi

# Ensure magic exists
if ! command -v magic >/dev/null 2>&1; then
  echo "Error: 'magic' executable not found in PATH. Install Magic or adjust PATH."
  exit 4
fi

# Ensure techgen exists
if ! command -v techgen >/dev/null 2>&1; then
  echo "Error: 'techgen' executable not found in PATH. Install ACT or adjust PATH."
  exit 4
fi

echo "Using tech: $TECH"
echo "Input GDS: $IN_GDS"
echo "Output (rect): $OUT_RECT"
filename="${IN_GDS##*/}"

# Run Magic and issue commands.
techgen -T$TECH -dnull > ${TECH}_tmp.tech
magic -T${TECH}_tmp -noconsole <<MAGIC_CMDS
gds read "$IN_GDS"
load ${filename%.*}
flatten ${filename%.*}
save "${OUT_RECT%.*}.mag"
quit -noprompt 
MAGIC_CMDS
echo "mag -> rect"
python mag2rect.py ${OUT_RECT%.*}.mag 
rm ${TECH}_tmp.tech 
rm ${OUT_RECT%.*}.mag 


