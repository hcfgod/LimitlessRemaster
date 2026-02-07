#include "Graphics/Camera/CameraManager.h"

#include "Core/Debug/Log.h"
#include "Graphics/Camera/Camera.h"
#include "Graphics/Camera/OrthographicCamera2D.h"
#include "Graphics/Camera/PerspectiveCamera3D.h"

namespace Limitless
{
    CameraId CameraManager::AllocateId()
    {
        CameraId id{};
        id.Value = m_NextId++;
        if (id.Value == 0)
        {
            // Extremely unlikely, but keep the invariant that 0 is invalid.
            id.Value = m_NextId++;
        }
        return id;
    }

    CameraId CameraManager::CreateOrthographic2D(const Orthographic2DCreateInfo& info)
    {
        CameraId id = AllocateId();

        OrthographicCamera2D::Settings settings{};
        settings.Zoom = info.Zoom;
        settings.NearPlane = info.NearPlane;
        settings.FarPlane = info.FarPlane;

        auto camera = std::make_unique<OrthographicCamera2D>(
            id,
            info.Name,
            info.Usage,
            info.ViewportWidthPixels,
            info.ViewportHeightPixels,
            settings);

        m_Cameras.emplace(id, std::move(camera));

        if (!m_ActiveCameraId)
        {
            m_ActiveCameraId = id;
        }

        return id;
    }

    CameraId CameraManager::CreatePerspective3D(const Perspective3DCreateInfo& info)
    {
        CameraId id = AllocateId();

        PerspectiveCamera3D::Settings settings{};
        settings.FieldOfViewYDegrees = info.FieldOfViewYDegrees;
        settings.NearPlane = info.NearPlane;
        settings.FarPlane = info.FarPlane;

        auto camera = std::make_unique<PerspectiveCamera3D>(
            id,
            info.Name,
            info.Usage,
            info.ViewportWidthPixels,
            info.ViewportHeightPixels,
            settings);

        m_Cameras.emplace(id, std::move(camera));

        if (!m_ActiveCameraId)
        {
            m_ActiveCameraId = id;
        }

        return id;
    }

    bool CameraManager::DestroyCamera(CameraId id)
    {
        if (!id)
        {
            return false;
        }

        const auto it = m_Cameras.find(id);
        if (it == m_Cameras.end())
        {
            return false;
        }

        m_Cameras.erase(it);
        FixupActiveCameraAfterDestroy(id);
        return true;
    }

    void CameraManager::Clear()
    {
        m_Cameras.clear();
        m_ActiveCameraId = {};
    }

    Camera* CameraManager::GetCamera(CameraId id)
    {
        const auto it = m_Cameras.find(id);
        if (it == m_Cameras.end())
        {
            return nullptr;
        }
        return it->second.get();
    }

    const Camera* CameraManager::GetCamera(CameraId id) const
    {
        const auto it = m_Cameras.find(id);
        if (it == m_Cameras.end())
        {
            return nullptr;
        }
        return it->second.get();
    }

    OrthographicCamera2D* CameraManager::GetOrthographic2D(CameraId id)
    {
        return dynamic_cast<OrthographicCamera2D*>(GetCamera(id));
    }

    PerspectiveCamera3D* CameraManager::GetPerspective3D(CameraId id)
    {
        return dynamic_cast<PerspectiveCamera3D*>(GetCamera(id));
    }

    bool CameraManager::SetActiveCamera(CameraId id)
    {
        if (!id)
        {
            return false;
        }

        if (m_Cameras.find(id) == m_Cameras.end())
        {
            return false;
        }

        m_ActiveCameraId = id;
        return true;
    }

    Camera* CameraManager::GetActiveCamera()
    {
        return GetCamera(m_ActiveCameraId);
    }

    const Camera* CameraManager::GetActiveCamera() const
    {
        return GetCamera(m_ActiveCameraId);
    }

    std::optional<CameraId> CameraManager::FindByName(const std::string& name) const
    {
        for (const auto& [id, camera] : m_Cameras)
        {
            if (camera && camera->GetName() == name)
            {
                return id;
            }
        }
        return std::nullopt;
    }

    std::vector<CameraId> CameraManager::GetAllCameraIds() const
    {
        std::vector<CameraId> ids;
        ids.reserve(m_Cameras.size());
        for (const auto& [id, _] : m_Cameras)
        {
            ids.push_back(id);
        }
        return ids;
    }

    void CameraManager::FixupActiveCameraAfterDestroy(CameraId destroyedId)
    {
        if (m_ActiveCameraId != destroyedId)
        {
            return;
        }

        if (m_Cameras.empty())
        {
            m_ActiveCameraId = {};
            return;
        }

        // Pick an arbitrary remaining camera.
        m_ActiveCameraId = m_Cameras.begin()->first;
        LT_CORE_INFO("Active camera was destroyed; switched active camera to '{}'", m_Cameras.begin()->second->GetName());
    }
}

