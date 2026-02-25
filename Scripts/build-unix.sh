#!/bin/bash

set -euo pipefail

# Parse command line arguments
CONFIGURATION="Debug"
COMPILER="gcc"

while [[ $# -gt 0 ]]; do
    case $1 in
        --config)
            CONFIGURATION="$2"
            shift 2
            ;;
        --compiler)
            COMPILER="$2"
            shift 2
            ;;
        --help)
            echo "Usage: $0 [--config Debug|Release|Dist] [--compiler gcc|clang]"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Validate configuration
if [[ "$CONFIGURATION" != "Debug" && "$CONFIGURATION" != "Release" && "$CONFIGURATION" != "Dist" ]]; then
    echo "Error: Invalid configuration '$CONFIGURATION'. Must be Debug, Release, or Dist."
    exit 1
fi

# Validate compiler
if [[ "$COMPILER" != "gcc" && "$COMPILER" != "clang" ]]; then
    echo "Error: Invalid compiler '$COMPILER'. Must be gcc or clang."
    exit 1
fi

can_use_sudo() {
    command -v sudo >/dev/null 2>&1 && sudo -n true >/dev/null 2>&1
}

has_linkable_library() {
    local library_name="$1"
    local compiler_binary="cc"
    if command -v gcc >/dev/null 2>&1; then
        compiler_binary="gcc"
    elif command -v clang >/dev/null 2>&1; then
        compiler_binary="clang"
    fi

    local test_binary="/tmp/limitless_link_test_$$"
    if printf 'int main(void){return 0;}\n' | "$compiler_binary" -x c - -l"${library_name}" -o "$test_binary" >/dev/null 2>&1; then
        rm -f "$test_binary"
        return 0
    fi
    rm -f "$test_binary"
    return 1
}

# Function to check and install dependencies
check_dependencies() {
    echo "Checking for required dependencies..."

    local system_name
    system_name="$(uname -s | tr '[:upper:]' '[:lower:]')"

    if [[ "$system_name" == "darwin" ]]; then
        if ! command -v brew >/dev/null 2>&1; then
            echo "Error: Homebrew is required on macOS to install build dependencies."
            echo "Install it from https://brew.sh and rerun this script."
            return 1
        fi

        local missing_brew_deps=()
        local brew_deps=("pkg-config" "box2d" "sdl3")
        for dep in "${brew_deps[@]}"; do
            if ! brew list --versions "$dep" >/dev/null 2>&1; then
                missing_brew_deps+=("$dep")
            fi
        done

        if [[ ${#missing_brew_deps[@]} -gt 0 ]]; then
            echo "Installing missing Homebrew dependencies: ${missing_brew_deps[*]}"
            brew update
            brew install "${missing_brew_deps[@]}"
        fi

        local box2d_prefix
        box2d_prefix="$(brew --prefix box2d 2>/dev/null || true)"
        if [[ -z "$box2d_prefix" || ! -f "${box2d_prefix}/lib/libbox2d.dylib" ]]; then
            echo "Error: box2d library not found after Homebrew install."
            echo "Expected file: \${HOMEBREW_PREFIX}/opt/box2d/lib/libbox2d.dylib"
            return 1
        fi
        if ! pkg-config --exists sdl3 2>/dev/null; then
            echo "Error: sdl3 still not discoverable by pkg-config after Homebrew install."
            echo "Try: export PKG_CONFIG_PATH=\"$(brew --prefix)/lib/pkgconfig:${PKG_CONFIG_PATH:-}\""
            return 1
        fi
    else
        local missing_deps=()

        # Check for build tools
        if ! command -v gcc >/dev/null 2>&1 && ! command -v clang >/dev/null 2>&1; then
            missing_deps+=("build-essential")
        fi

        if ! command -v make >/dev/null 2>&1; then
            missing_deps+=("make")
        fi

        if ! command -v pkg-config >/dev/null 2>&1; then
            missing_deps+=("pkg-config")
        fi

        # Check for X11 libraries
        if ! pkg-config --exists x11 2>/dev/null; then
            missing_deps+=("libx11-dev")
        fi
        if ! pkg-config --exists xext 2>/dev/null; then
            missing_deps+=("libxext-dev")
        fi
        if ! pkg-config --exists xrandr 2>/dev/null; then
            missing_deps+=("libxrandr-dev")
        fi
        if ! pkg-config --exists xcursor 2>/dev/null; then
            missing_deps+=("libxcursor-dev")
        fi
        if ! pkg-config --exists xi 2>/dev/null; then
            missing_deps+=("libxi-dev")
        fi
        if ! pkg-config --exists xinerama 2>/dev/null; then
            missing_deps+=("libxinerama-dev")
        fi
        if ! pkg-config --exists xxf86vm 2>/dev/null; then
            missing_deps+=("libxxf86vm-dev")
        fi
        if ! pkg-config --exists xss 2>/dev/null && ! has_linkable_library "Xss"; then
            missing_deps+=("libxss-dev")
        fi

        # Check for audio and runtime libraries
        if ! pkg-config --exists alsa 2>/dev/null; then
            missing_deps+=("libasound2-dev")
        fi
        if ! pkg-config --exists dbus-1 2>/dev/null; then
            missing_deps+=("libdbus-1-dev")
        fi
        if ! pkg-config --exists libudev 2>/dev/null; then
            missing_deps+=("libudev-dev")
        fi
        if ! pkg-config --exists ibus-1.0 2>/dev/null; then
            missing_deps+=("libibus-1.0-dev")
        fi

        if [[ ${#missing_deps[@]} -gt 0 ]]; then
            echo "Missing Linux dependencies detected: ${missing_deps[*]}"

            if ! can_use_sudo; then
                echo "Warning: sudo is unavailable. Continuing without package installation."
                echo "Warning: install these packages manually if the build fails."
            elif command -v apt-get >/dev/null 2>&1; then
                echo "Installing missing Linux dependencies with apt..."
                sudo apt-get update
                sudo apt-get install -y "${missing_deps[@]}"
            elif command -v pacman >/dev/null 2>&1; then
                # Map Debian package names to Arch equivalents where needed.
                local arch_deps=("base-devel" "pkgconf" "libx11" "libxext" "libxrandr" "libxcursor" "libxi" "libxinerama" "libxxf86vm" "libxss" "alsa-lib" "dbus" "ibus" "systemd")
                sudo pacman -Syu --needed "${arch_deps[@]}"
            else
                echo "Error: Unsupported Linux package manager. Install dependencies manually and retry."
                return 1
            fi
        fi

        if ! install_box2d_v3_linux; then
            return 1
        fi

        # Check for SDL3 and install from package manager / source as fallback
        if ! pkg-config --exists sdl3 2>/dev/null; then
            echo "SDL3 not found. Attempting package manager install first..."
            if can_use_sudo && command -v apt-get >/dev/null 2>&1 && sudo apt-get install -y libsdl3-dev 2>/dev/null; then
                echo "SDL3 installed from apt."
            elif can_use_sudo && command -v pacman >/dev/null 2>&1 && sudo pacman -S --needed sdl3 >/dev/null 2>&1; then
                echo "SDL3 installed from pacman."
            else
                echo "SDL3 package unavailable. Building SDL3 from source..."
                local temp_dir="/tmp/sdl3_build_$$"
                mkdir -p "$temp_dir"
                pushd "$temp_dir" >/dev/null

                if ! command -v git >/dev/null 2>&1; then
                    if can_use_sudo && command -v apt-get >/dev/null 2>&1; then
                        sudo apt-get install -y git
                    elif can_use_sudo && command -v pacman >/dev/null 2>&1; then
                        sudo pacman -S --needed git
                    else
                        echo "Error: git is required to build SDL3 from source."
                        popd >/dev/null
                        rm -rf "$temp_dir"
                        return 1
                    fi
                fi

                if ! command -v cmake >/dev/null 2>&1; then
                    if can_use_sudo && command -v apt-get >/dev/null 2>&1; then
                        sudo apt-get install -y cmake
                    elif can_use_sudo && command -v pacman >/dev/null 2>&1; then
                        sudo pacman -S --needed cmake
                    else
                        echo "Error: cmake is required to build SDL3 from source."
                        popd >/dev/null
                        rm -rf "$temp_dir"
                        return 1
                    fi
                fi

                git clone https://github.com/libsdl-org/SDL.git
                cd SDL
                git checkout release-3.2.18
                mkdir build && cd build
                cmake .. -DCMAKE_BUILD_TYPE=Release -DSDL_STATIC=OFF -DSDL_SHARED=ON -DSDL_TEST=OFF -DSDL_OPENGL=ON -DSDL_OPENGLES=ON
                make -j"$(get_job_count)"
                if ! can_use_sudo; then
                    echo "Error: sudo is required to install SDL3 system-wide."
                    popd >/dev/null
                    rm -rf "$temp_dir"
                    return 1
                fi
                sudo make install
                if command -v ldconfig >/dev/null 2>&1; then
                    sudo ldconfig
                fi

                popd >/dev/null
                rm -rf "$temp_dir"
                echo "SDL3 built and installed successfully from source."
            fi
        fi
    fi

    echo "All required dependencies are installed."
    return 0
}

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
    if [[ -n "${WSL_INTEROP:-}" && "$(pwd)" == /mnt/* && "$jobs" -gt 8 ]]; then
        jobs=8
    fi

    echo "$jobs"
}

has_box2d_v3_linux() {
    local local_lib_dir
    local local_static_lib
    local local_shared_lib
    local_lib_dir="$(pwd)/Limitless/Vendor/box2d/libs/linux"
    local_static_lib="${local_lib_dir}/libbox2d.a"
    local_shared_lib="${local_lib_dir}/libbox2d.so"

    if [[ -f "$local_static_lib" ]] && nm "$local_static_lib" 2>/dev/null | grep "b2World_IsValid" >/dev/null; then
        return 0
    fi

    if [[ -f "$local_shared_lib" ]] && nm -D "$local_shared_lib" 2>/dev/null | grep "b2World_IsValid" >/dev/null; then
        return 0
    fi

    if command -v ldconfig >/dev/null 2>&1; then
        local installed_path
        installed_path="$(ldconfig -p 2>/dev/null | awk '/libbox2d\.so/{print $NF; exit}')"
        if [[ -n "${installed_path:-}" ]] && nm -D "$installed_path" 2>/dev/null | grep "b2World_IsValid" >/dev/null; then
            return 0
        fi
    fi

    if [[ -f "/usr/local/lib/libbox2d.so" ]] && nm -D "/usr/local/lib/libbox2d.so" 2>/dev/null | grep "b2World_IsValid" >/dev/null; then
        return 0
    fi

    return 1
}

install_box2d_v3_linux() {
    if has_box2d_v3_linux; then
        return 0
    fi

    echo "Box2D 3.x not found (missing b2World_IsValid). Building from source..."

    local temp_dir="/tmp/box2d_build_$$"
    local local_lib_dir
    local_lib_dir="$(pwd)/Limitless/Vendor/box2d/libs/linux"
    mkdir -p "$temp_dir"
    pushd "$temp_dir" >/dev/null

    if ! command -v git >/dev/null 2>&1; then
        if can_use_sudo && command -v apt-get >/dev/null 2>&1; then
            sudo apt-get install -y git
        elif can_use_sudo && command -v pacman >/dev/null 2>&1; then
            sudo pacman -S --needed git
        else
            echo "Error: git is required to install Box2D from source."
            popd >/dev/null
            rm -rf "$temp_dir"
            return 1
        fi
    fi

    if ! command -v cmake >/dev/null 2>&1; then
        if can_use_sudo && command -v apt-get >/dev/null 2>&1; then
            sudo apt-get install -y cmake
        elif can_use_sudo && command -v pacman >/dev/null 2>&1; then
            sudo pacman -S --needed cmake
        else
            echo "Error: cmake is required to install Box2D from source."
            popd >/dev/null
            rm -rf "$temp_dir"
            return 1
        fi
    fi

    git clone --depth 1 --branch v3.1.1 https://github.com/erincatto/box2d.git
    cmake -S box2d -B box2d/build -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DBOX2D_UNIT_TESTS=OFF -DBOX2D_SAMPLES=OFF
    cmake --build box2d/build --parallel "$(get_job_count)"

    mkdir -p "$local_lib_dir"
    if [[ ! -f "box2d/build/src/libbox2d.a" ]]; then
        echo "Error: Box2D static library output was not found."
        popd >/dev/null
        rm -rf "$temp_dir"
        return 1
    fi
    cp "box2d/build/src/libbox2d.a" "${local_lib_dir}/libbox2d.a"

    popd >/dev/null
    rm -rf "$temp_dir"

    if ! has_box2d_v3_linux; then
        echo "Error: Box2D 3.x installation verification failed."
        return 1
    fi

    echo "Box2D 3.x built successfully at ${local_lib_dir}/libbox2d.a."
    return 0
}

# Function to download and setup premake5
setup_premake() {
    local premake_dir="Vendor/Premake"
    local premake_path="$premake_dir/premake5"
    
    # Check if premake5 already exists and is executable
    if [[ -f "$premake_path" && -x "$premake_path" ]]; then
        echo "Premake5 found at $premake_path"
        return 0
    fi
    
    echo "Premake5 not found. Downloading..."
    
    # Create directory if it doesn't exist
    mkdir -p "$premake_dir"
    
    # Download premake5 (keep in sync with Windows bootstrap + CI)
    local premake_version="5.0.0-beta2"
    local system_name="$(uname -s | tr '[:upper:]' '[:lower:]')"
    local premake_platform="linux"
    if [[ "$system_name" == "darwin" ]]; then
        premake_platform="macosx"
    fi

    local premake_url="https://github.com/premake/premake-core/releases/download/v${premake_version}/premake-${premake_version}-${premake_platform}.tar.gz"
    local temp_file="premake5.tar.gz"
    
    echo "Downloading premake5 from $premake_url..."
    
    # Try curl first, then wget as fallback
    if command -v curl >/dev/null 2>&1; then
        if ! curl -L -o "$temp_file" "$premake_url"; then
            echo "Error: Failed to download premake5 with curl"
            return 1
        fi
    elif command -v wget >/dev/null 2>&1; then
        if ! wget -O "$temp_file" "$premake_url"; then
            echo "Error: Failed to download premake5 with wget"
            return 1
        fi
    else
        echo "Error: Neither curl nor wget is installed."
        echo "Please install one of them:"
        echo "  Ubuntu/Debian: sudo apt-get install curl"
        echo "  Ubuntu/Debian: sudo apt-get install wget"
        echo "  CentOS/RHEL: sudo yum install curl"
        echo "  CentOS/RHEL: sudo yum install wget"
        return 1
    fi
    
    # Extract premake5
    echo "Extracting premake5..."
    if ! tar --no-same-owner -xzf "$temp_file" -C "$premake_dir"; then
        echo "Error: Failed to extract premake5"
        rm -f "$temp_file"
        return 1
    fi
    
    # Make it executable
    chmod +x "$premake_path"
    
    # Clean up
    rm -f "$temp_file"
    
    echo "Premake5 downloaded and setup successfully"
    echo "Premake5 version:"
    "$premake_path" --version
}

echo "Building LimitlessRemaster in $CONFIGURATION configuration with $COMPILER..."

# Change to the project root directory
cd "$(dirname "$0")/.."

# Check dependencies
if ! check_dependencies; then
    echo "Error: Missing required dependencies"
    exit 1
fi

# Setup premake5 if needed
if ! setup_premake; then
    echo "Error: Failed to setup premake5"
    exit 1
fi

# Generate Makefiles
echo "Generating Makefiles..."
if [[ "$COMPILER" == "clang" ]]; then
    Vendor/Premake/premake5 gmake2 --cc=clang
else
    Vendor/Premake/premake5 gmake2 --cc=gcc
fi

if [[ $? -ne 0 ]]; then
    echo "Error: Failed to generate Makefiles"
    exit 1
fi

# Build the project
echo "Building project..."
SYSTEM_NAME="$(uname -s | tr '[:upper:]' '[:lower:]')"
if [[ "$SYSTEM_NAME" == "darwin" ]]; then
    SYSTEM_NAME="macosx"
fi

ARCH_NAME="$(uname -m | tr '[:upper:]' '[:lower:]')"
PLATFORM_NAME="x64"
MAKE_PLATFORM_NAME="x64"
if [[ "$ARCH_NAME" == "aarch64" || "$ARCH_NAME" == "arm64" ]]; then
    PLATFORM_NAME="ARM64"
    MAKE_PLATFORM_NAME="arm64"
fi

CONFIG_LOWER="${CONFIGURATION,,}"
CFG_SHORTNAME="${CONFIG_LOWER}_${MAKE_PLATFORM_NAME}"

make -j"$(get_job_count)" config="${CFG_SHORTNAME}"
if [[ $? -ne 0 ]]; then
    echo "Error: Build failed"
    exit 1
fi

echo "Build completed successfully!"
OUTPUT_DIR="Build/${CFG_SHORTNAME}-${SYSTEM_NAME}-${PLATFORM_NAME}"
echo "Output directory: ${OUTPUT_DIR}/"

# Run tests if they exist
if [[ -f "${OUTPUT_DIR}/Test/Test" ]]; then
    echo "Running tests..."
    "./${OUTPUT_DIR}/Test/Test" --success
    if [[ $? -ne 0 ]]; then
        echo "Warning: Some tests failed"
    else
        echo "All tests passed!"
    fi
fi 