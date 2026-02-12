#include "Scene/Scene.h"
#include "Assets/AssetManager.h"
#include "Assets/TextureAsset.h"
#include "Graphics/Camera/Camera.h"
#include "Graphics/Framebuffer.h"
#include "Graphics/RenderCommand.h"
#include "Graphics/Renderer.h"
#include "Graphics/Renderer2D.h"

#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <unordered_map>

namespace Limitless
{
    namespace
    {
        constexpr int32_t kSiblingOrderStep = 10;
    }

    Scene::Scene() = default;

    entt::entity Scene::CreateEntity(const std::string& name)
    {
        entt::entity entity = m_Registry.create();
        m_Registry.emplace<TagComponent>(entity, TagComponent{ name });
        m_Registry.emplace<TransformComponent>(entity);
        auto& hierarchy = m_Registry.emplace<HierarchyComponent>(entity);

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

    void Scene::DestroyEntity(entt::entity entity)
    {
        if (!IsValid(entity))
            return;

        const auto children = GetChildren(entity);
        for (entt::entity child : children)
            DestroyEntity(child);

        m_Registry.destroy(entity);
    }

    bool Scene::IsValid(entt::entity entity) const
    {
        return m_Registry.valid(entity);
    }

    bool Scene::SetParent(entt::entity child, entt::entity parent)
    {
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

        return true;
    }

    bool Scene::SetSiblingOrderBefore(entt::entity entity, entt::entity targetSibling)
    {
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
        return true;
    }

    bool Scene::SetSiblingOrderAfter(entt::entity entity, entt::entity targetSibling)
    {
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

    glm::mat4 Scene::GetWorldTransformMatrix(entt::entity entity) const
    {
        if (!IsValid(entity))
            return glm::mat4(1.0f);

        std::vector<entt::entity> chain;
        entt::entity current = entity;
        while (current != entt::null && IsValid(current))
        {
            chain.push_back(current);
            current = GetParent(current);
        }

        glm::mat4 worldMatrix(1.0f);
        for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        {
            const auto* transform = m_Registry.try_get<TransformComponent>(*it);
            if (transform)
                worldMatrix *= transform->GetLocalMatrix();
        }

        return worldMatrix;
    }

    std::unique_ptr<Scene> Scene::Clone() const
    {
        auto clone = std::make_unique<Scene>();
        const auto& sourceRegistry = GetRegistry();
        auto& destinationRegistry = clone->GetRegistry();
        std::unordered_map<entt::entity, entt::entity> entityMap;

        auto view = sourceRegistry.view<TagComponent, TransformComponent>();
        for (entt::entity sourceEntity : view)
        {
            const auto& tag = view.get<TagComponent>(sourceEntity);
            const auto& transform = view.get<TransformComponent>(sourceEntity);

            // CreateEntity ensures default baseline components are initialized first.
            entt::entity destinationEntity = clone->CreateEntity(tag.Tag);
            entityMap.emplace(sourceEntity, destinationEntity);
            destinationRegistry.replace<TransformComponent>(destinationEntity, transform);

            if (const auto* sprite = sourceRegistry.try_get<SpriteComponent>(sourceEntity))
            {
                auto& destinationSprite = destinationRegistry.emplace<SpriteComponent>(destinationEntity);
                destinationSprite.TextureKey = sprite->TextureKey;
                destinationSprite.CachedTexture.reset();
                destinationSprite.Color = sprite->Color;
            }
        }

        for (const auto& [sourceEntity, destinationEntity] : entityMap)
        {
            const auto* sourceHierarchy = sourceRegistry.try_get<HierarchyComponent>(sourceEntity);
            if (!sourceHierarchy)
                continue;

            entt::entity destinationParent = entt::null;
            if (sourceHierarchy->Parent != entt::null)
            {
                auto foundParent = entityMap.find(sourceHierarchy->Parent);
                if (foundParent != entityMap.end())
                    destinationParent = foundParent->second;
            }
            clone->SetParent(destinationEntity, destinationParent);
            if (auto* destinationHierarchy = destinationRegistry.try_get<HierarchyComponent>(destinationEntity))
                destinationHierarchy->SiblingOrder = sourceHierarchy->SiblingOrder;
        }

        return clone;
    }

    void SceneRenderer::Render(Scene& scene, const Camera& camera)
    {
        Renderer2D::BeginScene(camera);

        auto& registry = scene.GetRegistry();
        auto view = registry.view<TransformComponent, SpriteComponent>();
        std::vector<entt::entity> renderEntities;
        renderEntities.reserve(view.size_hint());
        for (entt::entity entity : view)
            renderEntities.push_back(entity);

        std::sort(renderEntities.begin(), renderEntities.end(), [&scene, &registry](entt::entity left, entt::entity right) {
            const glm::mat4 leftWorld = scene.GetWorldTransformMatrix(left);
            const glm::mat4 rightWorld = scene.GetWorldTransformMatrix(right);
            const float leftZ = leftWorld[3].z;
            const float rightZ = rightWorld[3].z;
            if (leftZ != rightZ)
                return leftZ < rightZ; // Larger Z draws later (on top) in painter's algorithm.

            const auto* leftHierarchy = registry.try_get<HierarchyComponent>(left);
            const auto* rightHierarchy = registry.try_get<HierarchyComponent>(right);
            const int32_t leftOrder = leftHierarchy ? leftHierarchy->SiblingOrder : 0;
            const int32_t rightOrder = rightHierarchy ? rightHierarchy->SiblingOrder : 0;
            if (leftOrder != rightOrder)
                return leftOrder < rightOrder;

            return static_cast<uint32_t>(left) < static_cast<uint32_t>(right);
        });

        for (entt::entity entity : renderEntities)
        {
            auto& sprite = registry.get<SpriteComponent>(entity);

            glm::mat4 model = scene.GetWorldTransformMatrix(entity);
            if (!sprite.TextureKey.empty())
            {
                if (!sprite.CachedTexture)
                {
                    auto tex = std::dynamic_pointer_cast<Assets::TextureAsset>(
                        Assets::AssetManager::GetCachedByKey(sprite.TextureKey));
                    if (!tex)
                        tex = Assets::TextureAsset::LoadBlocking(sprite.TextureKey);
                    sprite.CachedTexture = tex;
                }
                if (sprite.CachedTexture)
                    Renderer2D::DrawQuad(model, sprite.CachedTexture, sprite.Color);
                else
                    Renderer2D::DrawQuad(model, sprite.Color);
            }
            else
            {
                Renderer2D::DrawQuad(model, sprite.Color);
            }
        }

        Renderer2D::EndScene();
    }

    void SceneRenderer::RenderToViewport(Scene& scene, const Camera& camera,
        const std::shared_ptr<Framebuffer>& framebuffer, uint32_t width, uint32_t height)
    {
        if (!framebuffer || width == 0 || height == 0)
            return;

        auto& renderer = Renderer::GetInstance();
        if (!renderer.IsInitialized())
            return;

        renderer.SubmitCommand(std::make_unique<BindFramebufferCommand>(framebuffer));
        renderer.SubmitCommand(std::make_unique<SetViewportCommand>(0, 0, static_cast<int>(width), static_cast<int>(height)));

        ClearCommand::ClearFlags clearFlags;
        clearFlags.color = true;
        clearFlags.depth = true;
        clearFlags.stencil = false;
        renderer.SubmitCommand(std::make_unique<ClearCommand>(clearFlags, 0.12f, 0.12f, 0.14f, 1.0f));

        Render(scene, camera);

        renderer.SubmitCommand(std::make_unique<BindFramebufferCommand>(nullptr));
    }
}
