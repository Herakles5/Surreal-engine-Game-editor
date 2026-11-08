#!/bin/bash
# Builds surreal-boost-layer_<version>_amd64.deb  (Vulkan implicit layer + CLI launcher + default config)
set -euo pipefail
cd "$(dirname "$0")"
VERSION="${1:-1.1.0}"
PKG="surreal-boost-layer_${VERSION}_amd64"
OUT="build/${PKG}"

make clean >/dev/null && make >/dev/null
rm -rf build && mkdir -p "${OUT}/DEBIAN"
make install DESTDIR="$PWD/${OUT}" PREFIX=/usr >/dev/null

cat > "${OUT}/DEBIAN/control" <<EOF
Package: surreal-boost-layer
Version: ${VERSION}
Section: graphics
Priority: optional
Architecture: amd64
Depends: libvulkan1, libc6 (>= 2.38)
Maintainer: AION <154477518+Herakles5@users.noreply.github.com>
Description: AION Surreal Boost - Vulkan Upscaler, CAS & FPS Accelerator for Linux/Steam
 Implicit Vulkan layer (VK_LAYER_SURREAL_boost) and launcher (surreal-boost)
 for native Vulkan games and Steam Proton (DXVK / VKD3D-Proton). Provides a
 GPU Spatial Render-Scale Upscaler with Contrast Adaptive Sharpening (CAS),
 automatic anisotropic filtering boost, texture LOD bias control, sub-ms hybrid
 frame pacing, Pascal DX11/DX12 pipeline tuning, and 1%-low FPS telemetry.
 Only active when SURREAL_BOOST=1 is set (e.g. "surreal-boost %command%").
EOF
echo "/etc/surreal-boost/layer.conf" > "${OUT}/DEBIAN/conffiles"

dpkg-deb --build --root-owner-group "${OUT}" "build/${PKG}.deb" >/dev/null
echo "built: $PWD/build/${PKG}.deb"
dpkg-deb -I "build/${PKG}.deb" | sed -n '1,14p'
dpkg-deb -c "build/${PKG}.deb"
