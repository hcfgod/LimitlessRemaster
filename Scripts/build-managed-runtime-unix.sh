#!/usr/bin/env bash

set -euo pipefail

CONFIGURATION="Debug"
PLATFORM="x64"
OUTPUT_DIR=""
PROJECT_ROOT=""
DOTNET_CONFIGURATION=""
SCRIPT_DIR=""
REPO_ROOT=""
MANAGED_PROJECT_GENERATOR_SCRIPT=""
MANAGED_BUILD_SCRIPT=""
MANAGED_PROJECT_CSPROJ=""
MANAGED_PROJECT_ASSEMBLY_FILE=""
SCRIPT_ASSEMBLIES_JSON='["Limitless.Managed.TestScripts.dll"]'
MANAGED_PROJECT_CACHE_KEY="engine"
MANAGED_LOCK_ROOT=""
MANAGED_LOCK_DIR=""
MANAGED_CACHE_ROOT=""
MANAGED_CACHE_OUTPUT_DIR=""
MANAGED_OUTPUT_DIR=""
MANAGED_MANIFEST_PATH=""
MANAGED_BUILD_ROOT=""
MANAGED_CORAL_BUILD_ROOT=""
MANAGED_CONTRACT_BUILD_ROOT=""
MANAGED_TESTS_BUILD_ROOT=""
MANAGED_PROJECT_BUILD_ROOT=""
MANAGED_CORAL_STAGE_DIR=""
MANAGED_CONTRACT_STAGE_DIR=""
MANAGED_TESTS_STAGE_DIR=""
MANAGED_PROJECT_STAGE_DIR=""
MANAGED_LOCK_WAIT_SECONDS=0
MANAGED_LOCK_WAIT_TIMEOUT_SECONDS=600
MANAGED_LOCK_STALE_TIMEOUT_SECONDS=120
MANAGED_LOCK_ACQUIRED=0
MANAGED_CACHE_REQUIRES_BUILD=1
TARGET_OS="Linux"
LATEST_INPUT_PATH=""

print_usage() {
    echo "Usage: $0 --config [Debug|Release|Dist] --platform [x64|ARM64] --output-dir /path/to/output [--project-root /path/to/project]"
}

ensure_directory() {
    mkdir -p "$1"
}

reset_directory() {
    rm -rf "$1"
    mkdir -p "$1"
}

copy_directory_contents() {
    local source_dir="$1"
    local dest_dir="$2"
    if [[ ! -d "$source_dir" ]]; then
        echo "Error: Managed build output not found: '$source_dir'"
        return 1
    fi
    mkdir -p "$dest_dir"
    tar -C "$source_dir" -cf - . | tar -C "$dest_dir" -xpf -
}

copy_managed_payload() {
    local source_root="$1"
    local dest_root="$2"
    local copy_src="$source_root/Managed"
    local copy_dst="$dest_root/Managed"
    if [[ "$source_root" == "$dest_root" ]]; then
        return 0
    fi
    if [[ ! -d "$copy_src" ]]; then
        echo "Error: Managed payload source not found: '$copy_src'"
        return 1
    fi
    rm -rf "$copy_dst"
    mkdir -p "$copy_dst"
    tar -C "$copy_src" -cf - . | tar -C "$copy_dst" -xpf -
}

update_latest_path() {
    local candidate="$1"
    if [[ -z "$LATEST_INPUT_PATH" || "$candidate" -nt "$LATEST_INPUT_PATH" ]]; then
        LATEST_INPUT_PATH="$candidate"
    fi
}

scan_input_root() {
    local root="$1"
    if [[ -z "$root" || ! -e "$root" ]]; then
        return 0
    fi
    if [[ -d "$root" ]]; then
        while IFS= read -r -d '' candidate; do
            update_latest_path "$candidate"
        done < <(find "$root" -type f ! -path '*/bin/*' ! -path '*/obj/*' -print0)
    else
        update_latest_path "$root"
    fi
}

evaluate_managed_cache_requirement() {
    MANAGED_CACHE_REQUIRES_BUILD=1
    if [[ ! -f "$MANAGED_MANIFEST_PATH" ]]; then
        return 0
    fi

    LATEST_INPUT_PATH=""
    scan_input_root "$REPO_ROOT/Limitless/Vendor/Coral/Coral.Managed"
    scan_input_root "$REPO_ROOT/Managed/Limitless.Managed"
    scan_input_root "$REPO_ROOT/Managed/Limitless.Managed.TestScripts"
    scan_input_root "$MANAGED_PROJECT_GENERATOR_SCRIPT"
    scan_input_root "$MANAGED_BUILD_SCRIPT"

    if [[ -n "$MANAGED_PROJECT_CSPROJ" ]]; then
        scan_input_root "$MANAGED_PROJECT_CSPROJ"
        if [[ -d "$PROJECT_ROOT/Assets" ]]; then
            while IFS= read -r -d '' candidate; do
                update_latest_path "$candidate"
            done < <(find "$PROJECT_ROOT/Assets" -type f -name '*.cs' -print0)
        fi
    fi

    if [[ -z "$LATEST_INPUT_PATH" || ! "$LATEST_INPUT_PATH" -nt "$MANAGED_MANIFEST_PATH" ]]; then
        MANAGED_CACHE_REQUIRES_BUILD=0
    fi
}

get_path_mtime_epoch() {
    local path="$1"
    if stat -c %Y "$path" >/dev/null 2>&1; then
        stat -c %Y "$path"
    else
        stat -f %m "$path"
    fi
}

check_stale_managed_lock() {
    local lock_age=0
    local current_epoch=0
    local lock_epoch=0
    if [[ ! -d "$MANAGED_LOCK_DIR" ]]; then
        return 0
    fi
    lock_epoch="$(get_path_mtime_epoch "$MANAGED_LOCK_DIR")"
    current_epoch="$(date +%s)"
    lock_age=$(( current_epoch - lock_epoch ))
    if (( lock_age >= MANAGED_LOCK_STALE_TIMEOUT_SECONDS )); then
        echo "Warning: Managed build lock looks stale (age ${lock_age}s). Removing: $MANAGED_LOCK_DIR"
        rm -rf "$MANAGED_LOCK_DIR" >/dev/null 2>&1 || true
    fi
}

acquire_managed_lock() {
    MANAGED_LOCK_WAIT_SECONDS=0
    while ! mkdir "$MANAGED_LOCK_DIR" 2>/dev/null; do
        MANAGED_LOCK_WAIT_SECONDS=$((MANAGED_LOCK_WAIT_SECONDS + 1))
        if (( MANAGED_LOCK_WAIT_SECONDS == 1 )); then
            echo "Waiting for managed build lock: '$MANAGED_LOCK_DIR'..."
        fi
        if (( MANAGED_LOCK_WAIT_SECONDS == 15 )); then
            echo "Waiting for another managed build to finish..."
        fi
        if (( MANAGED_LOCK_WAIT_SECONDS == 60 || MANAGED_LOCK_WAIT_SECONDS == 120 || MANAGED_LOCK_WAIT_SECONDS == 180 || MANAGED_LOCK_WAIT_SECONDS == 240 || MANAGED_LOCK_WAIT_SECONDS == 300 || MANAGED_LOCK_WAIT_SECONDS == 360 || MANAGED_LOCK_WAIT_SECONDS == 420 || MANAGED_LOCK_WAIT_SECONDS == 480 || MANAGED_LOCK_WAIT_SECONDS == 540 )); then
            echo "Still waiting for managed build lock: '$MANAGED_LOCK_DIR'..."
        fi
        if (( MANAGED_LOCK_WAIT_SECONDS == 120 )); then
            echo "Note: If this keeps happening, a stale lock may exist from an aborted build."
        fi
        if (( MANAGED_LOCK_WAIT_SECONDS >= 30 && MANAGED_LOCK_WAIT_SECONDS % 30 == 0 && MANAGED_LOCK_WAIT_SECONDS <= 120 )); then
            check_stale_managed_lock
        fi
        if (( MANAGED_LOCK_WAIT_SECONDS >= MANAGED_LOCK_WAIT_TIMEOUT_SECONDS )); then
            echo "Error: Timed out waiting for managed build lock after ${MANAGED_LOCK_WAIT_TIMEOUT_SECONDS} seconds."
            echo "If no build is running, delete the lock directory and retry: '$MANAGED_LOCK_DIR'"
            return 1
        fi
        sleep 1
    done

    {
        echo "Managed lock acquired by: ${USER:-unknown}@$(hostname)"
        echo "Script: $(basename "$0")"
        echo "Args: --config $CONFIGURATION --platform $PLATFORM --output-dir \"$OUTPUT_DIR\"${PROJECT_ROOT:+ --project-root \"$PROJECT_ROOT\"}"
        echo "Timestamp: $(date)"
    } > "$MANAGED_LOCK_DIR/owner.txt"
    MANAGED_LOCK_ACQUIRED=1
}

release_managed_lock() {
    if [[ "$MANAGED_LOCK_ACQUIRED" != "1" ]]; then
        return 0
    fi
    rm -rf "$MANAGED_LOCK_DIR" >/dev/null 2>&1 || true
    MANAGED_LOCK_ACQUIRED=0
}

write_manifest() {
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
}

cleanup() {
    release_managed_lock >/dev/null 2>&1 || true
}

trap cleanup EXIT

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
if [[ -n "$PROJECT_ROOT" ]]; then
    PROJECT_ROOT="$(cd "$PROJECT_ROOT" && pwd)"
fi

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
MANAGED_BUILD_SCRIPT="$REPO_ROOT/Scripts/build-managed-runtime-unix.sh"
MANAGED_PROJECT_GENERATOR_SCRIPT="$REPO_ROOT/Scripts/generate-managed-project-csproj-unix.sh"
if [[ -n "$PROJECT_ROOT" ]]; then
    MANAGED_PROJECT_CACHE_KEY="project-$(basename "$PROJECT_ROOT")"
fi

MANAGED_LOCK_ROOT="$REPO_ROOT/Build/ManagedRuntimeLocks"
MANAGED_LOCK_DIR="$MANAGED_LOCK_ROOT/${DOTNET_CONFIGURATION}-${PLATFORM}/${MANAGED_PROJECT_CACHE_KEY}"
MANAGED_CACHE_ROOT="$REPO_ROOT/Build/ManagedRuntimeCache"
MANAGED_CACHE_OUTPUT_DIR="$MANAGED_CACHE_ROOT/${DOTNET_CONFIGURATION}-${PLATFORM}/${MANAGED_PROJECT_CACHE_KEY}"
MANAGED_OUTPUT_DIR="$MANAGED_CACHE_OUTPUT_DIR/Managed"
MANAGED_MANIFEST_PATH="$MANAGED_OUTPUT_DIR/Limitless.Managed.payload.json"
MANAGED_BUILD_ROOT="$MANAGED_CACHE_OUTPUT_DIR/_build"
MANAGED_CORAL_BUILD_ROOT="$MANAGED_BUILD_ROOT/Coral.Managed"
MANAGED_CONTRACT_BUILD_ROOT="$MANAGED_BUILD_ROOT/Limitless.Managed"
MANAGED_TESTS_BUILD_ROOT="$MANAGED_BUILD_ROOT/Limitless.Managed.TestScripts"
MANAGED_PROJECT_BUILD_ROOT="$MANAGED_BUILD_ROOT/ProjectScripts"
MANAGED_CORAL_STAGE_DIR="$MANAGED_CORAL_BUILD_ROOT/stage"
MANAGED_CONTRACT_STAGE_DIR="$MANAGED_CONTRACT_BUILD_ROOT/stage"
MANAGED_TESTS_STAGE_DIR="$MANAGED_TESTS_BUILD_ROOT/stage"
MANAGED_PROJECT_STAGE_DIR="$MANAGED_PROJECT_BUILD_ROOT/stage"

if [[ "$(uname -s | tr '[:upper:]' '[:lower:]')" == "darwin" ]]; then
    TARGET_OS="macOS"
fi

if [[ "${LT_SKIP_MANAGED_RUNTIME_BUILD:-0}" == "1" ]]; then
    echo "Skipping managed scripting artifact build because LT_SKIP_MANAGED_RUNTIME_BUILD=1."
    exit 0
fi

ensure_directory "$MANAGED_OUTPUT_DIR"
ensure_directory "$MANAGED_BUILD_ROOT"
ensure_directory "$MANAGED_LOCK_ROOT"
ensure_directory "$(dirname "$MANAGED_LOCK_DIR")"

if [[ -n "$PROJECT_ROOT" ]]; then
    if [[ ! -f "$MANAGED_PROJECT_GENERATOR_SCRIPT" ]]; then
        echo "Error: Managed project generator script not found: $MANAGED_PROJECT_GENERATOR_SCRIPT"
        exit 1
    fi
    generator_output="$(bash "$MANAGED_PROJECT_GENERATOR_SCRIPT" "$PROJECT_ROOT" "$REPO_ROOT")"
    if [[ -n "$generator_output" ]]; then
        IFS='|' read -r MANAGED_PROJECT_CSPROJ MANAGED_PROJECT_ASSEMBLY_FILE <<< "$generator_output"
    fi
fi

evaluate_managed_cache_requirement
if [[ "$MANAGED_CACHE_REQUIRES_BUILD" == "1" ]]; then
    acquire_managed_lock
    evaluate_managed_cache_requirement
fi

if [[ "$MANAGED_CACHE_REQUIRES_BUILD" == "1" ]]; then
    reset_directory "$MANAGED_OUTPUT_DIR"

    echo "Building managed scripting artifacts for $CONFIGURATION $PLATFORM..."

    ensure_directory "$MANAGED_CORAL_STAGE_DIR"
    dotnet publish "$REPO_ROOT/Limitless/Vendor/Coral/Coral.Managed/Coral.Managed-Static.csproj" -c "$DOTNET_CONFIGURATION" -o "$MANAGED_CORAL_STAGE_DIR" /nologo /verbosity:minimal "/p:BaseIntermediateOutputPath=$MANAGED_CORAL_BUILD_ROOT/obj/" "/p:MSBuildProjectExtensionsPath=$MANAGED_CORAL_BUILD_ROOT/obj/" "/p:BaseOutputPath=$MANAGED_CORAL_BUILD_ROOT/bin/"

    ensure_directory "$MANAGED_CONTRACT_STAGE_DIR"
    dotnet build "$REPO_ROOT/Managed/Limitless.Managed/Limitless.Managed.csproj" -c "$DOTNET_CONFIGURATION" -o "$MANAGED_CONTRACT_STAGE_DIR" /nologo /verbosity:minimal "/p:BaseIntermediateOutputPath=$MANAGED_CONTRACT_BUILD_ROOT/obj/" "/p:MSBuildProjectExtensionsPath=$MANAGED_CONTRACT_BUILD_ROOT/obj/" "/p:BaseOutputPath=$MANAGED_CONTRACT_BUILD_ROOT/bin/"

    ensure_directory "$MANAGED_TESTS_STAGE_DIR"
    dotnet build "$REPO_ROOT/Managed/Limitless.Managed.TestScripts/Limitless.Managed.TestScripts.csproj" -c "$DOTNET_CONFIGURATION" -o "$MANAGED_TESTS_STAGE_DIR" /nologo /verbosity:minimal /p:BuildProjectReferences=false "/p:LimitlessManagedReferencePath=$MANAGED_CONTRACT_STAGE_DIR/Limitless.Managed.dll" "/p:BaseIntermediateOutputPath=$MANAGED_TESTS_BUILD_ROOT/obj/" "/p:MSBuildProjectExtensionsPath=$MANAGED_TESTS_BUILD_ROOT/obj/" "/p:BaseOutputPath=$MANAGED_TESTS_BUILD_ROOT/bin/"

    if [[ -n "$MANAGED_PROJECT_CSPROJ" ]]; then
        ensure_directory "$MANAGED_PROJECT_STAGE_DIR"
        dotnet build "$MANAGED_PROJECT_CSPROJ" -c "$DOTNET_CONFIGURATION" -o "$MANAGED_PROJECT_STAGE_DIR" /nologo /verbosity:minimal /p:BuildProjectReferences=false "/p:LimitlessManagedReferencePath=$MANAGED_CONTRACT_STAGE_DIR/Limitless.Managed.dll" "/p:BaseIntermediateOutputPath=$MANAGED_PROJECT_BUILD_ROOT/obj/" "/p:MSBuildProjectExtensionsPath=$MANAGED_PROJECT_BUILD_ROOT/obj/" "/p:BaseOutputPath=$MANAGED_PROJECT_BUILD_ROOT/bin/"
        SCRIPT_ASSEMBLIES_JSON="[\"Limitless.Managed.TestScripts.dll\", \"$MANAGED_PROJECT_ASSEMBLY_FILE\"]"
    else
        SCRIPT_ASSEMBLIES_JSON='["Limitless.Managed.TestScripts.dll"]'
    fi

    copy_directory_contents "$MANAGED_CORAL_STAGE_DIR" "$MANAGED_OUTPUT_DIR"
    copy_directory_contents "$MANAGED_CONTRACT_STAGE_DIR" "$MANAGED_OUTPUT_DIR"
    copy_directory_contents "$MANAGED_TESTS_STAGE_DIR" "$MANAGED_OUTPUT_DIR"
    if [[ -n "$MANAGED_PROJECT_CSPROJ" ]]; then
        copy_directory_contents "$MANAGED_PROJECT_STAGE_DIR" "$MANAGED_OUTPUT_DIR"
    fi

    write_manifest
else
    echo "Using cached managed scripting artifacts for $CONFIGURATION $PLATFORM..."
fi

release_managed_lock
copy_managed_payload "$MANAGED_CACHE_OUTPUT_DIR" "$OUTPUT_DIR"

echo "Managed scripting artifacts staged to '$OUTPUT_DIR/Managed'."
