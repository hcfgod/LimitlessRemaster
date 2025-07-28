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

echo "Building LimitlessRemaster in $CONFIGURATION configuration with $COMPILER..."

# Change to the project root directory
cd "$(dirname "$0")/.."

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
make -j$(nproc) config="${CONFIGURATION}_x64"
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