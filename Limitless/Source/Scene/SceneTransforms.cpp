#include "Scene/Scene.h"

#include <algorithm>
#include <vector>

#include <glm/gtc/constants.hpp>

namespace Limitless
{
    namespace
    {
        float WrapAngleRadians(float angleRadians)
        {
            while (angleRadians > glm::pi<float>())
                angleRadians -= glm::two_pi<float>();
            while (angleRadians < -glm::pi<float>())
                angleRadians += glm::two_pi<float>();
            return angleRadians;
        }
    }

    void Scene::MarkTransformDirty(entt::entity entity)
    {
        (void)entity;
        m_TransformsDirty = true;
    }

    void Scene::UpdateTransforms()
    {
        auto transformView = m_Registry.view<TransformComponent>();
        if (!m_TransformsDirty)
        {
            bool hasDirtyTransform = false;
            for (entt::entity entity : transformView)
            {
                if (transformView.get<TransformComponent>(entity).Dirty)
                {
                    hasDirtyTransform = true;
                    break;
                }
            }

            if (!hasDirtyTransform)
                return;
            m_TransformsDirty = true;
        }

        std::vector<entt::entity> roots;
        for (entt::entity entity : transformView)
        {
            const auto* hierarchy = m_Registry.try_get<HierarchyComponent>(entity);
            if (!hierarchy ||
                hierarchy->Parent == entt::null ||
                !IsValid(hierarchy->Parent) ||
                !m_Registry.all_of<TransformComponent>(hierarchy->Parent))
                roots.push_back(entity);
        }

        std::sort(roots.begin(), roots.end(), [this](entt::entity left, entt::entity right) {
            const auto* leftHierarchy = m_Registry.try_get<HierarchyComponent>(left);
            const auto* rightHierarchy = m_Registry.try_get<HierarchyComponent>(right);
            const int32_t leftOrder = leftHierarchy ? leftHierarchy->SiblingOrder : 0;
            const int32_t rightOrder = rightHierarchy ? rightHierarchy->SiblingOrder : 0;
            if (leftOrder != rightOrder)
                return leftOrder < rightOrder;
            return static_cast<uint32_t>(left) < static_cast<uint32_t>(right);
        });

        auto updateSubtree = [&](auto&& self, entt::entity entity, const glm::mat4& parentWorldTransform) -> void {
            if (!IsValid(entity))
                return;

            glm::mat4 entityWorldTransform = parentWorldTransform;
            if (auto* transform = m_Registry.try_get<TransformComponent>(entity))
            {
                transform->WorldTransform = parentWorldTransform * transform->GetLocalMatrix();
                transform->Dirty = false;
                entityWorldTransform = transform->WorldTransform;
            }

            const auto children = GetChildren(entity);
            for (entt::entity child : children)
                self(self, child, entityWorldTransform);
        };

        for (entt::entity root : roots)
            updateSubtree(updateSubtree, root, glm::mat4(1.0f));

        m_TransformsDirty = false;
    }

    glm::mat4 Scene::GetWorldTransformMatrix(entt::entity entity) const
    {
        if (!IsValid(entity))
            return glm::mat4(1.0f);

        if (m_TransformsDirty)
            const_cast<Scene*>(this)->UpdateTransforms();

        const auto* transform = m_Registry.try_get<TransformComponent>(entity);
        if (transform)
            return transform->WorldTransform;

        entt::entity parent = GetParent(entity);
        while (parent != entt::null)
        {
            const auto* parentTransform = m_Registry.try_get<TransformComponent>(parent);
            if (parentTransform)
                return parentTransform->WorldTransform;
            parent = GetParent(parent);
        }
        return glm::mat4(1.0f);
    }

    glm::mat4 Scene::GetWorldTransformMatrixForRendering(entt::entity entity, float interpolationAlpha) const
    {
        if (!IsValid(entity))
            return glm::mat4(1.0f);

        if (m_TransformsDirty)
            const_cast<Scene*>(this)->UpdateTransforms();

        const float alpha = std::clamp(interpolationAlpha, 0.0f, 1.0f);
        std::vector<entt::entity> chain;
        entt::entity current = entity;
        while (current != entt::null && IsValid(current))
        {
            chain.push_back(current);
            current = GetParent(current);
        }

        bool needsKinematicInterpolation = false;
        for (entt::entity chainedEntity : chain)
        {
            const auto* rigidbody = m_Registry.try_get<Rigidbody2DComponent>(chainedEntity);
            if (!rigidbody)
                continue;
            if (!rigidbody->RuntimeBodyCreated || !rigidbody->Interpolate)
                continue;
            if (rigidbody->Type != Rigidbody2DComponent::BodyType::Kinematic)
                continue;
            needsKinematicInterpolation = true;
            break;
        }

        if (!needsKinematicInterpolation)
        {
            const auto* transform = m_Registry.try_get<TransformComponent>(entity);
            if (transform)
                return transform->WorldTransform;

            entt::entity parent = GetParent(entity);
            while (parent != entt::null)
            {
                const auto* parentTransform = m_Registry.try_get<TransformComponent>(parent);
                if (parentTransform)
                    return parentTransform->WorldTransform;
                parent = GetParent(parent);
            }
            return glm::mat4(1.0f);
        }

        glm::mat4 worldMatrix(1.0f);
        for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        {
            const auto* transform = m_Registry.try_get<TransformComponent>(*it);
            if (!transform)
                continue;

            TransformComponent localForRender = *transform;
            const auto* rigidbody = m_Registry.try_get<Rigidbody2DComponent>(*it);
            if (rigidbody &&
                rigidbody->RuntimeBodyCreated &&
                rigidbody->Interpolate &&
                rigidbody->Type == Rigidbody2DComponent::BodyType::Kinematic)
            {
                localForRender.Position.x = glm::mix(rigidbody->RuntimeRenderPreviousPosition.x, rigidbody->RuntimeRenderCurrentPosition.x, alpha);
                localForRender.Position.y = glm::mix(rigidbody->RuntimeRenderPreviousPosition.y, rigidbody->RuntimeRenderCurrentPosition.y, alpha);
                const float angleDelta = WrapAngleRadians(rigidbody->RuntimeRenderCurrentAngleRadians - rigidbody->RuntimeRenderPreviousAngleRadians);
                localForRender.Rotation.z = glm::degrees(rigidbody->RuntimeRenderPreviousAngleRadians + angleDelta * alpha);
            }

            worldMatrix *= localForRender.GetLocalMatrix();
        }

        return worldMatrix;
    }
}
