#include "Scene/Scene.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetManager.h"
#include "Assets/AssetPaths.h"
#include "Assets/AssetTypes.h"
#include "Assets/AssetUtils.h"
#include "Assets/MaterialAssetImporter.h"
#include "Assets/TextureAsset.h"
#include "Graphics/Camera/Camera.h"
#include "Graphics/Framebuffer.h"
#include "Graphics/RenderCommand.h"
#include "Graphics/Renderer.h"
#include "Graphics/Renderer2D.h"
#include "Scripting/NativeScriptRegistry.h"

#include <nlohmann/json.hpp>

#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <fstream>
#include <unordered_map>

namespace Limitless
{
    namespace
    {
        constexpr int32_t kSiblingOrderStep = 10;

        // Resolve legacy/stale asset keys to the latest known key in AssetDatabase.
        // This keeps scene references resilient across asset moves/renames.
        std::string ResolveLatestKeyFromDatabase(const std::string& assetKey)
        {
            if (assetKey.empty())
                return {};

            const auto record = Assets::AssetDatabase::GetInstance().FindByKey(assetKey);
            if (record.IsSuccess() && !record.GetValue().Key.empty())
                return record.GetValue().Key;

            return assetKey;
        }

        // Unity-style reference object for scene asset links.
        // Prefer GUID stability, but also store key for convenience and bundle-only scenarios.
        nlohmann::json MakeAssetReferenceJson(const std::string& assetKey, Assets::AssetType type)
        {
            using json = nlohmann::json;

            json ref = json::object();
            ref["guid"] = "";
            ref["key"] = assetKey;

            if (assetKey.empty())
            {
                return ref;
            }

            // Preferred: AssetDatabase GUID (stable, fast).
            const auto record = Assets::AssetDatabase::GetInstance().FindByKey(assetKey);
            if (record.IsSuccess() && !record.GetValue().Guid.empty())
            {
                ref["guid"] = record.GetValue().Guid;
                return ref;
            }

            // Fallback: resolve path and ensure `.meta` exists.
            const auto resolved = Assets::ResolveAssetKeyToPath(assetKey);
            if (resolved.IsSuccess())
            {
                const auto guidResult = Assets::LoadOrCreateGuid(resolved.GetValue().string(), {{"key", assetKey}, {"type", Assets::ToString(type)}});
                if (guidResult.IsSuccess())
                {
                    ref["guid"] = guidResult.GetValue();
                    (void)Assets::AssetDatabase::GetInstance().ImportOrUpdate(assetKey, type);
                }
            }

            return ref;
        }

        // Resolve a scene reference object to a key.
        // Accepts either:
        // - legacy string key
        // - object { guid, key }
        std::string ResolveAssetKeyFromSceneJson(const nlohmann::json& value)
        {
            if (value.is_string())
            {
                return ResolveLatestKeyFromDatabase(value.get<std::string>());
            }

            if (!value.is_object())
            {
                return {};
            }

            // Preferred: GUID -> key.
            if (value.contains("guid") && value["guid"].is_string())
            {
                const std::string guid = value["guid"].get<std::string>();
                if (!guid.empty())
                {
                    const auto rec = Assets::AssetDatabase::GetInstance().FindByGuid(guid);
                    if (rec.IsSuccess() && !rec.GetValue().Key.empty())
                    {
                        return rec.GetValue().Key;
                    }
                }
            }

            // Fallback: embedded key.
            if (value.contains("key") && value["key"].is_string())
            {
                return ResolveLatestKeyFromDatabase(value["key"].get<std::string>());
            }

            return {};
        }

        nlohmann::json SerializeScriptPropertyValue(const ScriptPropertyValue& value)
        {
            nlohmann::json root = nlohmann::json::object();
            if (const auto* floatValue = std::get_if<float>(&value))
            {
                root["Type"] = "Float";
                root["Value"] = *floatValue;
            }
            else if (const auto* integerValue = std::get_if<int32_t>(&value))
            {
                root["Type"] = "Integer";
                root["Value"] = *integerValue;
            }
            else if (const auto* booleanValue = std::get_if<bool>(&value))
            {
                root["Type"] = "Boolean";
                root["Value"] = *booleanValue;
            }
            else if (const auto* vectorValue = std::get_if<glm::vec3>(&value))
            {
                root["Type"] = "Vector3";
                root["Value"] = { vectorValue->x, vectorValue->y, vectorValue->z };
            }
            else if (const auto* stringValue = std::get_if<std::string>(&value))
            {
                root["Type"] = "String";
                root["Value"] = *stringValue;
            }
            return root;
        }

        bool DeserializeScriptPropertyValue(const nlohmann::json& root, ScriptPropertyValue& outValue)
        {
            if (!root.is_object())
                return false;

            const std::string typeName = root.value("Type", std::string{});
            if (typeName == "Float")
            {
                outValue = root.value("Value", 0.0f);
                return true;
            }
            if (typeName == "Integer")
            {
                outValue = root.value("Value", 0);
                return true;
            }
            if (typeName == "Boolean")
            {
                outValue = root.value("Value", false);
                return true;
            }
            if (typeName == "Vector3")
            {
                const auto vector = root.value("Value", std::vector<float>{ 0.0f, 0.0f, 0.0f });
                if (vector.size() >= 3)
                    outValue = glm::vec3(vector[0], vector[1], vector[2]);
                else
                    outValue = glm::vec3(0.0f);
                return true;
            }
            if (typeName == "String")
            {
                outValue = root.value("Value", std::string{});
                return true;
            }

            return false;
        }

    }

    Scene::Scene() = default;

    Scene::~Scene()
    {
        auto view = m_Registry.view<NativeScriptComponent>();
        for (entt::entity entity : view)
        {
            (void)entity;
            auto& nativeScript = view.get<NativeScriptComponent>(entity);
            for (auto& scriptEntry : nativeScript.Scripts)
            {
                if (scriptEntry.RuntimeInstance && scriptEntry.RuntimeInitialized)
                    scriptEntry.RuntimeInstance->OnDestroy();
                scriptEntry.RuntimeInstance.reset();
                scriptEntry.RuntimeInitialized = false;
                scriptEntry.RuntimeUpdateCount = 0;
            }
        }
    }

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

        if (auto* nativeScript = m_Registry.try_get<NativeScriptComponent>(entity))
        {
            for (auto& scriptEntry : nativeScript->Scripts)
            {
                if (scriptEntry.RuntimeInstance && scriptEntry.RuntimeInitialized)
                    scriptEntry.RuntimeInstance->OnDestroy();
                scriptEntry.RuntimeInstance.reset();
                scriptEntry.RuntimeInitialized = false;
                scriptEntry.RuntimeUpdateCount = 0;
            }
        }

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

    void Scene::Update(float deltaTime)
    {
        auto view = m_Registry.view<NativeScriptComponent>();
        for (entt::entity entity : view)
        {
            auto& nativeScript = view.get<NativeScriptComponent>(entity);
            for (auto& scriptEntry : nativeScript.Scripts)
            {
                if (!scriptEntry.Enabled || scriptEntry.ScriptClassName.empty())
                {
                    if (scriptEntry.RuntimeInstance && scriptEntry.RuntimeInitialized)
                        scriptEntry.RuntimeInstance->OnDestroy();
                    scriptEntry.RuntimeInstance.reset();
                    scriptEntry.RuntimeInitialized = false;
                    scriptEntry.RuntimeUpdateCount = 0;
                    continue;
                }

                if (!scriptEntry.RuntimeInstance)
                {
                    scriptEntry.RuntimeInstance = NativeScriptRegistry::CreateScript(scriptEntry.ScriptClassName);
                    if (!scriptEntry.RuntimeInstance && !scriptEntry.ScriptAssetRelativePath.empty())
                    {
                        const std::string fallbackClassName = std::filesystem::path(scriptEntry.ScriptAssetRelativePath).stem().string();
                        if (!fallbackClassName.empty() &&
                            fallbackClassName != scriptEntry.ScriptClassName &&
                            NativeScriptRegistry::HasScript(fallbackClassName))
                        {
                            scriptEntry.ScriptClassName = fallbackClassName;
                            scriptEntry.RuntimeInstance = NativeScriptRegistry::CreateScript(scriptEntry.ScriptClassName);
                        }
                    }
                    if (scriptEntry.RuntimeInstance)
                    {
                        scriptEntry.RuntimeInstance->m_Scene = this;
                        scriptEntry.RuntimeInstance->m_Registry = &m_Registry;
                        scriptEntry.RuntimeInstance->m_EntityHandle = entity;
                        scriptEntry.RuntimeInstance->m_ExposedProperties = &scriptEntry.ExposedProperties;
                        scriptEntry.RuntimeInitialized = false;
                        scriptEntry.RuntimeUpdateCount = 0;
                    }
                }

                if (!scriptEntry.RuntimeInstance)
                    continue;

                // Rebind runtime context every frame. NativeScriptEntry objects can move in memory
                // when the scripts vector grows/reorders, so cached pointers must be refreshed.
                scriptEntry.RuntimeInstance->m_Scene = this;
                scriptEntry.RuntimeInstance->m_Registry = &m_Registry;
                scriptEntry.RuntimeInstance->m_EntityHandle = entity;
                scriptEntry.RuntimeInstance->m_ExposedProperties = &scriptEntry.ExposedProperties;

                if (!scriptEntry.RuntimeInitialized)
                {
                    scriptEntry.RuntimeInstance->OnSynchronizeExposedFields();
                    scriptEntry.RuntimeInstance->OnCreate();
                    scriptEntry.RuntimeInitialized = true;
                }

                scriptEntry.RuntimeInstance->OnSynchronizeExposedFields();
                scriptEntry.RuntimeInstance->OnUpdate(deltaTime);
                ++scriptEntry.RuntimeUpdateCount;
            }
        }
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
                destinationSprite.TextureLoadAttempted = false;
                destinationSprite.Color = sprite->Color;
            }

            if (const auto* material = sourceRegistry.try_get<MaterialComponent>(sourceEntity))
            {
                auto& destinationMaterial = destinationRegistry.emplace<MaterialComponent>(destinationEntity);
                destinationMaterial.MaterialKey = material->MaterialKey;
                destinationMaterial.CachedMaterial.reset();
                destinationMaterial.MaterialLoadAttempted = false;
            }

            if (const auto* text = sourceRegistry.try_get<TextComponent>(sourceEntity))
            {
                auto& destinationText = destinationRegistry.emplace<TextComponent>(destinationEntity);
                destinationText.Text = text->Text;
                destinationText.FontFilePath = text->FontFilePath;
                destinationText.CachedFont.reset();
                destinationText.FontLoadAttempted = false;
                destinationText.FontSize = text->FontSize;
                destinationText.Color = text->Color;
                destinationText.Space = text->Space;
                destinationText.Anchor = text->Anchor;
            }

            if (const auto* camera = sourceRegistry.try_get<CameraComponent>(sourceEntity))
            {
                destinationRegistry.emplace<CameraComponent>(destinationEntity, *camera);
            }

            if (const auto* audioSource = sourceRegistry.try_get<AudioSourceComponent>(sourceEntity))
            {
                auto& destinationAudioSource = destinationRegistry.emplace<AudioSourceComponent>(destinationEntity);
                destinationAudioSource.AudioClipKey = audioSource->AudioClipKey;
                destinationAudioSource.Volume = audioSource->Volume;
                destinationAudioSource.PlayOnStart = audioSource->PlayOnStart;
                destinationAudioSource.Loop = audioSource->Loop;
                destinationAudioSource.Muted = audioSource->Muted;
                destinationAudioSource.RuntimeVoiceId = 0;
                destinationAudioSource.RuntimePlaybackStarted = false;
            }

            if (const auto* nativeScript = sourceRegistry.try_get<NativeScriptComponent>(sourceEntity))
            {
                auto& destinationNativeScript = destinationRegistry.emplace<NativeScriptComponent>(destinationEntity);
                destinationNativeScript.Scripts.reserve(nativeScript->Scripts.size());
                for (const auto& sourceScriptEntry : nativeScript->Scripts)
                {
                    auto& destinationScriptEntry = destinationNativeScript.Scripts.emplace_back();
                    destinationScriptEntry.ScriptClassName = sourceScriptEntry.ScriptClassName;
                    destinationScriptEntry.ScriptAssetRelativePath = sourceScriptEntry.ScriptAssetRelativePath;
                    destinationScriptEntry.Enabled = sourceScriptEntry.Enabled;
                    destinationScriptEntry.ExposedProperties = sourceScriptEntry.ExposedProperties;
                    destinationScriptEntry.RuntimeInitialized = false;
                    destinationScriptEntry.RuntimeInstance.reset();
                }
            }
        }

        for (const auto& [sourceEntity, destinationEntity] : entityMap)
        {
            const auto* sourceHierarchy = sourceRegistry.try_get<HierarchyComponent>(sourceEntity);
            if (!sourceHierarchy)
                continue;

            auto* destinationHierarchy = destinationRegistry.try_get<HierarchyComponent>(destinationEntity);
            if (!destinationHierarchy)
                destinationHierarchy = &destinationRegistry.emplace<HierarchyComponent>(destinationEntity);

            // Preserve exact local transform values from edit scene.
            // Using SetParent() would preserve world transform and rewrite local transform,
            // which causes children to shift when entering Play Mode.
            destinationHierarchy->Parent = entt::null;
            if (sourceHierarchy->Parent != entt::null)
            {
                auto foundParent = entityMap.find(sourceHierarchy->Parent);
                if (foundParent != entityMap.end())
                    destinationHierarchy->Parent = foundParent->second;
            }
            destinationHierarchy->SiblingOrder = sourceHierarchy->SiblingOrder;
        }

        clone->m_EditorCameraBookmark = m_EditorCameraBookmark;
        return clone;
    }

    Result<void> Scene::SaveToFile(const std::filesystem::path& path) const
    {
        std::error_code errorCode;
        const std::filesystem::path parentDirectory = path.parent_path();
        if (!parentDirectory.empty() && !std::filesystem::exists(parentDirectory, errorCode))
            std::filesystem::create_directories(parentDirectory, errorCode);
        if (errorCode)
            return Result<void>(ErrorCode::FileAccessDenied, "Scene::SaveToFile failed creating parent directories");

        auto view = m_Registry.view<TagComponent, TransformComponent>();
        std::vector<entt::entity> entities;
        entities.reserve(view.size_hint());
        for (entt::entity entity : view)
            entities.push_back(entity);
        std::sort(entities.begin(), entities.end(), [](entt::entity left, entt::entity right) {
            return static_cast<uint32_t>(left) < static_cast<uint32_t>(right);
        });

        std::unordered_map<entt::entity, int32_t> indexByEntity;
        indexByEntity.reserve(entities.size());
        for (size_t index = 0; index < entities.size(); ++index)
            indexByEntity.emplace(entities[index], static_cast<int32_t>(index));

        nlohmann::json root = nlohmann::json::object();
        root["Version"] = 5;
        if (m_EditorCameraBookmark.has_value())
        {
            root["EditorCamera"] = {
                { "Position", { m_EditorCameraBookmark->Position.x, m_EditorCameraBookmark->Position.y, m_EditorCameraBookmark->Position.z } },
                { "YawDegrees", m_EditorCameraBookmark->YawDegrees },
                { "PitchDegrees", m_EditorCameraBookmark->PitchDegrees }
            };
        }
        root["Entities"] = nlohmann::json::array();

        for (entt::entity entity : entities)
        {
            const auto& tag = view.get<TagComponent>(entity);
            const auto& transform = view.get<TransformComponent>(entity);
            nlohmann::json entry = nlohmann::json::object();
            entry["Tag"] = tag.Tag;
            entry["Transform"] = {
                { "Position", { transform.Position.x, transform.Position.y, transform.Position.z } },
                { "Rotation", { transform.Rotation.x, transform.Rotation.y, transform.Rotation.z } },
                { "Scale", { transform.Scale.x, transform.Scale.y, transform.Scale.z } }
            };

            const entt::entity parent = GetParent(entity);
            int32_t parentIndex = -1;
            if (parent != entt::null)
            {
                const auto it = indexByEntity.find(parent);
                if (it != indexByEntity.end())
                    parentIndex = it->second;
            }

            const auto* hierarchy = m_Registry.try_get<HierarchyComponent>(entity);
            entry["Hierarchy"] = {
                { "ParentIndex", parentIndex },
                { "SiblingOrder", hierarchy ? hierarchy->SiblingOrder : 0 }
            };

            if (const auto* sprite = m_Registry.try_get<SpriteComponent>(entity))
            {
                entry["Sprite"] = {
                    { "Texture", MakeAssetReferenceJson(sprite->TextureKey, Assets::AssetType::Texture2D) },
                    { "Color", { sprite->Color.r, sprite->Color.g, sprite->Color.b, sprite->Color.a } }
                };
            }

            if (const auto* material = m_Registry.try_get<MaterialComponent>(entity))
            {
                entry["Material"] = MakeAssetReferenceJson(material->MaterialKey, Assets::AssetType::Material);
            }

            if (const auto* text = m_Registry.try_get<TextComponent>(entity))
            {
                auto toAnchorString = [](TextComponent::ScreenAnchor anchor) -> const char*
                {
                    switch (anchor)
                    {
                    case TextComponent::ScreenAnchor::TopLeft: return "TopLeft";
                    case TextComponent::ScreenAnchor::TopCenter: return "TopCenter";
                    case TextComponent::ScreenAnchor::TopRight: return "TopRight";
                    case TextComponent::ScreenAnchor::MiddleLeft: return "MiddleLeft";
                    case TextComponent::ScreenAnchor::MiddleRight: return "MiddleRight";
                    case TextComponent::ScreenAnchor::BottomLeft: return "BottomLeft";
                    case TextComponent::ScreenAnchor::BottomCenter: return "BottomCenter";
                    case TextComponent::ScreenAnchor::BottomRight: return "BottomRight";
                    case TextComponent::ScreenAnchor::Center:
                    default:
                        return "Center";
                    }
                };
                entry["Text"] = {
                    { "Value", text->Text },
                    { "FontFilePath", text->FontFilePath },
                    { "FontSize", text->FontSize },
                    { "Color", { text->Color.r, text->Color.g, text->Color.b, text->Color.a } },
                    { "Space", text->Space == TextComponent::RenderSpace::Screen ? "Screen" : "World" },
                    { "Anchor", toAnchorString(text->Anchor) }
                };
            }

            if (const auto* camera = m_Registry.try_get<CameraComponent>(entity))
            {
                const char* projectionName = (camera->Projection == CameraComponent::ProjectionType::Perspective3D)
                    ? "Perspective3D"
                    : "Orthographic2D";

                entry["Camera"] = {
                    { "Projection", projectionName },
                    { "IsPrimary", camera->IsPrimary },
                    { "Zoom", camera->Zoom },
                    { "NearPlane", camera->NearPlane },
                    { "FarPlane", camera->FarPlane },
                    { "FieldOfViewYDegrees", camera->FieldOfViewYDegrees }
                };
            }

            if (const auto* audioSource = m_Registry.try_get<AudioSourceComponent>(entity))
            {
                entry["AudioSource"] = {
                    { "AudioClip", MakeAssetReferenceJson(audioSource->AudioClipKey, Assets::AssetType::AudioClip) },
                    { "Volume", audioSource->Volume },
                    { "PlayOnStart", audioSource->PlayOnStart },
                    { "Loop", audioSource->Loop },
                    { "Muted", audioSource->Muted }
                };
            }

            if (const auto* nativeScript = m_Registry.try_get<NativeScriptComponent>(entity))
            {
                nlohmann::json scriptEntries = nlohmann::json::array();
                for (const auto& scriptEntry : nativeScript->Scripts)
                {
                    nlohmann::json exposedProperties = nlohmann::json::object();
                    for (const auto& [propertyName, propertyValue] : scriptEntry.ExposedProperties)
                    {
                        exposedProperties[propertyName] = SerializeScriptPropertyValue(propertyValue);
                    }
                    scriptEntries.push_back({
                        { "Class", scriptEntry.ScriptClassName },
                        { "AssetPath", scriptEntry.ScriptAssetRelativePath },
                        { "ExposedProperties", std::move(exposedProperties) },
                        { "Enabled", scriptEntry.Enabled }
                    });
                }
                entry["NativeScripts"] = std::move(scriptEntries);
            }

            root["Entities"].push_back(std::move(entry));
        }

        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output.is_open())
            return Result<void>(ErrorCode::FileAccessDenied, "Scene::SaveToFile failed opening destination file");
        output << root.dump(2);
        return Result<void>();
    }

    Result<std::unique_ptr<Scene>> Scene::LoadFromFile(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input.is_open())
            return Result<std::unique_ptr<Scene>>(ErrorCode::FileNotFound, "Scene::LoadFromFile failed opening scene file");

        nlohmann::json root;
        try
        {
            input >> root;
        }
        catch (const std::exception& exception)
        {
            return Result<std::unique_ptr<Scene>>(ErrorCode::FileCorrupted, std::string("Scene::LoadFromFile JSON parse failed: ") + exception.what());
        }

        if (!root.is_object() || !root.contains("Entities") || !root["Entities"].is_array())
            return Result<std::unique_ptr<Scene>>(ErrorCode::FileCorrupted, "Scene::LoadFromFile invalid scene JSON format");

        auto scene = std::make_unique<Scene>();
        if (root.contains("EditorCamera") && root["EditorCamera"].is_object())
        {
            const auto& editorCameraJson = root["EditorCamera"];
            auto position = editorCameraJson.value("Position", std::vector<float>{ 0.0f, 0.0f, 0.0f });
            Scene::EditorCameraBookmark bookmark{};
            if (position.size() >= 3)
                bookmark.Position = glm::vec3(position[0], position[1], position[2]);
            bookmark.YawDegrees = editorCameraJson.value("YawDegrees", -90.0f);
            bookmark.PitchDegrees = editorCameraJson.value("PitchDegrees", 0.0f);
            scene->SetEditorCameraBookmark(bookmark);
        }

        std::vector<entt::entity> createdEntities;
        std::vector<int32_t> parentIndices;
        std::vector<int32_t> siblingOrders;
        createdEntities.reserve(root["Entities"].size());
        parentIndices.reserve(root["Entities"].size());
        siblingOrders.reserve(root["Entities"].size());

        for (const auto& entry : root["Entities"])
        {
            const std::string tag = entry.value("Tag", "Entity");
            const entt::entity entity = scene->CreateEntity(tag);
            auto& transform = scene->GetRegistry().get<TransformComponent>(entity);

            if (entry.contains("Transform"))
            {
                const auto& transformJson = entry["Transform"];
                auto position = transformJson.value("Position", std::vector<float>{ 0.0f, 0.0f, 0.0f });
                auto rotation = transformJson.value("Rotation", std::vector<float>{ 0.0f, 0.0f, 0.0f });
                auto scale = transformJson.value("Scale", std::vector<float>{ 1.0f, 1.0f, 1.0f });
                if (position.size() >= 3)
                    transform.Position = glm::vec3(position[0], position[1], position[2]);
                if (rotation.size() >= 3)
                    transform.Rotation = glm::vec3(rotation[0], rotation[1], rotation[2]);
                if (scale.size() >= 3)
                    transform.Scale = glm::vec3(scale[0], scale[1], scale[2]);
            }

            if (entry.contains("Sprite"))
            {
                const auto& spriteJson = entry["Sprite"];
                auto& sprite = scene->GetRegistry().emplace<SpriteComponent>(entity);
                // Backward compatible:
                // - v1: Sprite.TextureKey (string)
                // - v2+: Sprite.Texture { guid, key }
                if (spriteJson.contains("Texture"))
                {
                    sprite.TextureKey = ResolveAssetKeyFromSceneJson(spriteJson["Texture"]);
                }
                else
                {
                    sprite.TextureKey = spriteJson.value("TextureKey", "");
                }
                sprite.TextureLoadAttempted = false;
                auto color = spriteJson.value("Color", std::vector<float>{ 1.0f, 1.0f, 1.0f, 1.0f });
                if (color.size() >= 4)
                    sprite.Color = glm::vec4(color[0], color[1], color[2], color[3]);
            }

            if (entry.contains("Material"))
            {
                const auto& materialJson = entry["Material"];
                auto& material = scene->GetRegistry().emplace<MaterialComponent>(entity);
                // Backward compatible:
                // - v1: Material.MaterialKey (string)
                // - v2+: Material { guid, key }
                if (materialJson.is_object() && materialJson.contains("MaterialKey"))
                {
                    material.MaterialKey = ResolveLatestKeyFromDatabase(materialJson.value("MaterialKey", ""));
                }
                else
                {
                    material.MaterialKey = ResolveAssetKeyFromSceneJson(materialJson);
                }
                material.CachedMaterial.reset();
                material.MaterialLoadAttempted = false;
            }

            if (entry.contains("Text") && entry["Text"].is_object())
            {
                const auto& textJson = entry["Text"];
                auto& text = scene->GetRegistry().emplace<TextComponent>(entity);
                text.Text = textJson.value("Value", std::string("Text"));
                text.FontFilePath = textJson.value("FontFilePath", std::string{});
                text.FontSize = textJson.value("FontSize", 32.0f);
                const std::string renderSpace = textJson.value("Space", std::string("World"));
                text.Space = (renderSpace == "Screen")
                    ? TextComponent::RenderSpace::Screen
                    : TextComponent::RenderSpace::World;
                const std::string anchorName = textJson.value("Anchor", std::string("Center"));
                if (anchorName == "TopLeft")
                    text.Anchor = TextComponent::ScreenAnchor::TopLeft;
                else if (anchorName == "TopCenter")
                    text.Anchor = TextComponent::ScreenAnchor::TopCenter;
                else if (anchorName == "TopRight")
                    text.Anchor = TextComponent::ScreenAnchor::TopRight;
                else if (anchorName == "MiddleLeft")
                    text.Anchor = TextComponent::ScreenAnchor::MiddleLeft;
                else if (anchorName == "MiddleRight")
                    text.Anchor = TextComponent::ScreenAnchor::MiddleRight;
                else if (anchorName == "BottomLeft")
                    text.Anchor = TextComponent::ScreenAnchor::BottomLeft;
                else if (anchorName == "BottomCenter")
                    text.Anchor = TextComponent::ScreenAnchor::BottomCenter;
                else if (anchorName == "BottomRight")
                    text.Anchor = TextComponent::ScreenAnchor::BottomRight;
                else
                    text.Anchor = TextComponent::ScreenAnchor::Center;
                auto color = textJson.value("Color", std::vector<float>{ 1.0f, 1.0f, 1.0f, 1.0f });
                if (color.size() >= 4)
                    text.Color = glm::vec4(color[0], color[1], color[2], color[3]);
                text.CachedFont.reset();
                text.FontLoadAttempted = false;
            }

            if (entry.contains("Camera") && entry["Camera"].is_object())
            {
                const auto& cameraJson = entry["Camera"];
                auto& camera = scene->GetRegistry().emplace<CameraComponent>(entity);

                const std::string projectionName = cameraJson.value("Projection", "Orthographic2D");
                camera.Projection = (projectionName == "Perspective3D")
                    ? CameraComponent::ProjectionType::Perspective3D
                    : CameraComponent::ProjectionType::Orthographic2D;

                camera.IsPrimary = cameraJson.value("IsPrimary", true);
                camera.Zoom = cameraJson.value("Zoom", 1.0f);
                camera.NearPlane = cameraJson.value("NearPlane", -1.0f);
                camera.FarPlane = cameraJson.value("FarPlane", 1.0f);
                camera.FieldOfViewYDegrees = cameraJson.value("FieldOfViewYDegrees", 60.0f);
            }

            if (entry.contains("AudioSource"))
            {
                const auto& audioSourceJson = entry["AudioSource"];
                auto& audioSource = scene->GetRegistry().emplace<AudioSourceComponent>(entity);

                if (audioSourceJson.is_object() && audioSourceJson.contains("AudioClip"))
                    audioSource.AudioClipKey = ResolveAssetKeyFromSceneJson(audioSourceJson["AudioClip"]);
                else if (audioSourceJson.is_object())
                    audioSource.AudioClipKey = ResolveLatestKeyFromDatabase(audioSourceJson.value("AudioClipKey", std::string{}));

                if (audioSourceJson.is_object())
                {
                    audioSource.Volume = audioSourceJson.value("Volume", 1.0f);
                    if (audioSource.Volume < 0.0f)
                        audioSource.Volume = 0.0f;
                    audioSource.PlayOnStart = audioSourceJson.value("PlayOnStart", true);
                    audioSource.Loop = audioSourceJson.value("Loop", false);
                    audioSource.Muted = audioSourceJson.value("Muted", false);
                }

                audioSource.RuntimeVoiceId = 0;
                audioSource.RuntimePlaybackStarted = false;
            }

            auto loadNativeScriptEntry = [](const nlohmann::json& nativeScriptJson, NativeScriptEntry& outScriptEntry) {
                outScriptEntry.ScriptClassName = nativeScriptJson.value("Class", std::string{});
                outScriptEntry.ScriptAssetRelativePath = nativeScriptJson.value("AssetPath", std::string{});
                outScriptEntry.Enabled = nativeScriptJson.value("Enabled", true);
                if (nativeScriptJson.contains("ExposedProperties") && nativeScriptJson["ExposedProperties"].is_object())
                {
                    for (auto it = nativeScriptJson["ExposedProperties"].begin(); it != nativeScriptJson["ExposedProperties"].end(); ++it)
                    {
                        ScriptPropertyValue propertyValue;
                        if (DeserializeScriptPropertyValue(it.value(), propertyValue))
                            outScriptEntry.ExposedProperties[it.key()] = propertyValue;
                    }
                }
                outScriptEntry.RuntimeInitialized = false;
                outScriptEntry.RuntimeInstance.reset();
            };

            if (entry.contains("NativeScripts") && entry["NativeScripts"].is_array())
            {
                auto& nativeScript = scene->GetRegistry().emplace<NativeScriptComponent>(entity);
                for (const auto& scriptEntryJson : entry["NativeScripts"])
                {
                    if (!scriptEntryJson.is_object())
                        continue;
                    auto& loadedScriptEntry = nativeScript.Scripts.emplace_back();
                    loadNativeScriptEntry(scriptEntryJson, loadedScriptEntry);
                }
            }
            else if (entry.contains("NativeScript") && entry["NativeScript"].is_object())
            {
                // Backward compatibility: legacy scenes with a single native script object.
                auto& nativeScript = scene->GetRegistry().emplace<NativeScriptComponent>(entity);
                auto& loadedScriptEntry = nativeScript.Scripts.emplace_back();
                loadNativeScriptEntry(entry["NativeScript"], loadedScriptEntry);
            }

            int32_t parentIndex = -1;
            int32_t siblingOrder = 0;
            if (entry.contains("Hierarchy"))
            {
                const auto& hierarchyJson = entry["Hierarchy"];
                parentIndex = hierarchyJson.value("ParentIndex", -1);
                siblingOrder = hierarchyJson.value("SiblingOrder", 0);
            }

            createdEntities.push_back(entity);
            parentIndices.push_back(parentIndex);
            siblingOrders.push_back(siblingOrder);
        }

        auto& registry = scene->GetRegistry();
        for (size_t index = 0; index < createdEntities.size(); ++index)
        {
            auto* hierarchy = registry.try_get<HierarchyComponent>(createdEntities[index]);
            if (!hierarchy)
                hierarchy = &registry.emplace<HierarchyComponent>(createdEntities[index]);

            const int32_t parentIndex = parentIndices[index];
            if (parentIndex >= 0 && static_cast<size_t>(parentIndex) < createdEntities.size())
                hierarchy->Parent = createdEntities[static_cast<size_t>(parentIndex)];
            else
                hierarchy->Parent = entt::null;
            hierarchy->SiblingOrder = siblingOrders[index];
        }

        return Result<std::unique_ptr<Scene>>(std::move(scene));
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
            bool useMissingAssetFallback = false;

            // Material override (Unity-style): if the entity has a MaterialComponent, prefer its main texture.
            Assets::TextureAsset::Ptr materialMainTextureAsset;
            if (auto* material = registry.try_get<MaterialComponent>(entity))
            {
                if (!material->MaterialKey.empty())
                {
                    if (!material->CachedMaterial && !material->MaterialLoadAttempted)
                    {
                        material->CachedMaterial = Assets::AssetManager::LoadBlocking<Assets::MaterialAsset>(material->MaterialKey);
                        material->MaterialLoadAttempted = true;
                    }
                    if (material->CachedMaterial)
                    {
                        materialMainTextureAsset = material->CachedMaterial->GetMainTextureHandle().Lock();
                    }
                    else
                    {
                        useMissingAssetFallback = true;
                    }
                }
            }

            if (materialMainTextureAsset)
            {
                Renderer2D::DrawQuad(model, materialMainTextureAsset, sprite.Color);
            }
            else if (useMissingAssetFallback)
            {
                Renderer2D::DrawQuad(model, glm::vec4(1.0f, 0.0f, 1.0f, sprite.Color.a));
            }
            else if (!sprite.TextureKey.empty())
            {
                if (!sprite.CachedTexture && !sprite.TextureLoadAttempted)
                {
                    auto tex = std::dynamic_pointer_cast<Assets::TextureAsset>(
                        Assets::AssetManager::GetCachedByKey(sprite.TextureKey));
                    if (!tex)
                        tex = Assets::TextureAsset::LoadBlocking(sprite.TextureKey);
                    sprite.CachedTexture = tex;
                    sprite.TextureLoadAttempted = true;
                }
                if (sprite.CachedTexture)
                {
                    Renderer2D::DrawQuad(model, sprite.CachedTexture, sprite.Color);
                }
                else
                    Renderer2D::DrawQuad(model, glm::vec4(1.0f, 0.0f, 1.0f, sprite.Color.a));
            }
            else
            {
                Renderer2D::DrawQuad(model, sprite.Color);
            }
        }

        auto textView = registry.view<TransformComponent, TextComponent>();
        std::vector<entt::entity> textRenderEntities;
        textRenderEntities.reserve(textView.size_hint());
        for (entt::entity entity : textView)
            textRenderEntities.push_back(entity);

        std::sort(textRenderEntities.begin(), textRenderEntities.end(), [&scene, &registry](entt::entity left, entt::entity right) {
            const glm::mat4 leftWorld = scene.GetWorldTransformMatrix(left);
            const glm::mat4 rightWorld = scene.GetWorldTransformMatrix(right);
            const float leftZ = leftWorld[3].z;
            const float rightZ = rightWorld[3].z;
            if (leftZ != rightZ)
                return leftZ < rightZ;

            const auto* leftHierarchy = registry.try_get<HierarchyComponent>(left);
            const auto* rightHierarchy = registry.try_get<HierarchyComponent>(right);
            const int32_t leftOrder = leftHierarchy ? leftHierarchy->SiblingOrder : 0;
            const int32_t rightOrder = rightHierarchy ? rightHierarchy->SiblingOrder : 0;
            if (leftOrder != rightOrder)
                return leftOrder < rightOrder;

            return static_cast<uint32_t>(left) < static_cast<uint32_t>(right);
        });

        for (entt::entity entity : textRenderEntities)
        {
            auto& text = registry.get<TextComponent>(entity);
            if (text.Space != TextComponent::RenderSpace::World)
            {
                continue;
            }
            if (text.Text.empty() || text.FontFilePath.empty())
            {
                continue;
            }

            if (!text.CachedFont && !text.FontLoadAttempted)
            {
                text.CachedFont = Font::CreateFromFile(text.FontFilePath);
                text.FontLoadAttempted = true;
                if (!text.CachedFont)
                {
                    LT_CORE_WARN("SceneRenderer: failed to load text font '{}'", text.FontFilePath);
                }
            }

            if (!text.CachedFont)
            {
                continue;
            }

            const glm::mat4 model = scene.GetWorldTransformMatrix(entity);
            Renderer2D::DrawText(model, text.Text, text.CachedFont, text.FontSize, text.Color);
        }

        Renderer2D::EndScene();
    }

    namespace
    {
        glm::vec2 GetScreenAnchorBasePosition(TextComponent::ScreenAnchor anchor, float halfWidth, float halfHeight)
        {
            switch (anchor)
            {
            case TextComponent::ScreenAnchor::TopLeft:
                return glm::vec2(-halfWidth, halfHeight);
            case TextComponent::ScreenAnchor::TopCenter:
                return glm::vec2(0.0f, halfHeight);
            case TextComponent::ScreenAnchor::TopRight:
                return glm::vec2(halfWidth, halfHeight);
            case TextComponent::ScreenAnchor::MiddleLeft:
                return glm::vec2(-halfWidth, 0.0f);
            case TextComponent::ScreenAnchor::MiddleRight:
                return glm::vec2(halfWidth, 0.0f);
            case TextComponent::ScreenAnchor::BottomLeft:
                return glm::vec2(-halfWidth, -halfHeight);
            case TextComponent::ScreenAnchor::BottomCenter:
                return glm::vec2(0.0f, -halfHeight);
            case TextComponent::ScreenAnchor::BottomRight:
                return glm::vec2(halfWidth, -halfHeight);
            case TextComponent::ScreenAnchor::Center:
            default:
                return glm::vec2(0.0f, 0.0f);
            }
        }

        void RenderScreenSpaceTextPass(Scene& scene, uint32_t width, uint32_t height)
        {
            if (width == 0 || height == 0)
            {
                return;
            }

            auto& registry = scene.GetRegistry();
            auto textView = registry.view<TransformComponent, TextComponent>();
            std::vector<entt::entity> textRenderEntities;
            textRenderEntities.reserve(textView.size_hint());
            for (entt::entity entity : textView)
            {
                textRenderEntities.push_back(entity);
            }

            std::sort(textRenderEntities.begin(), textRenderEntities.end(), [&scene, &registry](entt::entity left, entt::entity right) {
                const glm::mat4 leftWorld = scene.GetWorldTransformMatrix(left);
                const glm::mat4 rightWorld = scene.GetWorldTransformMatrix(right);
                const float leftZ = leftWorld[3].z;
                const float rightZ = rightWorld[3].z;
                if (leftZ != rightZ)
                    return leftZ < rightZ;

                const auto* leftHierarchy = registry.try_get<HierarchyComponent>(left);
                const auto* rightHierarchy = registry.try_get<HierarchyComponent>(right);
                const int32_t leftOrder = leftHierarchy ? leftHierarchy->SiblingOrder : 0;
                const int32_t rightOrder = rightHierarchy ? rightHierarchy->SiblingOrder : 0;
                if (leftOrder != rightOrder)
                    return leftOrder < rightOrder;

                return static_cast<uint32_t>(left) < static_cast<uint32_t>(right);
            });

            // Screen-space UI uses centered pixel coordinates so existing world-origin text
            // (near X=0,Y=0) remains visible when switching to screen space.
            // Use a wide depth range for overlays so non-trivial entity Z values do not clip.
            // Depth testing is disabled for this pass, so Z is not used for occlusion.
            const float halfWidth = static_cast<float>(width) * 0.5f;
            const float halfHeight = static_cast<float>(height) * 0.5f;
            const glm::mat4 screenProjection = glm::ortho(
                -halfWidth, halfWidth,
                -halfHeight, halfHeight,
                -10000.0f, 10000.0f);
            Renderer2D::BeginScene(screenProjection, false);

            for (entt::entity entity : textRenderEntities)
            {
                auto& text = registry.get<TextComponent>(entity);
                if (text.Space != TextComponent::RenderSpace::Screen)
                {
                    continue;
                }

                if (text.Text.empty() || text.FontFilePath.empty())
                {
                    continue;
                }

                if (!text.CachedFont && !text.FontLoadAttempted)
                {
                    text.CachedFont = Font::CreateFromFile(text.FontFilePath);
                    text.FontLoadAttempted = true;
                    if (!text.CachedFont)
                    {
                        LT_CORE_WARN("SceneRenderer: failed to load text font '{}'", text.FontFilePath);
                    }
                }

                if (!text.CachedFont)
                {
                    continue;
                }

                // Screen-space text should not inherit world hierarchy transforms.
                // Use the entity's local transform directly and force Z to 0 for overlay usage.
                const auto& transform = registry.get<TransformComponent>(entity);
                glm::mat4 model = transform.GetLocalMatrix();
                const glm::vec2 anchorBase = GetScreenAnchorBasePosition(text.Anchor, halfWidth, halfHeight);
                model[3].x += anchorBase.x;
                model[3].y += anchorBase.y;
                model[3].z = 0.0f;
                // Renderer2D text sizing is normalized by font em-size for world-space rendering.
                // Convert to pixel-like sizing for screen-space UI so a FontSize of 32 behaves
                // like ~32 px instead of sub-pixel world units.
                const float screenSpaceFontSize = text.FontSize * std::max(1.0f, text.CachedFont->GetEmSize());
                Renderer2D::DrawText(model, text.Text, text.CachedFont, screenSpaceFontSize, text.Color);
            }

            Renderer2D::EndScene();
        }
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
        RenderScreenSpaceTextPass(scene, width, height);

        renderer.SubmitCommand(std::make_unique<BindFramebufferCommand>(nullptr));
    }
}
