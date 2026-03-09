#!/usr/bin/env bash

set -euo pipefail

CONFIGURATION="Debug"
PLATFORM="x64"
OUTPUT_DIR=""
PROJECT_ROOT=""

print_usage() {
    echo "Usage: $0 --config [Debug|Release|Dist] --platform [x64|ARM64] --output-dir /path/to/output [--project-root /path/to/project]"
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
        --output-dir)
            OUTPUT_DIR="${2:-}"
            shift 2
            ;;
        --project-root)
            PROJECT_ROOT="${2:-}"
            shift 2
            ;;
        --help|-h)
            print_usage
            exit 0
            ;;
        *)
            echo "Error: Unknown option '$1'."
            print_usage
            exit 1
            ;;
    esac
done

if [[ -z "$OUTPUT_DIR" ]]; then
    echo "Error: Missing output directory argument."
    print_usage
    exit 1
fi

mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(cd "$OUTPUT_DIR" && pwd)"

if [[ "$CONFIGURATION" != "Debug" && "$CONFIGURATION" != "Release" && "$CONFIGURATION" != "Dist" ]]; then
    echo "Error: Invalid configuration '$CONFIGURATION'. Expected Debug, Release, or Dist."
    exit 1
fi

DOTNET_CONFIGURATION="$CONFIGURATION"
if [[ "$DOTNET_CONFIGURATION" == "Dist" ]]; then
    DOTNET_CONFIGURATION="Release"
fi

if ! command -v dotnet >/dev/null 2>&1; then
    echo "Error: dotnet SDK was not found on PATH."
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
MANAGED_OUTPUT_DIR="$OUTPUT_DIR/Managed"
MANAGED_MANIFEST_PATH="$MANAGED_OUTPUT_DIR/Limitless.Managed.payload.json"
MANAGED_PROJECT_GENERATOR_SCRIPT="$REPO_ROOT/Scripts/generate-managed-project-csproj-unix.sh"
MANAGED_LOCK_ROOT="$REPO_ROOT/Build/ManagedRuntimeLocks"
MANAGED_LOCK_DIR="$MANAGED_LOCK_ROOT/${DOTNET_CONFIGURATION}-${PLATFORM}"
MANAGED_PROJECT_CSPROJ=""
MANAGED_PROJECT_ASSEMBLY_FILE=""
SCRIPT_ASSEMBLIES_JSON='["Limitless.Managed.TestScripts.dll"]'
mkdir -p "$MANAGED_OUTPUT_DIR"
if [[ "${LT_SKIP_MANAGED_RUNTIME_BUILD:-0}" == "1" ]]; then
    echo "Skipping managed scripting artifact build because LT_SKIP_MANAGED_RUNTIME_BUILD=1."
    exit 0
fi
mkdir -p "$MANAGED_LOCK_ROOT"

while ! mkdir "$MANAGED_LOCK_DIR" 2>/dev/null; do
    sleep 1
done

cleanup() {
    rm -rf "$MANAGED_LOCK_DIR"
}

trap cleanup EXIT

echo "Building managed scripting artifacts for $CONFIGURATION $PLATFORM..."
dotnet publish "$REPO_ROOT/Limitless/Vendor/Coral/Coral.Managed/Coral.Managed-Static.csproj" -c "$DOTNET_CONFIGURATION" -o "$MANAGED_OUTPUT_DIR" /nologo /verbosity:minimal
dotnet build "$REPO_ROOT/Managed/Limitless.Managed/Limitless.Managed.csproj" -c "$DOTNET_CONFIGURATION" -o "$MANAGED_OUTPUT_DIR" /nologo /verbosity:minimal
dotnet build "$REPO_ROOT/Managed/Limitless.Managed.TestScripts/Limitless.Managed.TestScripts.csproj" -c "$DOTNET_CONFIGURATION" -o "$MANAGED_OUTPUT_DIR" /nologo /verbosity:minimal

if [[ -n "$PROJECT_ROOT" ]]; then
    PROJECT_ROOT="$(cd "$PROJECT_ROOT" && pwd)"
    if [[ ! -f "$MANAGED_PROJECT_GENERATOR_SCRIPT" ]]; then
        echo "Error: Managed project generator script not found: $MANAGED_PROJECT_GENERATOR_SCRIPT"
        exit 1
    fi

    generator_output="$(bash "$MANAGED_PROJECT_GENERATOR_SCRIPT" "$PROJECT_ROOT" "$REPO_ROOT")"
    if [[ -n "$generator_output" ]]; then
        IFS='|' read -r MANAGED_PROJECT_CSPROJ MANAGED_PROJECT_ASSEMBLY_FILE <<< "$generator_output"
    fi

    if [[ -n "$MANAGED_PROJECT_CSPROJ" ]]; then
        dotnet build "$MANAGED_PROJECT_CSPROJ" -c "$DOTNET_CONFIGURATION" -o "$MANAGED_OUTPUT_DIR" /nologo /verbosity:minimal
        SCRIPT_ASSEMBLIES_JSON='["Limitless.Managed.TestScripts.dll", '"\"$MANAGED_PROJECT_ASSEMBLY_FILE\""']'
    fi
fi

TARGET_OS="Linux"
if [[ "$(uname -s | tr '[:upper:]' '[:lower:]')" == "darwin" ]]; then
    TARGET_OS="macOS"
fi

cat > "$MANAGED_MANIFEST_PATH" <<EOF
{
  "formatVersion": 1,
  "apiVersion": 1,
  "coralManagedAssembly": "Coral.Managed.dll",
  "coralManagedRuntimeConfig": "Coral.Managed.runtimeconfig.json",
  "contractAssembly": "Limitless.Managed.dll",
  "contractRuntimeConfig": "Limitless.Managed.runtimeconfig.json",
  "scriptAssemblies": ${SCRIPT_ASSEMBLIES_JSON},
  "buildConfiguration": "${DOTNET_CONFIGURATION}",
  "targetOS": "${TARGET_OS}",
  "targetArchitecture": "${PLATFORM}"
}
EOF

echo "Managed scripting artifacts staged to '$MANAGED_OUTPUT_DIR'."
