#!/usr/bin/env bash

set -euo pipefail

CONFIGURATION="Debug"
PLATFORM="x64"
PROJECT_ROOT=""

print_usage() {
    echo "Usage (preferred): $0 --config [Debug|Release|Dist] --platform [x64|ARM64] --project-root /path/to/project"
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
OUTPUT_DIR="$TOOLCHAIN_ROOT/Build/$BUILD_FOLDER/Editor"
INTERMEDIATE_DIR="$TOOLCHAIN_ROOT/Build/Intermediates/$BUILD_FOLDER/ProjectScriptCore"
SDK_INCLUDE_DIR="$TOOLCHAIN_ROOT/SDK/include"
SDK_VENDOR_DIR="$TOOLCHAIN_ROOT/SDK/vendor"
SDK_LIB_DIR="$TOOLCHAIN_ROOT/SDK/lib/$BUILD_FOLDER"

mkdir -p "$OUTPUT_DIR" "$INTERMEDIATE_DIR"

GLUE_CPP="$INTERMEDIATE_DIR/ScriptCoreHostGlue.cpp"
cat > "$GLUE_CPP" <<'EOF'
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

mapfile -t SOURCE_FILES < <(find "$GENERATED_DIR" -type f -name '*.cpp' ! -name 'ScriptCoreHostGlue.cpp')
if [[ "$SYSTEM_NAME" == "macosx" ]]; then
    OUTPUT_EXTENSION="dylib"
    CXX_BIN="${CXX:-clang++}"
else
    OUTPUT_EXTENSION="so"
    CXX_BIN="${CXX:-g++}"
fi

OUTPUT_LIB="$OUTPUT_DIR/libScriptCore.${OUTPUT_EXTENSION}"
"$CXX_BIN" -shared -fPIC -std=c++20 -O2 \
    -I"$SDK_INCLUDE_DIR" -I"$SDK_VENDOR_DIR" -I"$GENERATED_DIR" \
    "$GLUE_CPP" "${SOURCE_FILES[@]}" \
    -L"$SDK_LIB_DIR" -lLimitless \
    -o "$OUTPUT_LIB"

echo "ScriptCore build completed successfully."
echo "Output: $OUTPUT_LIB"
