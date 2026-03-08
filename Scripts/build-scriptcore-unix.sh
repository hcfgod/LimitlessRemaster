#!/usr/bin/env bash

set -euo pipefail

CONFIGURATION="Debug"
PLATFORM=""
COMPILER=""
OPEN_PROJECT_ROOT=""

print_usage() {
    echo "Usage: $0 [--config Debug|Release|Dist] [--platform x64|ARM64] [--compiler gcc|clang] [--project-root /path/to/project]"
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
        --project-root)
            OPEN_PROJECT_ROOT="${2:-}"
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
REPO_ROOT="$(cd "$SCRIPT_DIRECTORY/.." && pwd)"
cd "$REPO_ROOT"

if [[ -n "$OPEN_PROJECT_ROOT" ]]; then
    OPEN_PROJECT_ROOT="$(cd "$OPEN_PROJECT_ROOT" && pwd)"
fi

can_use_sudo() {
    if [[ "${EUID:-$(id -u)}" -eq 0 ]]; then
        return 0
    fi
    command -v sudo >/dev/null 2>&1 && sudo -n true >/dev/null 2>&1
}

run_with_privileges() {
    if [[ "${EUID:-$(id -u)}" -eq 0 ]]; then
        "$@"
        return $?
    fi
    sudo "$@"
}

ensure_linux_build_tools() {
    local missing_tools=()
    if ! command -v make >/dev/null 2>&1; then
        missing_tools+=("make")
    fi

    if [[ "$COMPILER" == "clang" ]]; then
        if ! command -v clang >/dev/null 2>&1; then
            missing_tools+=("clang")
        fi
    else
        if ! command -v gcc >/dev/null 2>&1; then
            missing_tools+=("gcc")
        fi
        if ! command -v g++ >/dev/null 2>&1; then
            missing_tools+=("g++")
        fi
    fi

    if [[ ${#missing_tools[@]} -eq 0 ]]; then
        return 0
    fi

    echo "Missing Linux build tools: ${missing_tools[*]}"
    if command -v apt-get >/dev/null 2>&1 && can_use_sudo; then
        echo "Installing missing tools with apt..."
        run_with_privileges apt-get update
        run_with_privileges apt-get install -y "${missing_tools[@]}"
        return 0
    fi

    if command -v pacman >/dev/null 2>&1 && can_use_sudo; then
        echo "Installing missing tools with pacman..."
        local pacman_packages=("make")
        if [[ "$COMPILER" == "clang" ]]; then
            pacman_packages+=("clang")
        else
            pacman_packages+=("gcc")
        fi
        run_with_privileges pacman -Syu --needed "${pacman_packages[@]}"
        return 0
    fi

    echo "Error: Build tools are missing and auto-install is unavailable."
    echo "Install required tools manually and retry."
    if command -v apt-get >/dev/null 2>&1; then
        echo "Example (Ubuntu/Debian): sudo apt-get update && sudo apt-get install -y build-essential"
        if [[ "$COMPILER" == "clang" ]]; then
            echo "If using clang: sudo apt-get install -y clang"
        fi
    elif command -v pacman >/dev/null 2>&1; then
        echo "Example (Arch): sudo pacman -Syu --needed base-devel"
        if [[ "$COMPILER" == "clang" ]]; then
            echo "If using clang: sudo pacman -S --needed clang"
        fi
    fi

    return 1
}

if [[ "$SYSTEM_NAME" != "macosx" ]]; then
    if ! ensure_linux_build_tools; then
        exit 1
    fi
fi

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
BUILD_FOLDER="${CFG_SHORTNAME}-${SYSTEM_NAME}-${PLATFORM}"
OUTPUT_DIR="$REPO_ROOT/Build/$BUILD_FOLDER/Editor"
RUNTIME_TEMPLATE_DIR="$REPO_ROOT/RuntimeTemplates/$BUILD_FOLDER"
MANAGED_BUILD_SCRIPT="$REPO_ROOT/Scripts/build-managed-runtime-unix.sh"
PROJECT_LOCAL_OUTPUT_DIR=""
if [[ -n "$OPEN_PROJECT_ROOT" ]]; then
    PROJECT_LOCAL_OUTPUT_DIR="$OPEN_PROJECT_ROOT/Build/ScriptCore/$BUILD_FOLDER"
fi

get_job_count() {
    local jobs
    if command -v nproc >/dev/null 2>&1; then
        jobs="$(nproc)"
    elif command -v sysctl >/dev/null 2>&1; then
        jobs="$(sysctl -n hw.logicalcpu)"
    else
        jobs="4"
    fi

    # WSL builds against /mnt/<drive> are significantly slower with very high
    # parallelism and may appear "stuck" due long compiler stalls.
    if [[ -n "${WSL_INTEROP:-}" && "$PROJECT_ROOT" == /mnt/* && "$jobs" -gt 8 ]]; then
        jobs=8
    fi

    echo "$jobs"
}

JOBS="$(get_job_count)"
echo "Building ScriptCore only: config=${CFG_SHORTNAME}, compiler=${COMPILER}, jobs=${JOBS}"
make -j"${JOBS}" ScriptCore config="${CFG_SHORTNAME}"

if [[ ! -f "$MANAGED_BUILD_SCRIPT" ]]; then
    echo "Error: Managed runtime build script not found: $MANAGED_BUILD_SCRIPT"
    exit 1
fi

if [[ -n "$OPEN_PROJECT_ROOT" ]]; then
    bash "$MANAGED_BUILD_SCRIPT" --config "$CONFIGURATION" --platform "$PLATFORM" --output-dir "$OUTPUT_DIR" --project-root "$OPEN_PROJECT_ROOT"
else
    bash "$MANAGED_BUILD_SCRIPT" --config "$CONFIGURATION" --platform "$PLATFORM" --output-dir "$OUTPUT_DIR"
fi

if [[ -n "$PROJECT_LOCAL_OUTPUT_DIR" ]]; then
    bash "$MANAGED_BUILD_SCRIPT" --config "$CONFIGURATION" --platform "$PLATFORM" --output-dir "$PROJECT_LOCAL_OUTPUT_DIR" --project-root "$OPEN_PROJECT_ROOT"
fi

if [[ -n "$OPEN_PROJECT_ROOT" ]]; then
    bash "$MANAGED_BUILD_SCRIPT" --config "$CONFIGURATION" --platform "$PLATFORM" --output-dir "$RUNTIME_TEMPLATE_DIR" --project-root "$OPEN_PROJECT_ROOT"
else
    bash "$MANAGED_BUILD_SCRIPT" --config "$CONFIGURATION" --platform "$PLATFORM" --output-dir "$RUNTIME_TEMPLATE_DIR"
fi

echo "ScriptCore build completed successfully."
echo "Output directory: Build/${CFG_SHORTNAME}-${SYSTEM_NAME}-${PLATFORM}/Editor/"
