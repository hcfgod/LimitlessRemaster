#!/usr/bin/env bash

set -euo pipefail

CONFIGURATION="Debug"
PLATFORM=""
COMPILER=""

print_usage() {
    echo "Usage: $0 [--config Debug|Release|Dist] [--platform x64|ARM64] [--compiler gcc|clang]"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --config)
            CONFIGURATION="${2:-}"
            shift 2
            ;;
        --platform)
            PLATFORM="${2:-}"
            shift 2
            ;;
        --compiler)
            COMPILER="${2:-}"
            shift 2
            ;;
        --help|-h)
            print_usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            print_usage
            exit 1
            ;;
    esac
done

if [[ "$CONFIGURATION" != "Debug" && "$CONFIGURATION" != "Release" && "$CONFIGURATION" != "Dist" ]]; then
    echo "Error: Invalid configuration '$CONFIGURATION'. Expected Debug, Release, or Dist."
    exit 1
fi

ARCH_NAME="$(uname -m | tr '[:upper:]' '[:lower:]')"
if [[ -z "$PLATFORM" ]]; then
    if [[ "$ARCH_NAME" == "aarch64" || "$ARCH_NAME" == "arm64" ]]; then
        PLATFORM="ARM64"
    else
        PLATFORM="x64"
    fi
fi

if [[ "$PLATFORM" != "x64" && "$PLATFORM" != "ARM64" ]]; then
    echo "Error: Invalid platform '$PLATFORM'. Expected x64 or ARM64."
    exit 1
fi

SYSTEM_NAME="$(uname -s | tr '[:upper:]' '[:lower:]')"
if [[ "$SYSTEM_NAME" == "darwin" ]]; then
    SYSTEM_NAME="macosx"
fi

if [[ -z "$COMPILER" ]]; then
    if [[ "$SYSTEM_NAME" == "macosx" ]]; then
        COMPILER="clang"
    else
        COMPILER="gcc"
    fi
fi

if [[ "$COMPILER" != "gcc" && "$COMPILER" != "clang" ]]; then
    echo "Error: Invalid compiler '$COMPILER'. Expected gcc or clang."
    exit 1
fi

SCRIPT_DIRECTORY="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIRECTORY/.." && pwd)"
cd "$PROJECT_ROOT"

setup_premake() {
    local premake_dir="Vendor/Premake"
    local premake_path="$premake_dir/premake5"

    if [[ -f "$premake_path" && -x "$premake_path" ]]; then
        return 0
    fi

    echo "Premake5 not found. Downloading..."
    mkdir -p "$premake_dir"

    local premake_version="5.0.0-beta2"
    local premake_platform="linux"
    if [[ "$SYSTEM_NAME" == "macosx" ]]; then
        premake_platform="macosx"
    fi

    local premake_url="https://github.com/premake/premake-core/releases/download/v${premake_version}/premake-${premake_version}-${premake_platform}.tar.gz"
    local temp_file
    temp_file="$(mktemp)"

    if command -v curl >/dev/null 2>&1; then
        if ! curl -L -o "$temp_file" "$premake_url"; then
            echo "Error: Failed to download Premake5 with curl."
            rm -f "$temp_file"
            return 1
        fi
    elif command -v wget >/dev/null 2>&1; then
        if ! wget -O "$temp_file" "$premake_url"; then
            echo "Error: Failed to download Premake5 with wget."
            rm -f "$temp_file"
            return 1
        fi
    else
        echo "Error: Neither curl nor wget is installed."
        rm -f "$temp_file"
        return 1
    fi

    if ! tar --no-same-owner -xzf "$temp_file" -C "$premake_dir"; then
        echo "Error: Failed to extract Premake5 archive."
        rm -f "$temp_file"
        return 1
    fi
    chmod +x "$premake_path"
    rm -f "$temp_file"
}

if ! setup_premake; then
    echo "Error: Failed to setup Premake5."
    exit 1
fi

echo "Generating makefiles for ScriptCore build..."
if [[ "$COMPILER" == "clang" ]]; then
    Vendor/Premake/premake5 gmake2 --cc=clang
else
    Vendor/Premake/premake5 gmake2 --cc=gcc
fi

CONFIG_LOWER="$(echo "$CONFIGURATION" | tr '[:upper:]' '[:lower:]')"
PLATFORM_LOWER="$(echo "$PLATFORM" | tr '[:upper:]' '[:lower:]')"
CFG_SHORTNAME="${CONFIG_LOWER}_${PLATFORM_LOWER}"

get_job_count() {
    if command -v nproc >/dev/null 2>&1; then
        nproc
        return
    fi
    if command -v sysctl >/dev/null 2>&1; then
        sysctl -n hw.logicalcpu
        return
    fi
    echo "4"
}

JOBS="$(get_job_count)"
echo "Building ScriptCore only: config=${CFG_SHORTNAME}, compiler=${COMPILER}, jobs=${JOBS}"
make -j"${JOBS}" ScriptCore config="${CFG_SHORTNAME}"

echo "ScriptCore build completed successfully."
echo "Output directory: Build/${CFG_SHORTNAME}-${SYSTEM_NAME}-${PLATFORM}/Editor/"
