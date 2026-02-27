#include "Scene/Scene.h"

#include "Core/ConfigManager.h"
#include "Core/Debug/Log.h"
#include "Scripting/Coroutine.h"

#include <algorithm>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

namespace Limitless
{
    namespace
    {
        constexpr int32_t kSiblingOrderStep = 10;

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
        if (ShouldDeferStructuralMutations())
        {
            const std::string deferredName = name;
            EnqueueDeferredStructuralMutation([deferredName](Scene& scene) {
                scene.CreateEntity(deferredName);
            }, "CreateEntity");
            return entt::null;
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
        if (ShouldDeferStructuralMutations())
        {
            EnqueueDeferredStructuralMutation([entity](Scene& scene) {
                scene.DestroyEntity(entity);
            }, "DestroyEntity");
            return;
        }
        ValidateImmediateStructuralMutationPhase(*this, "DestroyEntity");

        if (!IsValid(entity))
            return;

        const auto children = GetChildren(entity);
        for (entt::entity child : children)
            DestroyEntity(child);

        if (auto* nativeScript = m_Registry.try_get<NativeScriptComponent>(entity))
        {
            for (auto& scriptEntry : nativeScript->Scripts)
            {
                if (scriptEntry.RuntimeInstance)
                {
                    if (scriptEntry.RuntimeInitialized)
                        scriptEntry.RuntimeInstance->OnDestroy();
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

        m_Registry.destroy(entity);
        ResetPhysicsRuntimeState();
        m_TransformsDirty = true;
        m_HierarchyDepthDirty = true;
    }

    bool Scene::IsValid(entt::entity entity) const
    {
        return m_Registry.valid(entity);
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
            const glm::mat4 childLocal = glm::inverse(parentWorld) * childWorldBefore;

            glm::vec3 skew(0.0f);
            glm::vec4 perspective(0.0f);
            glm::quat orientation(1.0f, 0.0f, 0.0f, 0.0f);
            glm::vec3 translation(0.0f);
            glm::vec3 scale(1.0f);
            if (glm::decompose(childLocal, scale, orientation, translation, skew, perspective))
            {
                childTransform->Position = translation;
                childTransform->Rotation = glm::degrees(glm::eulerAngles(orientation));
                childTransform->Scale = scale;
            }
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
}
