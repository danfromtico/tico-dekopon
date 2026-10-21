#!/usr/bin/env bash
# Builds tico-dekopon.nro in the switch-dev image and packs the tico module: a
# directory that extracts to sdmc:/tico/modules/dekopon/, where tico reads
# module.json, the settings definition and the strings, and launches the NRO
# beside them.
#
#   ./build_tico_module.sh          configure if needed, build and pack
#   ./build_tico_module.sh clean    from an empty build directory
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE="${SWITCH_DEV_IMAGE:-ghcr.io/autorunhq/switch-dev:2026.10.12}"
BUILD_DIR="build/switch"

if [ "${1:-}" = "clean" ]; then
    rm -rf "${ROOT:?}/${BUILD_DIR}"
fi

# the settings definition and strings, from tico/tools/tico_module.py
python3 -I "${ROOT}/tico/tools/tico_module.py"

docker run --rm -v "${ROOT}:/work" "${IMAGE}" bash -c "
    set -e
    git config --global --add safe.directory '*'
    if [ ! -f ${BUILD_DIR}/CMakeCache.txt ]; then
        cmake -S . -B ${BUILD_DIR} -G Ninja \
            -DCMAKE_TOOLCHAIN_FILE=\$DEVKITPRO/cmake/Switch.cmake \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_POLICY_VERSION_MINIMUM=3.5
    fi
    cmake --build ${BUILD_DIR} --target tico_nro -j\$(nproc)
"

NRO="${ROOT}/${BUILD_DIR}/src/tico/tico-dekopon.nro"
MODULE_SRC="${ROOT}/tico/module"
MODULE_ID=$(python3 -I -c 'import json,sys; print(json.load(open(sys.argv[1]))["id"])' \
    "${MODULE_SRC}/module.json")
PACKAGE_DIR="${ROOT}/build_tico"
MODULE_OUT="${PACKAGE_DIR}/module/${MODULE_ID}"

rm -rf "${PACKAGE_DIR}"
mkdir -p "${MODULE_OUT}"
cp -R "${MODULE_SRC}/." "${MODULE_OUT}/"
cp "${NRO}" "${MODULE_OUT}/"
cp -R "${ROOT}/tico/lang" "${MODULE_OUT}/"
BUNDLE="${PACKAGE_DIR}/tico-${MODULE_ID}-module.zip"
( cd "${PACKAGE_DIR}/module" && zip -qr "${BUNDLE}" "${MODULE_ID}" )

echo "NRO:    ${NRO}"
echo "Module: ${BUNDLE} (extracts to sdmc:/tico/modules/${MODULE_ID}/)"
