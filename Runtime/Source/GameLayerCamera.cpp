#include "GameLayer.h"

#include "Graphics/Camera/Camera.h"
#include "Graphics/Camera/CameraManager.h"
#include "Scene/Components/RenderingComponents.h"
#include "Scene/Scene.h"
#include "Scene/SceneRenderer.h"

#include <cmath>

namespace Limitless
{
    void GameLayer::ResetGameplayCameraState()
    {
        if (m_GameplayCameraId)
        {
            (void)m_CameraManager.DestroyCamera(m_GameplayCameraId);
            m_GameplayCameraId = {};
        }

        m_LoggedMissingGameplayCamera = false;
    }

    void GameLayer::RenderLoadedScenes(Camera& camera)
    {
        bool firstScene = true;
        for (const SceneCollection::Handle handle : m_SceneCollection.CollectHandlesWithRoles(ToSceneRoleMask(SceneRole::Render)))
        {
            Scene* scene = m_SceneCollection.GetScene(handle);
            if (!scene || !scene->IsReady())
                continue;

            SceneRenderer::RenderToViewport(*scene, camera, {}, m_ViewportWidth, m_ViewportHeight, firstScene);
            firstScene = false;
        }
    }

    Camera* GameLayer::ResolveGameplayCamera()
    {
        if (!m_Scene)
            return nullptr;

        auto& registry = m_Scene->GetRegistry();
        auto cameraView = registry.view<CameraComponent>();

        entt::entity selectedCameraEntity = entt::null;
        CameraComponent selectedCameraComponent{};
        bool hasSelection = false;
        for (auto entity : cameraView)
        {
            const auto& cameraComponent = cameraView.get<CameraComponent>(entity);
            if (!hasSelection)
            {
                selectedCameraEntity = entity;
                selectedCameraComponent = cameraComponent;
                hasSelection = true;
            }

            if (cameraComponent.IsPrimary)
            {
                selectedCameraEntity = entity;
                selectedCameraComponent = cameraComponent;
                hasSelection = true;
                break;
            }
        }

        if (!hasSelection)
            return nullptr;

        const CameraType expectedType = (selectedCameraComponent.Projection == CameraComponent::ProjectionType::Perspective3D)
            ? CameraType::Perspective3D
            : CameraType::Orthographic2D;

        Camera* gameplayCamera = m_CameraManager.GetCamera(m_GameplayCameraId);
        if (!gameplayCamera || gameplayCamera->GetType() != expectedType || gameplayCamera->GetUsage() != CameraUsage::Gameplay)
        {
            if (m_GameplayCameraId)
            {
                (void)m_CameraManager.DestroyCamera(m_GameplayCameraId);
                m_GameplayCameraId = {};
            }

            if (expectedType == CameraType::Orthographic2D)
            {
                CameraManager::Orthographic2DCreateInfo info{};
                info.Name = "GameplayCamera";
                info.Usage = CameraUsage::Gameplay;
                info.ViewportWidthPixels = m_ViewportWidth;
                info.ViewportHeightPixels = m_ViewportHeight;
                info.Zoom = selectedCameraComponent.Zoom > 0.0f ? selectedCameraComponent.Zoom : 1.0f;
                info.NearPlane = selectedCameraComponent.NearPlane;
                info.FarPlane = selectedCameraComponent.FarPlane > selectedCameraComponent.NearPlane
                    ? selectedCameraComponent.FarPlane
                    : (selectedCameraComponent.NearPlane + 2.0f);
                m_GameplayCameraId = m_CameraManager.CreateOrthographic2D(info);
            }
            else
            {
                CameraManager::Perspective3DCreateInfo info{};
                info.Name = "GameplayCamera";
                info.Usage = CameraUsage::Gameplay;
                info.ViewportWidthPixels = m_ViewportWidth;
                info.ViewportHeightPixels = m_ViewportHeight;
                info.FieldOfViewYDegrees = selectedCameraComponent.FieldOfViewYDegrees > 1.0f
                    ? selectedCameraComponent.FieldOfViewYDegrees
                    : 60.0f;
                info.NearPlane = selectedCameraComponent.NearPlane > 0.0f ? selectedCameraComponent.NearPlane : 0.01f;
                info.FarPlane = selectedCameraComponent.FarPlane > info.NearPlane
                    ? selectedCameraComponent.FarPlane
                    : (info.NearPlane + 1000.0f);
                if (selectedCameraComponent.NearPlane <= 0.0f && selectedCameraComponent.FarPlane <= 1.0f)
                {
                    info.NearPlane = 0.1f;
                    info.FarPlane = 1000.0f;
                }
                m_GameplayCameraId = m_CameraManager.CreatePerspective3D(info);
            }

            m_CameraManager.SetActiveCamera(m_GameplayCameraId);
            gameplayCamera = m_CameraManager.GetCamera(m_GameplayCameraId);
        }

        if (!gameplayCamera)
            return nullptr;

        gameplayCamera->SetViewportSize(m_ViewportWidth, m_ViewportHeight);
        const glm::mat4 worldTransform = m_Scene->GetWorldTransformMatrix(selectedCameraEntity);
        const glm::vec3 position = glm::vec3(worldTransform[3]);

        if (auto* orthographicCamera = m_CameraManager.GetOrthographic2D(m_GameplayCameraId))
        {
            const float zoom = selectedCameraComponent.Zoom > 0.0f ? selectedCameraComponent.Zoom : 1.0f;
            const float nearPlane = selectedCameraComponent.NearPlane;
            const float farPlane = selectedCameraComponent.FarPlane > nearPlane
                ? selectedCameraComponent.FarPlane
                : (nearPlane + 2.0f);
            orthographicCamera->SetProjection(zoom, nearPlane, farPlane);
            orthographicCamera->SetPosition(position);

            const float rotationRadians = std::atan2(worldTransform[1][0], worldTransform[0][0]);
            orthographicCamera->SetRotationRadians(rotationRadians);
            return orthographicCamera;
        }

        if (auto* perspectiveCamera = m_CameraManager.GetPerspective3D(m_GameplayCameraId))
        {
            const float fieldOfViewY = selectedCameraComponent.FieldOfViewYDegrees > 1.0f
                ? selectedCameraComponent.FieldOfViewYDegrees
                : 60.0f;
            float nearPlane = selectedCameraComponent.NearPlane > 0.0f ? selectedCameraComponent.NearPlane : 0.01f;
            float farPlane = selectedCameraComponent.FarPlane > nearPlane
                ? selectedCameraComponent.FarPlane
                : (nearPlane + 1000.0f);
            if (selectedCameraComponent.NearPlane <= 0.0f && selectedCameraComponent.FarPlane <= 1.0f)
            {
                nearPlane = 0.1f;
                farPlane = 1000.0f;
            }

            perspectiveCamera->SetPerspective(fieldOfViewY, nearPlane, farPlane);
            perspectiveCamera->SetPosition(position);

            const glm::vec3 forward = glm::normalize(glm::vec3(worldTransform * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
            const float yawDegrees = glm::degrees(std::atan2(forward.z, forward.x));
            const float pitchDegrees = glm::degrees(std::asin(glm::clamp(forward.y, -1.0f, 1.0f)));
            perspectiveCamera->SetYawPitchDegrees(yawDegrees, pitchDegrees);
            return perspectiveCamera;
        }

        return gameplayCamera;
    }
}
