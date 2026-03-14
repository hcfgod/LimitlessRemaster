#include "PrecompiledHeader.h"
#include "EditorLayer.h"

#include "EditorRuntimeOperations.h"
#include "Graphics/Camera/OrthographicCamera2D.h"
#include "Graphics/Camera/PerspectiveCamera3D.h"
#include "Scene/Components/CoreComponents.h"
#include "Scene/SceneRenderer.h"

#include <cmath>
#include <optional>

namespace Limitless
{

    void EditorLayer::EnsureSceneViewFramebuffer(uint32_t width, uint32_t height)
    {
        EditorRuntimeOperations::EnsureViewportFramebuffer(
            width,
            height,
            m_SceneViewFramebuffer,
            m_SceneViewWidthPixels,
            m_SceneViewHeightPixels,
            m_EditorCameraController.get());
    }

    void EditorLayer::EnsureGameViewFramebuffer(uint32_t width, uint32_t height)
    {
        EditorRuntimeOperations::EnsureViewportFramebuffer(
            width,
            height,
            m_GameViewFramebuffer,
            m_GameViewWidthPixels,
            m_GameViewHeightPixels,
            nullptr);
    }

    Camera* EditorLayer::ResolveGameViewCamera(uint32_t viewportWidthPixels,
                                               uint32_t viewportHeightPixels,
                                               bool& outMissingGameplayCamera)
    {
        outMissingGameplayCamera = false;

        const auto findFirstGameplayCamera = [this]() -> Camera* {
            for (CameraId id : m_CameraManager.GetAllCameraIds())
            {
                Camera* camera = m_CameraManager.GetCamera(id);
                if (camera && camera->GetUsage() == CameraUsage::Gameplay)
                    return camera;
            }
            return nullptr;
        };

        if (m_PlayModeState != EditorPlayModeState::Edit)
        {
            if (Camera* camera = m_CameraManager.GetCamera(m_CachedGameplayCameraId))
            {
                camera->SetViewportSize(viewportWidthPixels, viewportHeightPixels);
                return camera;
            }

            if (Camera* fallbackCamera = findFirstGameplayCamera())
            {
                fallbackCamera->SetViewportSize(viewportWidthPixels, viewportHeightPixels);
                return fallbackCamera;
            }

            outMissingGameplayCamera = true;
            return nullptr;
        }

        if (!m_Scene)
        {
            outMissingGameplayCamera = true;
            return nullptr;
        }

        struct GameplaySceneCameraSelection final
        {
            entt::entity Entity = entt::null;
            CameraComponent Component{};
        };

        const auto findGameplaySceneCamera = [this]() -> std::optional<GameplaySceneCameraSelection> {
            auto& registry = m_Scene->GetRegistry();
            auto view = registry.view<CameraComponent>();
            std::optional<GameplaySceneCameraSelection> fallback{};
            for (entt::entity entity : view)
            {
                const auto& cameraComponent = view.get<CameraComponent>(entity);
                if (!fallback.has_value())
                    fallback = GameplaySceneCameraSelection{ entity, cameraComponent };
                if (cameraComponent.IsPrimary)
                    return GameplaySceneCameraSelection{ entity, cameraComponent };
            }

            return fallback;
        };

        // Edit Mode Game View mirrors Unity/Godot behavior:
        // render from the scene's Primary camera even when runtime is not active.
        const std::optional<GameplaySceneCameraSelection> selection = findGameplaySceneCamera();
        if (!selection.has_value())
        {
            DestroyGameViewPreviewCamera();
            outMissingGameplayCamera = true;
            return nullptr;
        }

        const CameraType expectedType = (selection->Component.Projection == CameraComponent::ProjectionType::Perspective3D)
            ? CameraType::Perspective3D
            : CameraType::Orthographic2D;

        Camera* previewCamera = m_CameraManager.GetCamera(m_GameViewPreviewCameraId);
        if (!previewCamera || previewCamera->GetType() != expectedType || previewCamera->GetUsage() != CameraUsage::Gameplay)
        {
            DestroyGameViewPreviewCamera();

            if (expectedType == CameraType::Perspective3D)
            {
                CameraManager::Perspective3DCreateInfo createInfo{};
                createInfo.Name = "GameView Preview Camera";
                createInfo.Usage = CameraUsage::Gameplay;
                createInfo.ViewportWidthPixels = viewportWidthPixels;
                createInfo.ViewportHeightPixels = viewportHeightPixels;
                createInfo.FieldOfViewYDegrees = selection->Component.FieldOfViewYDegrees;
                if (selection->Component.NearPlane <= 0.0f && selection->Component.FarPlane <= 1.0f)
                {
                    createInfo.NearPlane = 0.1f;
                    createInfo.FarPlane = 1000.0f;
                }
                else
                {
                    createInfo.NearPlane = selection->Component.NearPlane > 0.0f ? selection->Component.NearPlane : 0.01f;
                    createInfo.FarPlane = selection->Component.FarPlane > createInfo.NearPlane
                        ? selection->Component.FarPlane
                        : (createInfo.NearPlane + 1000.0f);
                }
                m_GameViewPreviewCameraId = m_CameraManager.CreatePerspective3D(createInfo);
            }
            else
            {
                CameraManager::Orthographic2DCreateInfo createInfo{};
                createInfo.Name = "GameView Preview Camera";
                createInfo.Usage = CameraUsage::Gameplay;
                createInfo.ViewportWidthPixels = viewportWidthPixels;
                createInfo.ViewportHeightPixels = viewportHeightPixels;
                createInfo.Zoom = selection->Component.Zoom > 0.0f ? selection->Component.Zoom : 1.0f;
                createInfo.NearPlane = selection->Component.NearPlane;
                createInfo.FarPlane = selection->Component.FarPlane > selection->Component.NearPlane
                    ? selection->Component.FarPlane
                    : (selection->Component.NearPlane + 2.0f);
                m_GameViewPreviewCameraId = m_CameraManager.CreateOrthographic2D(createInfo);
            }

            previewCamera = m_CameraManager.GetCamera(m_GameViewPreviewCameraId);
        }

        if (!previewCamera)
        {
            outMissingGameplayCamera = true;
            return nullptr;
        }

        previewCamera->SetViewportSize(viewportWidthPixels, viewportHeightPixels);
        if (selection->Component.Projection == CameraComponent::ProjectionType::Orthographic2D)
        {
            auto* orthographicCamera = m_CameraManager.GetOrthographic2D(m_GameViewPreviewCameraId);
            if (!orthographicCamera)
            {
                outMissingGameplayCamera = true;
                return nullptr;
            }

            const float zoom = selection->Component.Zoom > 0.0f ? selection->Component.Zoom : 1.0f;
            const float nearPlane = selection->Component.NearPlane;
            const float farPlane = selection->Component.FarPlane > nearPlane
                ? selection->Component.FarPlane
                : (nearPlane + 2.0f);
            orthographicCamera->SetProjection(zoom, nearPlane, farPlane);
        }
        else
        {
            auto* perspectiveCamera = m_CameraManager.GetPerspective3D(m_GameViewPreviewCameraId);
            if (!perspectiveCamera)
            {
                outMissingGameplayCamera = true;
                return nullptr;
            }

            const float fieldOfViewY = selection->Component.FieldOfViewYDegrees > 1.0f ? selection->Component.FieldOfViewYDegrees : 60.0f;
            float nearPlane = selection->Component.NearPlane > 0.0f ? selection->Component.NearPlane : 0.01f;
            float farPlane = selection->Component.FarPlane > nearPlane ? selection->Component.FarPlane : nearPlane + 1000.0f;
            if (selection->Component.NearPlane <= 0.0f && selection->Component.FarPlane <= 1.0f)
            {
                nearPlane = 0.1f;
                farPlane = 1000.0f;
            }
            perspectiveCamera->SetPerspective(fieldOfViewY, nearPlane, farPlane);
        }

        SceneRenderer::SetActiveCullingMask(selection->Component.CullingMask);

        const glm::mat4 worldTransform = m_Scene->GetWorldTransformMatrix(selection->Entity);
        const glm::vec3 position = glm::vec3(worldTransform[3]);
        if (auto* orthographicCamera = m_CameraManager.GetOrthographic2D(m_GameViewPreviewCameraId))
        {
            orthographicCamera->SetPosition(position);
            const float rotationRadians = std::atan2(worldTransform[1][0], worldTransform[0][0]);
            orthographicCamera->SetRotationRadians(rotationRadians);
        }
        else if (auto* perspectiveCamera = m_CameraManager.GetPerspective3D(m_GameViewPreviewCameraId))
        {
            perspectiveCamera->SetPosition(position);
            const glm::vec3 forward = glm::normalize(glm::vec3(worldTransform * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
            const float yawDegrees = glm::degrees(std::atan2(forward.z, forward.x));
            const float pitchDegrees = glm::degrees(std::asin(glm::clamp(forward.y, -1.0f, 1.0f)));
            perspectiveCamera->SetYawPitchDegrees(yawDegrees, pitchDegrees);
        }

        return previewCamera;
    }

    void EditorLayer::DestroyGameViewPreviewCamera()
    {
        if (m_GameViewPreviewCameraId)
        {
            (void)m_CameraManager.DestroyCamera(m_GameViewPreviewCameraId);
            m_GameViewPreviewCameraId = {};
        }
    }
}

