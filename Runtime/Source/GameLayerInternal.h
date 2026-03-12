#pragma once

#include "Limitless.h"

namespace Limitless
{
    class Scene;
}

namespace Limitless::GameLayerInternal
{

    inline constexpr SceneRoleMask RuntimeSceneBaseRoles =
        SceneRole::RuntimeUpdate |
        SceneRole::FixedUpdate |
        SceneRole::Render |
        SceneRole::AudioPlayback;

    inline constexpr SceneRoleMask RuntimeSceneActiveRoles =
        RuntimeSceneBaseRoles |
        SceneRole::GameplayPrimary |
        SceneRole::ScriptQueryTarget;

    void ApplyRuntimeProjectSettingsFromBundle();
    void ApplyRuntimeProjectSettingsToScene(::Limitless::Scene& scene);
}
