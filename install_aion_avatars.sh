#!/bin/bash
# ==============================================================================
# Links Aion's 13 GLB avatars (+ Mia glTF) into assets/models/ so that they show
# up in the Surreal Studio 3D Model Importer ([L] in Edit Mode).
# Symlinks only - no 200 MB of binaries get duplicated or committed.
# ==============================================================================
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="${AION_AVATAR_DIR:-/opt/meinos}"
DST="$HERE/assets/models"
mkdir -p "$DST"

names=(
  "01" "02" "03" "04" "05" "06" "07" "08" "09"
  "10_Lioness" "11_CyberBeast" "12_Celestial" "13_CyberCat"
)
count=0
for i in "${!names[@]}"; do
  if [ -f "$SRC/model_$i.glb" ]; then
    ln -sf "$SRC/model_$i.glb" "$DST/Aion_Avatar_${names[$i]}.glb"
    count=$((count + 1))
  fi
done

MIA="$HERE/../avatar/scene.gltf"
[ -f "$MIA" ] && ln -sf "$(readlink -f "$MIA")" "$DST/Aion_Avatar_Mia.gltf" && count=$((count + 1))

echo "[OK] $count Aion avatar models linked into $DST"

