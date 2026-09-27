#!/usr/bin/env bash

set -Eeuo pipefail

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly PROJECT_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
readonly BUILD_DIR="${PROJECT_ROOT}/build"

if [[ ! -r /etc/os-release ]]; then
    echo "Error: /etc/os-release was not found. This script requires Ubuntu." >&2
    exit 1
fi

# shellcheck disable=SC1091
source /etc/os-release

if [[ "${ID:-}" != "ubuntu" ]]; then
    echo "Error: unsupported Linux distribution '${PRETTY_NAME:-unknown}'." >&2
    echo "This setup script is intended for Ubuntu or WSL Ubuntu." >&2
    exit 1
fi

if (( EUID == 0 )); then
    SUDO=()
elif command -v sudo >/dev/null 2>&1; then
    SUDO=(sudo)
else
    echo "Error: sudo is required to install development packages." >&2
    exit 1
fi

echo "[1/3] Updating the Ubuntu package list..."
"${SUDO[@]}" apt-get update

echo "[2/3] Installing C development tools..."
"${SUDO[@]}" apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    gdb \
    clang-format \
    cppcheck

echo "[3/3] Configuring the project..."
cmake \
    -S "${PROJECT_ROOT}" \
    -B "${BUILD_DIR}" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug

echo
echo "Setup complete. Build the project with:"
echo "  ${PROJECT_ROOT}/scripts/build.sh"
