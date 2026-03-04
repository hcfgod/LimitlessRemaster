#include "Scene/Scene.h"
#include "Scene/Components/ScriptingComponents.h"

#include "Core/ConfigManager.h"
#include "Core/Debug/Log.h"
#include "Scripting/Coroutine.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <type_traits>
#include <unordered_set>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

namespace Limitless
{
    namespace
    {
        constexpr int32_t kSiblingOrderStep = 10;
        constexpr float kParentInverseDeterminantEpsilon = 1e-6f;

        bool IsFiniteMatrix(const glm::mat4& matrix)
        {
            for (int column = 0; column < 4; ++column)
            {
                for (int row = 0; row < 4; ++row)
                {
                    if (!std::isfinite(matrix[column][row]))
                        return false;
                }
            }
            return true;
        }

        bool IsFiniteVec3(const glm::vec3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool IsFiniteQuat(const glm::quat& value)
        {
            return std::isfinite(value.w) && std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool TryAssignLocalTransformFromWorld(const glm::mat4& parentWorld,
                                              const glm::mat4& childWorld,
                                              TransformComponent& childTransform)
        {
            if (!IsFiniteMatrix(parentWorld) || !IsFiniteMatrix(childWorld))
                return false;

            const float determinant = glm::determinant(parentWorld);
            if (!std::isfinite(determinant) || std::abs(determinant) <= kParentInverseDeterminantEpsilon)
                return false;

            const glm::mat4 childLocal = glm::inverse(parentWorld) * childWorld;
            if (!IsFiniteMatrix(childLocal))
                return false;

            glm::vec3 skew(0.0f);
            glm::vec4 perspective(0.0f);
            glm::quat orientation(1.0f, 0.0f, 0.0f, 0.0f);
            glm::vec3 translation(0.0f);
            glm::vec3 scale(1.0f);
            if (!glm::decompose(childLocal, scale, orientation, translation, skew, perspective))
                return false;
            if (!IsFiniteVec3(translation) || !IsFiniteVec3(scale) || !IsFiniteQuat(orientation))
                return false;

            const float orientationLengthSquared =
                orientation.w * orientation.w +
                orientation.x * orientation.x +
                orientation.y * orientation.y +
                orientation.z * orientation.z;
            if (!std::isfinite(orientationLengthSquared) || orientationLengthSquared <= 0.0f)
                return false;
            orientation = glm::normalize(orientation);

            const glm::vec3 eulerDegrees = glm::degrees(glm::eulerAngles(orientation));
            if (!IsFiniteVec3(eulerDegrees))
                return false;

            childTransform.Position = translation;
            childTransform.Rotation = eulerDegrees;
            childTransform.Scale = scale;
            return true;
        }

        bool IsStructuralPhaseValidationEnabled()
        {
            return ConfigManager::GetInstance().GetValue<bool>("ecs.mt.validate_structural_phase", true);
        }

        void ValidateImmediateStructuralMutationPhase(const Scene& scene, const char* operationName)
        {
            if (!IsStructuralPhaseValidationEnabled())
                return;

            // Main-thread script code can legitimately perform structural mutations today.
            // The high-risk path is parallel script execution without deferral.
            if (!Scene::IsCurrentThreadParallelScriptExecution())
                return;

            const Scene::RuntimePhase phase = scene.GetRuntimePhase();
            if (phase == Scene::RuntimePhase::Idle || phase == Scene::RuntimePhase::Structural)
                return;

            if (scene.ShouldDeferStructuralMutations())
                return;

            LT_WARN("Scene structural operation '{}' executed during runtime phase {} (expected Structural phase).",
                    operationName ? operationName : "Unknown",
                    static_cast<uint32_t>(phase));
        }
    }

    entt::entity Scene::CreateEntity(const std::string& name)
    {
        if (m_IsShuttingDown)
            return entt::null;

        if (ShouldDeferStructuralMutations())
        {
            const std::string deferredName = name;
            const entt::entity deferredEntity = AllocateDeferredEntityReference();
            const bool enqueued = EnqueueDeferredStructuralMutation([deferredName, deferredEntity](Scene& scene) {
                const entt::entity createdEntity = scene.CreateEntity(deferredName);
                scene.BindDeferredEntityReference(deferredEntity, createdEntity);
            }, "CreateEntity");
            if (!enqueued)
            {
                ForgetDeferredEntityReference(deferredEntity);
                return entt::null;
            }
            return deferredEntity;
        }
        ValidateImmediateStructuralMutationPhase(*this, "CreateEntity");

        entt::entity entity = m_Registry.create();
        TagComponent tag{};
        tag.Tag = name;
        tag.Enabled = true;
        m_Registry.emplace<TagComponent>(entity, std::move(tag));
        m_Registry.emplace<TransformComponent>(entity);
        auto& hierarchy = m_Registry.emplace<HierarchyComponent>(entity);
        m_TransformsDirty = true;
        m_HierarchyDepthDirty = true;

        int32_t maxSiblingOrder = -kSiblingOrderStep;
        auto hierarchyView = m_Registry.view<HierarchyComponent>();
        for (entt::entity otherEntity : hierarchyView)
        {
            if (otherEntity == entity)
                continue;
            const auto& otherHierarchy = hierarchyView.get<HierarchyComponent>(otherEntity);
            if (otherHierarchy.Parent == entt::null)
                maxSiblingOrder = std::max(maxSiblingOrder, otherHierarchy.SiblingOrder);
        }
        hierarchy.SiblingOrder = maxSiblingOrder + kSiblingOrderStep;
        return entity;
    }

    Entity Scene::CreateEntityWrapped(const std::string& name)
    {
        return Entity(&m_Registry, CreateEntity(name));
    }

    void Scene::DestroyEntity(entt::entity entity)
    {
        if (m_IsShuttingDown)
            return;

        if (IsForcedDeferredEntityDestructionEnabled())
        {
            EnqueueDeferredStructuralMutation([entity](Scene& scene) {
                scene.DestroyEntity(entity);
            }, "DestroyEntity");
            return;
        }

        if (ShouldDeferStructuralMutations())
        {
            EnqueueDeferredStructuralMutation([entity](Scene& scene) {
                scene.DestroyEntity(entity);
            }, "DestroyEntity");
            return;
        }
        ValidateImmediateStructuralMutationPhase(*this, "DestroyEntity");

        entity = ResolveEntityReference(entity);
        if (!IsValid(entity))
            return;

        std::unordered_set<uint32_t> destroyedEntityIds;

        auto destroyRecursive = [&](auto&& self, entt::entity target) -> void
        {
            if (!IsValid(target))
                return;

            const auto children = GetChildren(target);
            for (entt::entity child : children)
                self(self, child);

            if (auto* nativeScript = m_Registry.try_get<NativeScriptComponent>(target))
            {
                for (auto& scriptEntry : nativeScript->Scripts)
                {
                    if (scriptEntry.RuntimeInstance)
                    {
                        if (scriptEntry.RuntimeInitialized)
                        {
                            const auto* tag = m_Registry.try_get<TagComponent>(target);
                            try
                            {
                                scriptEntry.RuntimeInstance->OnDestroy();
                            }
                            catch (const std::exception& exception)
                            {
                                LT_ERROR("Script '{}' on entity '{}' threw during OnDestroy while destroying entity: {}",
                                         scriptEntry.ScriptClassName,
                                         tag ? tag->Tag : "Entity",
                                         exception.what());
                            }
                            catch (...)
                            {
                                LT_ERROR("Script '{}' on entity '{}' threw a non-standard exception during OnDestroy while destroying entity",
                                         scriptEntry.ScriptClassName,
                                         tag ? tag->Tag : "Entity");
                            }
                        }
                        Coroutine::StopAll(*scriptEntry.RuntimeInstance);
                        scriptEntry.RuntimeInstance.reset();
                    }
                    scriptEntry.RuntimeInitialized = false;
                    scriptEntry.RuntimeUpdateCount = 0;
                    scriptEntry.RuntimeWarnedOnUpdateTransformMutation = false;
                    scriptEntry.RuntimeWarnedMissingAccessDeclaration = false;
                    scriptEntry.RuntimeWarnedAccessMaskMismatch = false;
                }
            }

            destroyedEntityIds.insert(static_cast<uint32_t>(target));
            m_Registry.destroy(target);
            RemoveDeferredEntityReferencesFor(target);
        };
        destroyRecursive(destroyRecursive, entity);

        if (!destroyedEntityIds.empty() && !m_RuntimeActiveContactPairs.empty())
        {
            for (auto it = m_RuntimeActiveContactPairs.begin(); it != m_RuntimeActiveContactPairs.end();)
            {
                if (destroyedEntityIds.contains(it->EntityA) || destroyedEntityIds.contains(it->EntityB))
                    it = m_RuntimeActiveContactPairs.erase(it);
                else
                    ++it;
            }
        }

        m_TransformsDirty = true;
        m_HierarchyDepthDirty = true;
    }

    bool Scene::IsValid(entt::entity entity) const
    {
        const entt::entity resolvedEntity = ResolveEntityReference(entity);
        return m_Registry.valid(resolvedEntity);
    }

    bool Scene::IsEntityEnabledInHierarchy(entt::entity entity) const
    {
        if (!IsValid(entity))
            return false;

        entt::entity current = entity;
        while (current != entt::null)
        {
            const auto* tag = m_Registry.try_get<TagComponent>(current);
            if (tag && !tag->Enabled)
                return false;
            current = GetParent(current);
        }
        return true;
    }

    bool Scene::SetParent(entt::entity child, entt::entity parent)
    {
        if (ShouldDeferStructuralMutations())
        {
            EnqueueDeferredStructuralMutation([child, parent](Scene& scene) {
                (void)scene.SetParent(child, parent);
            }, "SetParent");
            return true;
        }
        ValidateImmediateStructuralMutationPhase(*this, "SetParent");

        child = ResolveEntityReference(child);
        parent = ResolveEntityReference(parent);

        if (!IsValid(child))
            return false;

        if (parent != entt::null && !IsValid(parent))
            return false;

        if (child == parent)
            return false;

        // Prevent hierarchy cycles.
        if (parent != entt::null && IsDescendantOf(parent, child))
            return false;

        const glm::mat4 childWorldBefore = GetWorldTransformMatrix(child);

        auto* hierarchy = m_Registry.try_get<HierarchyComponent>(child);
        if (!hierarchy)
            hierarchy = &m_Registry.emplace<HierarchyComponent>(child);

        if (hierarchy->Parent == parent)
            return true;

        hierarchy->Parent = parent;
        int32_t maxSiblingOrder = -kSiblingOrderStep;
        auto hierarchyView = m_Registry.view<HierarchyComponent>();
        for (entt::entity entity : hierarchyView)
        {
            if (entity == child)
                continue;
            const auto& otherHierarchy = hierarchyView.get<HierarchyComponent>(entity);
            if (otherHierarchy.Parent == parent)
                maxSiblingOrder = std::max(maxSiblingOrder, otherHierarchy.SiblingOrder);
        }
        hierarchy->SiblingOrder = maxSiblingOrder + kSiblingOrderStep;

        if (auto* childTransform = m_Registry.try_get<TransformComponent>(child))
        {
            const glm::mat4 parentWorld = (parent != entt::null) ? GetWorldTransformMatrix(parent) : glm::mat4(1.0f);
            (void)TryAssignLocalTransformFromWorld(parentWorld, childWorldBefore, *childTransform);
        }

        MarkTransformDirty(child);
        m_HierarchyDepthDirty = true;

        return true;
    }

    bool Scene::SetSiblingOrderBefore(entt::entity entity, entt::entity targetSibling)
    {
        if (ShouldDeferStructuralMutations())
        {
            EnqueueDeferredStructuralMutation([entity, targetSibling](Scene& scene) {
                (void)scene.SetSiblingOrderBefore(entity, targetSibling);
            }, "SetSiblingOrderBefore");
            return true;
        }
        ValidateImmediateStructuralMutationPhase(*this, "SetSiblingOrderBefore");

        entity = ResolveEntityReference(entity);
        targetSibling = ResolveEntityReference(targetSibling);

        if (!IsValid(entity) || !IsValid(targetSibling) || entity == targetSibling)
            return false;

        const entt::entity targetParent = GetParent(targetSibling);
        if (!SetParent(entity, targetParent))
            return false;

        auto siblings = GetChildren(targetParent);
        siblings.erase(std::remove(siblings.begin(), siblings.end(), entity), siblings.end());
        const auto insertIt = std::find(siblings.begin(), siblings.end(), targetSibling);
        if (insertIt == siblings.end())
            return false;
        siblings.insert(insertIt, entity);

        for (size_t index = 0; index < siblings.size(); ++index)
        {
            auto* hierarchy = m_Registry.try_get<HierarchyComponent>(siblings[index]);
            if (hierarchy)
                hierarchy->SiblingOrder = static_cast<int32_t>(index * kSiblingOrderStep);
        }
        m_HierarchyDepthDirty = true;
        return true;
    }

    bool Scene::SetSiblingOrderAfter(entt::entity entity, entt::entity targetSibling)
    {
        if (ShouldDeferStructuralMutations())
        {
            EnqueueDeferredStructuralMutation([entity, targetSibling](Scene& scene) {
                (void)scene.SetSiblingOrderAfter(entity, targetSibling);
            }, "SetSiblingOrderAfter");
            return true;
        }
        ValidateImmediateStructuralMutationPhase(*this, "SetSiblingOrderAfter");

        entity = ResolveEntityReference(entity);
        targetSibling = ResolveEntityReference(targetSibling);

        if (!IsValid(entity) || !IsValid(targetSibling) || entity == targetSibling)
            return false;

        const entt::entity targetParent = GetParent(targetSibling);
        if (!SetParent(entity, targetParent))
            return false;

        auto siblings = GetChildren(targetParent);
        siblings.erase(std::remove(siblings.begin(), siblings.end(), entity), siblings.end());
        const auto targetIt = std::find(siblings.begin(), siblings.end(), targetSibling);
        if (targetIt == siblings.end())
            return false;
        siblings.insert(std::next(targetIt), entity);

        for (size_t index = 0; index < siblings.size(); ++index)
        {
            auto* hierarchy = m_Registry.try_get<HierarchyComponent>(siblings[index]);
            if (hierarchy)
                hierarchy->SiblingOrder = static_cast<int32_t>(index * kSiblingOrderStep);
        }
        m_HierarchyDepthDirty = true;
        return true;
    }

    entt::entity Scene::GetParent(entt::entity entity) const
    {
        entity = ResolveEntityReference(entity);
        if (!IsValid(entity))
            return entt::null;

        const auto* hierarchy = m_Registry.try_get<HierarchyComponent>(entity);
        if (!hierarchy)
            return entt::null;

        if (!IsValid(hierarchy->Parent))
            return entt::null;

        return hierarchy->Parent;
    }

    bool Scene::IsDescendantOf(entt::entity entity, entt::entity potentialAncestor) const
    {
        entity = ResolveEntityReference(entity);
        potentialAncestor = ResolveEntityReference(potentialAncestor);
        if (!IsValid(entity) || !IsValid(potentialAncestor))
            return false;

        entt::entity current = GetParent(entity);
        while (current != entt::null)
        {
            if (current == potentialAncestor)
                return true;
            current = GetParent(current);
        }

        return false;
    }

    std::vector<entt::entity> Scene::GetChildren(entt::entity parent) const
    {
        parent = ResolveEntityReference(parent);
        std::vector<entt::entity> children;
        if (parent != entt::null && !IsValid(parent))
            return children;

        auto view = m_Registry.view<HierarchyComponent>();
        for (entt::entity entity : view)
        {
            const auto& hierarchy = view.get<HierarchyComponent>(entity);
            if (hierarchy.Parent == parent)
                children.push_back(entity);
        }

        std::sort(children.begin(), children.end(), [this](entt::entity left, entt::entity right) {
            const auto* leftHierarchy = m_Registry.try_get<HierarchyComponent>(left);
            const auto* rightHierarchy = m_Registry.try_get<HierarchyComponent>(right);
            const int32_t leftOrder = leftHierarchy ? leftHierarchy->SiblingOrder : 0;
            const int32_t rightOrder = rightHierarchy ? rightHierarchy->SiblingOrder : 0;
            if (leftOrder != rightOrder)
                return leftOrder < rightOrder;
            return static_cast<uint32_t>(left) < static_cast<uint32_t>(right);
        });

        return children;
    }

    entt::entity Scene::ResolveEntityReference(entt::entity entity) const
    {
        if (entity == entt::null)
            return entt::null;

        std::lock_guard<std::mutex> lock(m_DeferredEntityReferencesMutex);
        const auto found = m_DeferredEntityReferences.find(entity);
        if (found == m_DeferredEntityReferences.end() || found->second == entt::null)
            return entity;
        return found->second;
    }

    entt::entity Scene::AllocateDeferredEntityReference()
    {
        using EntityValueType = std::underlying_type_t<entt::entity>;
        constexpr EntityValueType kDeferredEntityMask = static_cast<EntityValueType>(1)
            << (sizeof(EntityValueType) * 8 - 1);
        constexpr EntityValueType kDeferredEntityValueMask = ~kDeferredEntityMask;
        constexpr EntityValueType kNullEntityValue = static_cast<EntityValueType>(entt::null);

        while (true)
        {
            const uint64_t sequence = m_NextDeferredEntityReferenceSequence.fetch_add(1, std::memory_order_relaxed);
            const EntityValueType rawValue = kDeferredEntityMask
                | (static_cast<EntityValueType>(sequence) & kDeferredEntityValueMask);
            if (rawValue == kNullEntityValue)
                continue;

            const entt::entity deferredEntity = static_cast<entt::entity>(rawValue);

            std::lock_guard<std::mutex> lock(m_DeferredEntityReferencesMutex);
            if (m_DeferredEntityReferences.contains(deferredEntity))
                continue;

            m_DeferredEntityReferences.emplace(deferredEntity, entt::null);
            return deferredEntity;
        }
    }

    void Scene::BindDeferredEntityReference(entt::entity deferredEntity, entt::entity resolvedEntity)
    {
        if (deferredEntity == entt::null)
            return;

        std::lock_guard<std::mutex> lock(m_DeferredEntityReferencesMutex);
        const auto found = m_DeferredEntityReferences.find(deferredEntity);
        if (found == m_DeferredEntityReferences.end())
        {
            m_DeferredEntityReferences.emplace(deferredEntity, resolvedEntity);
            return;
        }

        if (resolvedEntity == entt::null || resolvedEntity == deferredEntity)
        {
            m_DeferredEntityReferences.erase(found);
            return;
        }

        found->second = resolvedEntity;
    }

    void Scene::ForgetDeferredEntityReference(entt::entity deferredEntity)
    {
        if (deferredEntity == entt::null)
            return;

        std::lock_guard<std::mutex> lock(m_DeferredEntityReferencesMutex);
        m_DeferredEntityReferences.erase(deferredEntity);
    }

    void Scene::RemoveDeferredEntityReferencesFor(entt::entity resolvedEntity)
    {
        if (resolvedEntity == entt::null)
            return;

        std::lock_guard<std::mutex> lock(m_DeferredEntityReferencesMutex);
        for (auto iterator = m_DeferredEntityReferences.begin(); iterator != m_DeferredEntityReferences.end();)
        {
            if (iterator->first == resolvedEntity || iterator->second == resolvedEntity)
            {
                iterator = m_DeferredEntityReferences.erase(iterator);
                continue;
            }
            ++iterator;
        }
    }
}
