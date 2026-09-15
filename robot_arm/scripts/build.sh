#!/usr/bin/env bash

set -Eeuo pipefail

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly PROJECT_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
readonly BUILD_DIR="${PROJECT_ROOT}/build"
readonly BUILD_TYPE="${BUILD_TYPE:-Debug}"

if ! command -v cmake >/dev/null 2>&1; then
    echo "Error: cmake is not installed." >&2
    echo "Run ${PROJECT_ROOT}/scripts/setup_ubuntu.sh first." >&2
    exit 1
fi

if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    if ! command -v ninja >/dev/null 2>&1; then
        echo "Error: ninja is not installed." >&2
        echo "Run ${PROJECT_ROOT}/scripts/setup_ubuntu.sh first." >&2
        exit 1
    fi

    echo "Configuring ${BUILD_TYPE} build..."
    cmake \
        -S "${PROJECT_ROOT}" \
        -B "${BUILD_DIR}" \
        -G Ninja \
        -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
fi

echo "Building project..."
cmake --build "${BUILD_DIR}" --parallel

echo
echo "Build complete: ${BUILD_DIR}/robot_arm_2d_control"
