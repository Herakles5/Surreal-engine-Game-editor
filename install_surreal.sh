#!/usr/bin/env bash
set -e

# ==============================================================================
# AION SURREAL 3D ENGINE - LINUX & COSMOS HYBRID SUITE INSTALLER
# Installs Headers, Binaries, Pkg-Config, and Assets to System Directories
# ==============================================================================

PREFIX="${PREFIX:-/usr/local}"
SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "===================================================================="
echo "  AION SURREAL 3D ENGINE - LINUX DISTRIBUTION INSTALLER"
echo "  Target Prefix: ${PREFIX}"
echo "===================================================================="

if [ "$EUID" -ne 0 ]; then
    echo "[!] Note: Installing to ${PREFIX} typically requires root / sudo."
fi

# 1. Create System Directories
echo "[*] Creating system installation directories..."
mkdir -p "${PREFIX}/include/surreal"
mkdir -p "${PREFIX}/bin"
mkdir -p "${PREFIX}/share/surreal/avatar"
mkdir -p "${PREFIX}/lib/pkgconfig"

# 2. Compile Shared Third-Party Object & Engine Binaries
echo "[*] Building Shared Third-Party Object..."
g++ -O3 -std=c++20 -c "${SRC_DIR}/surreal_third_party.cpp" -o "${SRC_DIR}/surreal_third_party.o"

echo "[*] Building Surreal Studio & Dojo Editor (C++20 + AVX2 + OpenGL 4.6 + OpenMP)..."
g++ -O3 -mavx2 -mfma -fopenmp -pthread -std=c++20 -DSURREAL_EXTERNAL_THIRD_PARTY_IMPL \
    "${SRC_DIR}/surreal_live_dojo.cpp" "${SRC_DIR}/surreal_third_party.o" \
    -lSDL2 -lGL -ldl -lm -fopenmp -o "${SRC_DIR}/surreal_live_dojo"

echo "[*] Building Surreal Benchmark Utility..."
g++ -O3 -mavx2 -mfma -fopenmp -pthread -std=c++20 -DSURREAL_EXTERNAL_THIRD_PARTY_IMPL \
    "${SRC_DIR}/test_surreal_benchmark.cpp" "${SRC_DIR}/surreal_third_party.o" \
    -lSDL2 -lGL -lm -pthread -fopenmp -o "${SRC_DIR}/test_surreal_benchmark"

echo "[*] Building Photo-to-3D Utility..."
g++ -O3 -mavx2 -mfma -fopenmp -pthread -std=c++20 -DSURREAL_EXTERNAL_THIRD_PARTY_IMPL \
    "${SRC_DIR}/photo3d_preview.cpp" "${SRC_DIR}/surreal_third_party.o" \
    -lm -pthread -fopenmp -o "${SRC_DIR}/surreal-photo3d"

# 3. Install Headers
echo "[*] Installing C++ headers to ${PREFIX}/include/surreal/..."
cp "${SRC_DIR}"/surreal_*.hpp "${PREFIX}/include/surreal/"
cp "${SRC_DIR}/cgltf.h" "${PREFIX}/include/surreal/"
cp "${SRC_DIR}/stb_image.h" "${PREFIX}/include/surreal/"

# 4. Install Binaries
echo "[*] Installing binaries to ${PREFIX}/bin/..."
install -m 755 "${SRC_DIR}/surreal_live_dojo" "${PREFIX}/bin/surreal-editor"
install -m 755 "${SRC_DIR}/surreal_live_dojo" "${PREFIX}/bin/surreal-dojo"
install -m 755 "${SRC_DIR}/test_surreal_benchmark" "${PREFIX}/bin/surreal-bench"
install -m 755 "${SRC_DIR}/surreal-photo3d" "${PREFIX}/bin/surreal-photo3d"

# 5. Install PBR Assets
echo "[*] Linking PBR 3D Assets to ${PREFIX}/share/surreal/..."
AVATAR_SRC=""
if [ -d "${SRC_DIR}/../avatar" ]; then
    AVATAR_SRC="${SRC_DIR}/../avatar"
elif [ -d "/root/coding/MeinOS/avatar" ]; then
    AVATAR_SRC="/root/coding/MeinOS/avatar"
fi
if [ -n "${AVATAR_SRC}" ]; then
    rm -rf "${PREFIX}/share/surreal/avatar"
    ln -s "$(cd "${AVATAR_SRC}" && pwd)" "${PREFIX}/share/surreal/avatar"
fi

# 6. Generate pkg-config Configuration
echo "[*] Registering pkg-config specification to ${PREFIX}/lib/pkgconfig/surreal.pc..."
cat << EOF > "${PREFIX}/lib/pkgconfig/surreal.pc"
prefix=${PREFIX}
exec_prefix=\${prefix}
includedir=\${prefix}/include
libdir=\${prefix}/lib

Name: surreal
Description: Sovereign Bare-Metal & Hybrid 3D Game Engine for CosmOS & Linux
Version: 2.0.0
Cflags: -I\${includedir} -I\${includedir}/surreal -mavx2 -mfma -fopenmp -O3 -std=c++20
Libs: -lSDL2 -lGL -lm -pthread -fopenmp
EOF

export PKG_CONFIG_PATH="${PREFIX}/lib/pkgconfig:${PKG_CONFIG_PATH}"

# 7. Verification: Compile Standalone Example Game
echo "[*] Testing installation by building examples/simple_game.cpp with pkg-config..."
if [ -f "${SRC_DIR}/examples/simple_game.cpp" ]; then
    g++ -O3 -mavx2 -mfma -fopenmp -std=c++20 "${SRC_DIR}/examples/simple_game.cpp" $(pkg-config --cflags --libs surreal) -o "${SRC_DIR}/examples/simple_game"
    echo "[✓] Standalone example game compiled successfully!"
fi

echo "===================================================================="
echo "  SURREAL 3D ENGINE INSTALLATION COMPLETE!"
echo "  Run 'surreal-editor'  to launch the 3D Studio & Live Dojo"
echo "  Run 'surreal-bench'   to launch the Bare-Metal AVX2 Benchmark"
echo "  Run 'surreal-photo3d' to convert photos into 3D preview renders"
echo "===================================================================="
