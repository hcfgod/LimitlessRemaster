#pragma once

#include "Graphics/Camera/CameraTypes.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Limitless
{
    class Camera;
    class OrthographicCamera2D;
    class PerspectiveCamera3D;

    class CameraManager final
    {
    public:
        CameraManager() = default;
        ~CameraManager() = default;

        CameraManager(const CameraManager&) = delete;
        CameraManager& operator=(const CameraManager&) = delete;

        struct Orthographic2DCreateInfo
        {
            std::string Name = "Orthographic2D";
            CameraUsage Usage = CameraUsage::Gameplay;
            uint32_t ViewportWidthPixels = 1;
            uint32_t ViewportHeightPixels = 1;
            float Zoom = 1.0f;
            float NearPlane = -1.0f;
            float FarPlane = 1.0f;
        };

        struct Perspective3DCreateInfo
        {
            std::string Name = "Perspective3D";
            CameraUsage Usage = CameraUsage::Gameplay;
            uint32_t ViewportWidthPixels = 1;
            uint32_t ViewportHeightPixels = 1;
            float FieldOfViewYDegrees = 60.0f;
            float NearPlane = 0.1f;
            float FarPlane = 1000.0f;
        };

        CameraId CreateOrthographic2D(const Orthographic2DCreateInfo& info);
        CameraId CreatePerspective3D(const Perspective3DCreateInfo& info);

        bool DestroyCamera(CameraId id);
        void Clear();

        Camera* GetCamera(CameraId id);
        const Camera* GetCamera(CameraId id) const;

        OrthographicCamera2D* GetOrthographic2D(CameraId id);
        PerspectiveCamera3D* GetPerspective3D(CameraId id);

        bool SetActiveCamera(CameraId id);
        CameraId GetActiveCameraId() const { return m_ActiveCameraId; }
        Camera* GetActiveCamera();
        const Camera* GetActiveCamera() const;

        std::optional<CameraId> FindByName(const std::string& name) const;

        size_t GetCameraCount() const { return m_Cameras.size(); }
        std::vector<CameraId> GetAllCameraIds() const;

    private:
        struct CameraIdHasher
        {
            size_t operator()(CameraId id) const noexcept
            {
                return static_cast<size_t>(id.Value);
            }
        };

        CameraId AllocateId();
        void FixupActiveCameraAfterDestroy(CameraId destroyedId);

        uint64_t m_NextId = 1;
        std::unordered_map<CameraId, std::unique_ptr<Camera>, CameraIdHasher> m_Cameras;
        CameraId m_ActiveCameraId{};
    };
}

