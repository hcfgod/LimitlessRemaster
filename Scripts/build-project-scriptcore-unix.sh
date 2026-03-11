#!/usr/bin/env bash

# --------------------------------------------------------------------------
#  build-project-scriptcore-unix.sh
#
#  Incremental ScriptCore build.  Each .cpp is compiled to a separate .o and
#  only recompiled when the source or one of its header dependencies has
#  changed (tracked via -MMD / .d files).  All .o files are then linked into
#  libScriptCore.so / .dylib.
#
#  Pass --clean to force a full rebuild.
# --------------------------------------------------------------------------

set -euo pipefail

CONFIGURATION="Debug"
PLATFORM="x64"
PROJECT_ROOT=""
FORCE_CLEAN=0

print_usage() {
    echo "Usage (preferred): $0 --config [Debug|Release|Dist] --platform [x64|ARM64] --project-root /path/to/project [--clean]"
    echo "Usage (legacy):    $0 [Debug|Release|Dist] [x64|ARM64] /path/to/project"
}

# Support both the newer flag-style contract and legacy positional args.
if [[ $# -gt 0 && "$1" == --* ]]; then
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
            --project-root)
                PROJECT_ROOT="${2:-}"
                shift 2
                ;;
            --clean)
                FORCE_CLEAN=1
                shift
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
else
    CONFIGURATION="${1:-Debug}"
    PLATFORM="${2:-x64}"
    PROJECT_ROOT="${3:-}"
fi

if [[ -z "$PROJECT_ROOT" ]]; then
    echo "Error: Missing project root argument."
    print_usage
    exit 1
fi

if [[ "$CONFIGURATION" != "Debug" && "$CONFIGURATION" != "Release" && "$CONFIGURATION" != "Dist" ]]; then
    echo "Error: Invalid configuration '$CONFIGURATION'. Expected Debug, Release, or Dist."
    exit 1
fi

if [[ "$PLATFORM" != "x64" && "$PLATFORM" != "ARM64" ]]; then
    echo "Error: Invalid platform '$PLATFORM'. Expected x64 or ARM64."
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOLCHAIN_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
GENERATED_DIR="$PROJECT_ROOT/Build/Generated/ScriptCore"
mkdir -p "$GENERATED_DIR"

CONFIG_LOWER="$(echo "$CONFIGURATION" | tr '[:upper:]' '[:lower:]')"
PLATFORM_LOWER="$(echo "$PLATFORM" | tr '[:upper:]' '[:lower:]')"
SYSTEM_NAME="$(uname -s | tr '[:upper:]' '[:lower:]')"
if [[ "$SYSTEM_NAME" == "darwin" ]]; then
    SYSTEM_NAME="macosx"
fi

BUILD_FOLDER="${CONFIG_LOWER}_${PLATFORM_LOWER}-${SYSTEM_NAME}-${PLATFORM}"
RUNTIME_TEMPLATE_DIR="$TOOLCHAIN_ROOT/RuntimeTemplates/$BUILD_FOLDER"
MANAGED_BUILD_SCRIPT="$TOOLCHAIN_ROOT/Scripts/build-managed-runtime-unix.sh"
OUTPUT_DIR="$TOOLCHAIN_ROOT/Build/$BUILD_FOLDER/Editor"
PROJECT_LOCAL_OUTPUT_DIR="$PROJECT_ROOT/Build/ScriptCore/$BUILD_FOLDER"
INTERMEDIATE_DIR="$TOOLCHAIN_ROOT/Build/Intermediates/$BUILD_FOLDER/ProjectScriptCore"
OBJ_DIR="$INTERMEDIATE_DIR/obj"
DEP_DIR="$INTERMEDIATE_DIR/dep"
SDK_INCLUDE_DIR="$TOOLCHAIN_ROOT/SDK/include"
SDK_VENDOR_DIR="$TOOLCHAIN_ROOT/SDK/vendor"
SDK_LIB_DIR="$TOOLCHAIN_ROOT/SDK/lib/$BUILD_FOLDER"

mkdir -p "$OUTPUT_DIR" "$INTERMEDIATE_DIR" "$OBJ_DIR" "$DEP_DIR"

# --- Clean mode: wipe object and dep caches ---
if [[ "$FORCE_CLEAN" -eq 1 ]]; then
    echo "Incremental build: clean requested, wiping object cache..."
    rm -rf "$OBJ_DIR" "$DEP_DIR"
    mkdir -p "$OBJ_DIR" "$DEP_DIR"
fi

# --- Generate host glue source ---
GLUE_CPP="$INTERMEDIATE_DIR/ScriptCoreHostGlue.cpp"
GLUE_CPP_TMP="${GLUE_CPP}.tmp"
cat > "$GLUE_CPP_TMP" <<'EOF'
#include "ScriptCoreRegistration.h"
#include "Scene/SceneManager.h"
#include "Scripting/Debug.h"
#include "Scripting/Input.h"
#include "Scripting/InputActions.h"
#include "Scripting/Physics2D.h"
namespace Limitless::ScriptCore
{
    namespace
    {
        std::vector<ScriptRegistration>& GetMutableRegistrations()
        {
            static std::vector<ScriptRegistration> registrations;
            return registrations;
        }
    }
    void AddRegistration(const ScriptRegistration& registration)
    {
        if (registration.ClassName.empty() || !registration.CreateFunction)
            return;
        auto& registrations = GetMutableRegistrations();
        for (auto& existingRegistration : registrations)
        {
            if (existingRegistration.ClassName == registration.ClassName)
            {
                existingRegistration = registration;
                return;
            }
        }
        registrations.push_back(registration);
    }
    const std::vector<ScriptRegistration>& GetRegistrations()
    {
        return GetMutableRegistrations();
    }
}
extern "C" LT_SCRIPTCORE_API void LT_RegisterScriptCoreTypes(Limitless::NativeScriptRegistrationCallback registrationCallback)
{
    if (!registrationCallback)
        return;
    for (const auto& registration : Limitless::ScriptCore::GetRegistrations())
        registrationCallback(registration.ClassName.c_str(), registration.CreateFunction);
}
extern "C" LT_SCRIPTCORE_API void LT_SetSceneTransitionBridge(Limitless::SceneTransitionBridgeCallback callback)
{
    Limitless::SceneManager::SetTransitionBridgeCallback(callback);
}
extern "C" LT_SCRIPTCORE_API void LT_SetInputActionAxis1DBridge(Limitless::InputActionAxis1DBridgeCallback callback)
{
    Limitless::InputActions::SetAxis1DBridgeCallback(callback);
    Limitless::Input::SetAxisBridgeCallback(callback);
}
extern "C" LT_SCRIPTCORE_API void LT_SetInputActionAxis2DBridge(Limitless::InputActionAxis2DBridgeCallback callback)
{
    Limitless::InputActions::SetAxis2DBridgeCallback(callback);
}
extern "C" LT_SCRIPTCORE_API void LT_SetInputActionExistsBridge(Limitless::InputActionExistsBridgeCallback callback)
{
    Limitless::InputActions::SetExistsBridgeCallback(callback);
}
extern "C" LT_SCRIPTCORE_API void LT_SetInputActionPressedBridge(Limitless::InputActionPressedBridgeCallback callback)
{
    Limitless::InputActions::SetPressedBridgeCallback(callback);
}
extern "C" LT_SCRIPTCORE_API void LT_SetInputActionStartedBridge(Limitless::InputActionTriggerBridgeCallback callback)
{
    Limitless::InputActions::SetStartedBridgeCallback(callback);
    Limitless::Input::SetButtonDownBridgeCallback(callback);
}
extern "C" LT_SCRIPTCORE_API void LT_SetInputActionPerformedBridge(Limitless::InputActionTriggerBridgeCallback callback)
{
    Limitless::InputActions::SetPerformedBridgeCallback(callback);
}
extern "C" LT_SCRIPTCORE_API void LT_SetInputActionCanceledBridge(Limitless::InputActionTriggerBridgeCallback callback)
{
    Limitless::InputActions::SetCanceledBridgeCallback(callback);
}
extern "C" LT_SCRIPTCORE_API void LT_SetInputActionButtonBridge(Limitless::InputActionTriggerBridgeCallback callback)
{
    Limitless::InputActions::SetButtonBridgeCallback(callback);
    Limitless::Input::SetButtonBridgeCallback(callback);
}
extern "C" LT_SCRIPTCORE_API void LT_SetInputButtonDownBridge(Limitless::InputActionTriggerBridgeCallback callback)
{
    Limitless::Input::SetButtonDownBridgeCallback(callback);
}
extern "C" LT_SCRIPTCORE_API void LT_SetInputButtonBridge(Limitless::InputActionTriggerBridgeCallback callback)
{
    Limitless::InputActions::SetButtonBridgeCallback(callback);
    Limitless::Input::SetButtonBridgeCallback(callback);
}
extern "C" LT_SCRIPTCORE_API void LT_SetPhysics2DRaycastBridge(Limitless::Physics2DRaycastBridgeCallback callback)
{
    Limitless::Physics2D::SetRaycastBridgeCallback(callback);
}
extern "C" LT_SCRIPTCORE_API void LT_SetScriptLogBridge(Limitless::ScriptLogBridgeCallback callback)
{
    Limitless::Debug::SetLogBridgeCallback(callback);
}
extern "C" LT_SCRIPTCORE_API void LT_SetScriptCreateEntityBridge(Limitless::ScriptCreateEntityBridgeCallback callback)
{
    Limitless::ScriptableEntity::SetCreateEntityBridgeCallback(callback);
}
extern "C" LT_SCRIPTCORE_API void LT_SetScriptDestroyEntityBridge(Limitless::ScriptDestroyEntityBridgeCallback callback)
{
    Limitless::ScriptableEntity::SetDestroyEntityBridgeCallback(callback);
}
extern "C" LT_SCRIPTCORE_API void LT_SetScriptInstantiatePrefabBridge(Limitless::ScriptInstantiatePrefabBridgeCallback callback)
{
    Limitless::ScriptableEntity::SetInstantiatePrefabBridgeCallback(callback);
}
EOF
if [[ ! -f "$GLUE_CPP" ]] || ! cmp -s "$GLUE_CPP_TMP" "$GLUE_CPP"; then
    mv "$GLUE_CPP_TMP" "$GLUE_CPP"
else
    rm -f "$GLUE_CPP_TMP"
fi

# --- Discover source files ---
SOURCE_FILES=()
while IFS= read -r src; do
    [[ -z "$src" ]] && continue
    SOURCE_FILES+=("$src")
done < <(find "$GENERATED_DIR" -type f -name '*.cpp' ! -name 'ScriptCoreHostGlue.cpp' -print | LC_ALL=C sort)
if [[ "$SYSTEM_NAME" == "macosx" ]]; then
    OUTPUT_EXTENSION="dylib"
    CXX_BIN="${CXX:-clang++}"
else
    OUTPUT_EXTENSION="so"
    CXX_BIN="${CXX:-g++}"
fi

OUTPUT_LIB="$OUTPUT_DIR/libScriptCore.${OUTPUT_EXTENSION}"

# --- Helper: convert a source path to a stable .o / .d filename ---
safe_obj_name() {
    local src_path="$1"
    local rel_path
    if [[ "$src_path" == "$GENERATED_DIR/"* ]]; then
        rel_path="${src_path#"$GENERATED_DIR"/}"
    else
        rel_path="$(basename "$src_path")"
    fi
    # Replace path separators and spaces with underscores
    local safe_name="${rel_path//\//_}"
    safe_name="${safe_name// /_}"
    echo "${safe_name%.cpp}.o"
}

# Common compiler flags
COMMON_CXX_FLAGS=(-fPIC -std=c++20 -O2 -MMD -I"$SDK_INCLUDE_DIR" -I"$SDK_VENDOR_DIR" -I"$GENERATED_DIR")

# -------------------------------------------------------------------------
#  INCREMENTAL COMPILE
#
#  For each .cpp, check whether the .o needs rebuilding by comparing
#  timestamps.  -MMD generates .d files alongside .o files that list
#  header dependencies.  We parse these to detect header changes.
# -------------------------------------------------------------------------

echo "Building project ScriptCore module (incremental)..."

if [[ "${#SOURCE_FILES[@]}" -eq 0 ]]; then
    DUMMY_CPP="$INTERMEDIATE_DIR/DummyScriptCoreTranslationUnit.cpp"
    DUMMY_CPP_TMP="${DUMMY_CPP}.tmp"
    printf '%s\n' '// Auto-generated fallback translation unit.' > "$DUMMY_CPP_TMP"
    if [[ ! -f "$DUMMY_CPP" ]] || ! cmp -s "$DUMMY_CPP_TMP" "$DUMMY_CPP"; then
        mv "$DUMMY_CPP_TMP" "$DUMMY_CPP"
    else
        rm -f "$DUMMY_CPP_TMP"
    fi
    SOURCE_FILES+=("$DUMMY_CPP")
fi

compiled_count=0
skipped_count=0
failed_count=0
obj_files=()

GLUE_OBJ="$OBJ_DIR/ScriptCoreHostGlue.o"
GLUE_DEP="$DEP_DIR/ScriptCoreHostGlue.d"
obj_files+=("$GLUE_OBJ")

# needs_compile <source_path> <obj_path> <dep_path>
# Returns 0 (true) if recompilation is needed, 1 (false) if up-to-date.
needs_compile() {
    local src="$1" obj="$2" dep="$3"

    # No object file → must compile
    [[ ! -f "$obj" ]] && return 0

    # Source newer than object → must compile
    [[ "$src" -nt "$obj" ]] && return 0

    # No dep file → must compile (can't verify headers)
    [[ ! -f "$dep" ]] && return 0

    # Parse the .d file (Makefile format) and check each dependency
    # The format is: target: dep1 dep2 dep3 ...  (possibly with line continuations)
    local deps_text
    deps_text=$(sed 's/\\$//' "$dep" | tr '\n' ' ' | sed 's/^[^:]*://')
    local dep_file
    for dep_file in $deps_text; do
        # Skip empty tokens
        [[ -z "$dep_file" ]] && continue
        # If a dependency no longer exists or is newer → recompile
        if [[ ! -f "$dep_file" ]] || [[ "$dep_file" -nt "$obj" ]]; then
            return 0
        fi
    done

    return 1
}

if needs_compile "$GLUE_CPP" "$GLUE_OBJ" "$GLUE_DEP"; then
    if "$CXX_BIN" "${COMMON_CXX_FLAGS[@]}" -MF "$GLUE_DEP" -c "$GLUE_CPP" -o "$GLUE_OBJ"; then
        compiled_count=$((compiled_count + 1))
    else
        echo "Error: compilation failed for $(basename "$GLUE_CPP")"
        failed_count=$((failed_count + 1))
        rm -f "$GLUE_OBJ" "$GLUE_DEP"
    fi
else
    skipped_count=$((skipped_count + 1))
fi

# Compile each user script source
for src in "${SOURCE_FILES[@]}"; do
    obj_name="$(safe_obj_name "$src")"
    obj_path="$OBJ_DIR/$obj_name"
    dep_path="$DEP_DIR/${obj_name%.o}.d"
    obj_files+=("$obj_path")

    if needs_compile "$src" "$obj_path" "$dep_path"; then
        if "$CXX_BIN" "${COMMON_CXX_FLAGS[@]}" -MF "$dep_path" -c "$src" -o "$obj_path"; then
            compiled_count=$((compiled_count + 1))
        else
            echo "Error: compilation failed for $(basename "$src")"
            failed_count=$((failed_count + 1))
            rm -f "$obj_path" "$dep_path"
        fi
    else
        skipped_count=$((skipped_count + 1))
    fi
done

echo "Incremental compile: $compiled_count compiled, $skipped_count up-to-date, $failed_count failed."

if [[ "$failed_count" -gt 0 ]]; then
    echo "Error: Incremental compilation failed."
    exit 1
fi

# --- Prune stale .o files whose source no longer exists ---
pruned=0
for existing_obj in "$OBJ_DIR"/*.o; do
    [[ ! -f "$existing_obj" ]] && continue
    is_expected=0
    for expected_obj in "${obj_files[@]}"; do
        if [[ "$existing_obj" == "$expected_obj" ]]; then
            is_expected=1
            break
        fi
    done
    if [[ "$is_expected" -eq 0 ]]; then
        rm -f "$existing_obj"
        dep_counterpart="$DEP_DIR/$(basename "${existing_obj%.o}.d")"
        rm -f "$dep_counterpart"
        pruned=$((pruned + 1))
    fi
done
if [[ "$pruned" -gt 0 ]]; then
    echo "Pruned $pruned stale object file(s)."
fi

link_required=0
if [[ "$compiled_count" -ne 0 || "$pruned" -ne 0 ]]; then
    link_required=1
elif [[ ! -f "$OUTPUT_LIB" ]]; then
    link_required=1
else
    for obj_path in "${obj_files[@]}"; do
        if [[ ! -f "$obj_path" || "$obj_path" -nt "$OUTPUT_LIB" ]]; then
            link_required=1
            break
        fi
    done
fi

if [[ "$link_required" -eq 0 ]]; then
    echo "libScriptCore.${OUTPUT_EXTENSION} is up-to-date; skipping relink."
else
    echo "Linking libScriptCore.${OUTPUT_EXTENSION}..."
    "$CXX_BIN" -shared -fPIC \
        "${obj_files[@]}" \
        -L"$SDK_LIB_DIR" -lLimitless \
        -o "$OUTPUT_LIB"
fi

if [[ ! -f "$MANAGED_BUILD_SCRIPT" ]]; then
    echo "Error: Managed runtime build script not found: $MANAGED_BUILD_SCRIPT"
    exit 1
fi

bash "$MANAGED_BUILD_SCRIPT" --config "$CONFIGURATION" --platform "$PLATFORM" --output-dir "$PROJECT_LOCAL_OUTPUT_DIR" --project-root "$PROJECT_ROOT"

rm -rf "$OUTPUT_DIR/Managed"
mkdir -p "$OUTPUT_DIR/Managed"
tar -C "$PROJECT_LOCAL_OUTPUT_DIR/Managed" -cf - . | tar -C "$OUTPUT_DIR/Managed" -xpf -

rm -rf "$RUNTIME_TEMPLATE_DIR/Managed"
mkdir -p "$RUNTIME_TEMPLATE_DIR/Managed"
tar -C "$PROJECT_LOCAL_OUTPUT_DIR/Managed" -cf - . | tar -C "$RUNTIME_TEMPLATE_DIR/Managed" -xpf -

echo "ScriptCore build completed successfully."
echo "Output: $OUTPUT_LIB"
