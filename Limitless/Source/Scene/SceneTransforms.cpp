#include "Scene/Scene.h"
#include "Scene/Components/PhysicsComponents.h"
#include "Scene/Components/RenderingComponents.h"
#include "Core/Concurrency/JobSystem.h"
#include "Core/ConfigManager.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <unordered_map>
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
        if (!IsValid(entity))
            return;

        std::vector<entt::entity> stack;
        stack.push_back(entity);
        while (!stack.empty())
        {
            const entt::entity current = stack.back();
            stack.pop_back();

            if (!IsValid(current))
                continue;

            bool alreadyDirty = false;
            if (auto* transform = m_Registry.try_get<TransformComponent>(current))
            {
                alreadyDirty = transform->Dirty;
                transform->Dirty = true;
                if (current == entity)
                    transform->LocalDirty = true;
            }

            // Avoid repeatedly re-propagating unchanged dirty subtrees.
            if (alreadyDirty)
                continue;

            const auto children = GetChildren(current);
            stack.insert(stack.end(), children.begin(), children.end());
        }

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
                const auto& transform = transformView.get<TransformComponent>(entity);
                const bool localStateChanged = transform.Position != transform.CachedLocalPosition ||
                                               transform.Rotation != transform.CachedLocalRotation ||
                                               transform.Scale != transform.CachedLocalScale;
                if (transform.Dirty || transform.LocalDirty || localStateChanged)
                {
                    hasDirtyTransform = true;
                    break;
                }
            }

            if (!hasDirtyTransform)
                return;
            m_TransformsDirty = true;
        }

        if (m_HierarchyDepthDirty)
        {
            const size_t transformCountEstimate = m_Registry.storage<TransformComponent>().size();
            std::vector<entt::entity> roots;
            roots.reserve(transformCountEstimate);

            // Build a deterministic adjacency map for entities with transforms.
            std::unordered_map<uint32_t, std::vector<entt::entity>> childrenByParent;
            childrenByParent.reserve(transformCountEstimate);
            for (entt::entity entity : transformView)
            {
                auto* hierarchy = m_Registry.try_get<HierarchyComponent>(entity);
                if (!hierarchy)
                    hierarchy = &m_Registry.emplace<HierarchyComponent>(entity);
                hierarchy->HierarchyDepth = 0;

                if (hierarchy->Parent == entt::null ||
                    !IsValid(hierarchy->Parent) ||
                    !m_Registry.all_of<TransformComponent>(hierarchy->Parent))
                {
                    roots.push_back(entity);
                    continue;
                }

                childrenByParent[static_cast<uint32_t>(hierarchy->Parent)].push_back(entity);
            }

            auto sortBySiblingOrder = [this](std::vector<entt::entity>& entities) {
                std::sort(entities.begin(), entities.end(), [this](entt::entity left, entt::entity right) {
                    const auto* leftHierarchy = m_Registry.try_get<HierarchyComponent>(left);
                    const auto* rightHierarchy = m_Registry.try_get<HierarchyComponent>(right);
                    const int32_t leftOrder = leftHierarchy ? leftHierarchy->SiblingOrder : 0;
                    const int32_t rightOrder = rightHierarchy ? rightHierarchy->SiblingOrder : 0;
                    if (leftOrder != rightOrder)
                        return leftOrder < rightOrder;
                    return static_cast<uint32_t>(left) < static_cast<uint32_t>(right);
                });
            };

            sortBySiblingOrder(roots);
            for (auto& [parent, children] : childrenByParent)
            {
                (void)parent;
                sortBySiblingOrder(children);
            }

            m_MaxHierarchyDepth = 0;
            std::deque<entt::entity> queue;
            queue.insert(queue.end(), roots.begin(), roots.end());
            while (!queue.empty())
            {
                const entt::entity current = queue.front();
                queue.pop_front();
                const auto* currentHierarchy = m_Registry.try_get<HierarchyComponent>(current);
                const uint16_t parentDepth = currentHierarchy ? currentHierarchy->HierarchyDepth : 0;

                const auto foundChildren = childrenByParent.find(static_cast<uint32_t>(current));
                if (foundChildren == childrenByParent.end())
                    continue;

                const uint16_t childDepth = parentDepth >= std::numeric_limits<uint16_t>::max()
                    ? std::numeric_limits<uint16_t>::max()
                    : static_cast<uint16_t>(parentDepth + 1);
                for (entt::entity child : foundChildren->second)
                {
                    auto* childHierarchy = m_Registry.try_get<HierarchyComponent>(child);
                    if (!childHierarchy)
                        continue;
                    childHierarchy->HierarchyDepth = childDepth;
                    m_MaxHierarchyDepth = std::max(m_MaxHierarchyDepth, childDepth);
                    queue.push_back(child);
                }
            }

            m_HierarchyDepthDirty = false;
        }

        std::vector<std::vector<entt::entity>> depthBuckets(static_cast<size_t>(m_MaxHierarchyDepth) + 1);
        for (entt::entity entity : transformView)
        {
            auto* hierarchy = m_Registry.try_get<HierarchyComponent>(entity);
            if (!hierarchy)
                hierarchy = &m_Registry.emplace<HierarchyComponent>(entity);
            const uint16_t depth = hierarchy->HierarchyDepth;
            if (depth >= depthBuckets.size())
                depthBuckets.resize(static_cast<size_t>(depth) + 1);
            depthBuckets[depth].push_back(entity);
        }

        for (entt::entity entity : transformView)
        {
            auto& transform = transformView.get<TransformComponent>(entity);
            transform.RuntimeWorldUpdatedThisFrame = false;
        }

        auto& jobSystem = Concurrency::GetJobSystem();
        const bool enableParallelTransforms = ConfigManager::GetInstance().GetValue<bool>("ecs.mt.enable_parallel_transforms", true);
        constexpr size_t kDepthBatchGrain = 64;
        for (size_t depthIndex = 0; depthIndex < depthBuckets.size(); ++depthIndex)
        {
            auto& depthEntities = depthBuckets[depthIndex];
            if (depthEntities.empty())
                continue;

            auto solveTransformAtIndex = [this, &depthEntities](size_t index) {
                const entt::entity entity = depthEntities[index];
                if (!IsValid(entity))
                    return;

                auto* transform = m_Registry.try_get<TransformComponent>(entity);
                if (!transform)
                    return;

                glm::mat4 parentWorld = glm::mat4(1.0f);
                bool parentUpdatedThisFrame = false;
                if (const auto* hierarchy = m_Registry.try_get<HierarchyComponent>(entity))
                {
                    if (hierarchy->Parent != entt::null &&
                        IsValid(hierarchy->Parent) &&
                        m_Registry.all_of<TransformComponent>(hierarchy->Parent))
                    {
                        const auto& parentTransform = m_Registry.get<TransformComponent>(hierarchy->Parent);
                        parentWorld = parentTransform.WorldTransform;
                        parentUpdatedThisFrame = parentTransform.RuntimeWorldUpdatedThisFrame;
                    }
                }

                const bool localStateChanged = transform->Position != transform->CachedLocalPosition ||
                                               transform->Rotation != transform->CachedLocalRotation ||
                                               transform->Scale != transform->CachedLocalScale;
                const bool localWasDirty = transform->LocalDirty || localStateChanged;
                if (localWasDirty)
                {
                    transform->LocalTransform = transform->GetLocalMatrix();
                    transform->LocalDirty = false;
                    transform->CachedLocalPosition = transform->Position;
                    transform->CachedLocalRotation = transform->Rotation;
                    transform->CachedLocalScale = transform->Scale;
                }

                const bool shouldUpdateWorld = parentUpdatedThisFrame || transform->Dirty || localWasDirty;
                if (shouldUpdateWorld)
                {
                    transform->WorldTransform = parentWorld * transform->LocalTransform;
                    transform->Dirty = false;
                }
                transform->RuntimeWorldUpdatedThisFrame = shouldUpdateWorld;
            };

            if (enableParallelTransforms && jobSystem.IsInitialized() && depthEntities.size() > 1)
                jobSystem.ParallelFor(0, depthEntities.size(), kDepthBatchGrain, solveTransformAtIndex);
            else
            {
                for (size_t entityIndex = 0; entityIndex < depthEntities.size(); ++entityIndex)
                    solveTransformAtIndex(entityIndex);
            }
        }

        m_TransformsDirty = false;
    }

    glm::mat4 Scene::GetWorldTransformMatrix(entt::entity entity) const
    {
        if (!IsValid(entity))
            return glm::mat4(1.0f);

        if (m_TransformsDirty)
        {
            thread_local std::vector<entt::entity> chain;
            chain.clear();
            entt::entity current = entity;
            while (current != entt::null && IsValid(current))
            {
                chain.push_back(current);
                current = GetParent(current);
            }

            glm::mat4 worldMatrix(1.0f);
            bool hasTransformInChain = false;
            for (auto chainIt = chain.rbegin(); chainIt != chain.rend(); ++chainIt)
            {
                const auto* transform = m_Registry.try_get<TransformComponent>(*chainIt);
                if (!transform)
                    continue;
                worldMatrix *= transform->GetLocalMatrix();
                hasTransformInChain = true;
            }
            return hasTransformInChain ? worldMatrix : glm::mat4(1.0f);
        }

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

        const float alpha = std::clamp(interpolationAlpha, 0.0f, 1.0f);
        thread_local std::vector<entt::entity> chain;
        chain.clear();
        entt::entity current = entity;
        while (current != entt::null && IsValid(current))
        {
            chain.push_back(current);
            current = GetParent(current);
        }
        if (chain.empty())
            return glm::mat4(1.0f);

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
            if (!m_TransformsDirty)
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
            }

            glm::mat4 worldMatrix(1.0f);
            bool hasTransformInChain = false;
            for (auto chainIt = chain.rbegin(); chainIt != chain.rend(); ++chainIt)
            {
                const auto* transform = m_Registry.try_get<TransformComponent>(*chainIt);
                if (!transform)
                    continue;
                worldMatrix *= transform->GetLocalMatrix();
                hasTransformInChain = true;
            }
            return hasTransformInChain ? worldMatrix : glm::mat4(1.0f);
        }

        glm::mat4 worldMatrix(1.0f);
        for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        {
            const auto* transform = m_Registry.try_get<TransformComponent>(*it);
            if (!transform)
                continue;

            TransformComponent localForRender = *transform;
            const auto* rigidbody = m_Registry.try_get<Rigidbody2DComponent>(*it);
            const auto* animator = m_Registry.try_get<AnimatorComponent>(*it);
            const bool animatorOverridesKinematic2DPose =
                animator &&
                animator->ApplyToTransform &&
                (animator->RuntimeAppliedPositionOffset.x != 0.0f ||
                 animator->RuntimeAppliedPositionOffset.y != 0.0f ||
                 animator->RuntimeAppliedRotationOffset.z != 0.0f);
            bool hasKinematicInterpolationDelta = false;
            if (rigidbody)
            {
                const float angleDelta = WrapAngleRadians(rigidbody->RuntimeRenderCurrentAngleRadians - rigidbody->RuntimeRenderPreviousAngleRadians);
                hasKinematicInterpolationDelta =
                    glm::distance(rigidbody->RuntimeRenderPreviousPosition, rigidbody->RuntimeRenderCurrentPosition) > 0.0001f ||
                    std::abs(angleDelta) > 0.0001f;
            }
            if (rigidbody &&
                rigidbody->RuntimeBodyCreated &&
                rigidbody->Interpolate &&
                rigidbody->Type == Rigidbody2DComponent::BodyType::Kinematic &&
                hasKinematicInterpolationDelta &&
                !animatorOverridesKinematic2DPose)
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
