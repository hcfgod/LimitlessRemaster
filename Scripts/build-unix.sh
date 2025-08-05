#!/bin/bash

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

# Function to check and install dependencies
check_dependencies() {
    echo "Checking for required dependencies..."
    
    local missing_deps=()
    
    # Check for build tools
    if ! command -v gcc >/dev/null 2>&1; then
        missing_deps+=("build-essential")
    fi
    
    if ! command -v make >/dev/null 2>&1; then
        missing_deps+=("make")
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
    
    if ! pkg-config --exists xss 2>/dev/null; then
        missing_deps+=("libxss-dev")
    fi
    
    # Check for audio libraries
    if ! pkg-config --exists alsa 2>/dev/null; then
        missing_deps+=("libasound2-dev")
    fi
    
    # Check for other system libraries
    if ! pkg-config --exists dbus-1 2>/dev/null; then
        missing_deps+=("libdbus-1-dev")
    fi
    
    if ! pkg-config --exists libudev 2>/dev/null; then
        missing_deps+=("libudev-dev")
    fi
    
    if ! pkg-config --exists ibus-1.0 2>/dev/null; then
        missing_deps+=("libibus-1.0-dev")
    fi
    
    # Install missing system dependencies
    if [[ ${#missing_deps[@]} -gt 0 ]]; then
        echo "Installing missing system dependencies..."
        echo "Missing: ${missing_deps[*]}"
        
        if ! sudo apt-get update; then
            echo "Error: Failed to update package list"
            return 1
        fi
        
        if ! sudo apt-get install -y "${missing_deps[@]}"; then
            echo "Error: Failed to install system dependencies"
            return 1
        fi
        
        echo "System dependencies installed successfully."
    fi
    
    # Check for SDL3 and install from source if needed
    if ! pkg-config --exists sdl3 2>/dev/null; then
        echo "SDL3 not found. Attempting to install from package manager first..."
        
        if sudo apt-get install -y libsdl3-dev 2>/dev/null; then
            echo "SDL3 installed from package manager"
        else
            echo "SDL3 not available in package manager. Building from source..."
            
            # Check if we have the required tools for building SDL3
            if ! command -v git >/dev/null 2>&1; then
                echo "Installing git..."
                sudo apt-get install -y git
            fi
            
            if ! command -v cmake >/dev/null 2>&1; then
                echo "Installing cmake..."
                sudo apt-get install -y cmake
            fi
            
            # Build SDL3 from source
            local temp_dir="/tmp/sdl3_build_$$"
            mkdir -p "$temp_dir"
            cd "$temp_dir"
            
            echo "Cloning SDL repository..."
            if ! git clone https://github.com/libsdl-org/SDL.git; then
                echo "Error: Failed to clone SDL repository"
                cd "$(dirname "$0")/.."
                rm -rf "$temp_dir"
                return 1
            fi
            
            cd SDL
            echo "Checking out SDL release-3.2.18..."
            if ! git checkout release-3.2.18; then
                echo "Error: Failed to checkout SDL release"
                cd "$(dirname "$0")/.."
                rm -rf "$temp_dir"
                return 1
            fi
            
            mkdir build && cd build
            echo "Configuring SDL3 build..."
            if ! cmake .. -DCMAKE_BUILD_TYPE=Release -DSDL_STATIC=OFF -DSDL_SHARED=ON -DSDL_TEST=OFF -DSDL_OPENGL=ON -DSDL_OPENGLES=ON; then
                echo "Error: Failed to configure SDL3 build"
                cd "$(dirname "$0")/.."
                rm -rf "$temp_dir"
                return 1
            fi
            
            echo "Building SDL3 (this may take a few minutes)..."
            if ! make -j$(nproc); then
                echo "Error: Failed to build SDL3"
                cd "$(dirname "$0")/.."
                rm -rf "$temp_dir"
                return 1
            fi
            
            echo "Installing SDL3..."
            if ! sudo make install; then
                echo "Error: Failed to install SDL3"
                cd "$(dirname "$0")/.."
                rm -rf "$temp_dir"
                return 1
            fi
            
            sudo ldconfig
            
            # Clean up
            cd "$(dirname "$0")/.."
            rm -rf "$temp_dir"
            
            echo "SDL3 built and installed successfully from source"
        fi
    fi
    
    echo "All required dependencies are installed."
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
    
    # Download premake5 for Linux
    local premake_url="https://github.com/premake/premake-core/releases/download/v5.0.0-alpha16/premake-5.0.0-alpha16-linux.tar.gz"
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
    if ! tar -xzf "$temp_file" -C "$premake_dir"; then
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
make -j$(nproc) config="${CONFIGURATION,,}_x64"
if [[ $? -ne 0 ]]; then
    echo "Error: Build failed"
    exit 1
fi

echo "Build completed successfully!"
echo "Output directory: Build/${CONFIGURATION}_x64/"

# Run tests if they exist
if [[ -f "Build/${CONFIGURATION}_x64/Test/Test" ]]; then
    echo "Running tests..."
    ./Build/${CONFIGURATION}_x64/Test/Test --success
    if [[ $? -ne 0 ]]; then
        echo "Warning: Some tests failed"
    else
        echo "All tests passed!"
    fi
fi 