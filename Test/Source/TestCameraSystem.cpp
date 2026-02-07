#include <doctest/doctest.h>

#include "Graphics/Camera/CameraManager.h"
#include "Graphics/Camera/OrthographicCamera2D.h"
#include "Graphics/Camera/PerspectiveCamera3D.h"

TEST_SUITE("Camera System")
{
    TEST_CASE("CameraManager creates cameras and manages active camera")
    {
        Limitless::CameraManager manager;

        Limitless::CameraManager::Orthographic2DCreateInfo orthoInfo{};
        orthoInfo.Name = "Ortho2D";
        orthoInfo.ViewportWidthPixels = 800;
        orthoInfo.ViewportHeightPixels = 600;

        const Limitless::CameraId orthoId = manager.CreateOrthographic2D(orthoInfo);
        CHECK(orthoId.IsValid());
        CHECK(manager.GetActiveCameraId() == orthoId);
        CHECK(manager.GetOrthographic2D(orthoId) != nullptr);

        Limitless::CameraManager::Perspective3DCreateInfo perspInfo{};
        perspInfo.Name = "Persp3D";
        perspInfo.ViewportWidthPixels = 800;
        perspInfo.ViewportHeightPixels = 600;

        const Limitless::CameraId perspId = manager.CreatePerspective3D(perspInfo);
        CHECK(perspId.IsValid());
        CHECK(manager.GetCameraCount() == 2);

        CHECK(manager.SetActiveCamera(perspId));
        CHECK(manager.GetActiveCameraId() == perspId);
        CHECK(manager.GetPerspective3D(perspId) != nullptr);

        const auto found = manager.FindByName("Ortho2D");
        CHECK(found.has_value());
        CHECK(found.value() == orthoId);
    }

    TEST_CASE("Destroying active camera selects another or clears active")
    {
        Limitless::CameraManager manager;

        Limitless::CameraManager::Perspective3DCreateInfo aInfo{};
        aInfo.Name = "A";
        aInfo.ViewportWidthPixels = 16;
        aInfo.ViewportHeightPixels = 9;
        const Limitless::CameraId a = manager.CreatePerspective3D(aInfo);

        Limitless::CameraManager::Perspective3DCreateInfo bInfo{};
        bInfo.Name = "B";
        bInfo.ViewportWidthPixels = 16;
        bInfo.ViewportHeightPixels = 9;
        const Limitless::CameraId b = manager.CreatePerspective3D(bInfo);

        CHECK(manager.SetActiveCamera(a));
        CHECK(manager.DestroyCamera(a));

        // Active camera should no longer be A. It may be B (or something else if selection strategy changes).
        CHECK(manager.GetActiveCameraId() != a);
        CHECK(manager.GetActiveCamera() != nullptr);

        CHECK(manager.DestroyCamera(b));
        CHECK(manager.GetCameraCount() == 0);
        CHECK(manager.GetActiveCamera() == nullptr);
        CHECK(manager.GetActiveCameraId().IsValid() == false);
    }
}

