#include "Scripting/ManagedScriptHost.h"
#include "Scripting/ManagedScriptHostInternal.h"

#include "Core/Debug/Log.h"
#include "Audio/AudioEngine.h"
#include "Scripting/Random.h"
#include "Scene/Components/AudioComponents.h"
#include "Scene/Components/CoreComponents.h"
#include "Scene/Components/PhysicsComponents.h"
#include "Scene/Components/RenderingComponents.h"
#include "Scene/Components/TilemapComponents.h"
#include "Scene/ParticleEmitterSystem.h"
#include "Scene/SceneManager.h"
#include "Scene/Scene.h"

#include <Coral/Assembly.hpp>
#include <Coral/Attribute.hpp>
#include <Coral/HostInstance.hpp>
#include <Coral/MessageLevel.hpp>
#include <Coral/String.hpp>
#include <Coral/Type.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <exception>
#include <memory>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace Limitless::ManagedScriptHost
{
    using namespace Internal;

    namespace Internal
    {
        HostState s_HostState;

        std::string ToLower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        std::string ToUtf8(Coral::String value)
        {
            Coral::ScopedString scoped(value);
            return static_cast<std::string>(scoped);
        }

        std::string ToUtf8Borrowed(Coral::String value)
        {
            if (value.Data() == nullptr)
                return {};

            return static_cast<std::string>(value);
        }

        std::filesystem::path NormalizeManagedDirectoryPath(const std::filesystem::path& path)
        {
            std::error_code errorCode;
            std::filesystem::path normalizedPath = std::filesystem::weakly_canonical(path, errorCode);
            if (errorCode)
            {
                errorCode.clear();
                normalizedPath = std::filesystem::absolute(path, errorCode);
                if (errorCode)
                    normalizedPath = path;
            }

            return normalizedPath.lexically_normal();
        }

        std::filesystem::path BuildLoadedManagedPayloadDirectory(uint64_t payloadId)
        {
            std::error_code errorCode;
            const std::filesystem::path tempRoot = std::filesystem::temp_directory_path(errorCode);
            const std::filesystem::path baseRoot = errorCode
                ? std::filesystem::path("Build") / "ManagedRuntimeShadow"
                : tempRoot / "LimitlessManagedRuntimeShadow";

            static const std::string shadowSessionDirectoryName = []() {
                const auto nowTicks = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                return std::string("session-") + std::to_string(nowTicks);
            }();

            return baseRoot / shadowSessionDirectoryName / ("payload-" + std::to_string(payloadId));
        }

        bool ShadowCopyManagedPayload(const std::filesystem::path& sourceDirectory,
                                      std::filesystem::path& outLoadedDirectory,
                                      std::string* errorMessage)
        {
            outLoadedDirectory.clear();

            const std::filesystem::path loadedDirectory = BuildLoadedManagedPayloadDirectory(s_HostState.NextLoadedPayloadId++);
            std::error_code errorCode;
            std::filesystem::remove_all(loadedDirectory, errorCode);
            errorCode.clear();
            std::filesystem::create_directories(loadedDirectory, errorCode);
            if (errorCode)
            {
                if (errorMessage != nullptr)
                    *errorMessage = "Failed to create shadow managed payload directory '" + loadedDirectory.string() + "': " + errorCode.message();
                return false;
            }

            errorCode.clear();
            std::filesystem::copy(sourceDirectory,
                                  loadedDirectory,
                                  std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
                                  errorCode);
            if (errorCode)
            {
                if (errorMessage != nullptr)
                    *errorMessage = "Failed to shadow-copy managed payload from '" + sourceDirectory.string() + "' to '"
                        + loadedDirectory.string() + "': " + errorCode.message();
                std::filesystem::remove_all(loadedDirectory, errorCode);
                return false;
            }

            outLoadedDirectory = loadedDirectory;
            if (errorMessage != nullptr)
                errorMessage->clear();
            return true;
        }

        void CleanupLoadedManagedPayloadDirectory()
        {
            if (s_HostState.Snapshot.LoadedManagedDirectory.empty())
                return;

            std::error_code errorCode;
            std::filesystem::remove_all(s_HostState.Snapshot.LoadedManagedDirectory, errorCode);
            (void)errorCode;
            s_HostState.Snapshot.LoadedManagedDirectory.clear();
        }

        entt::registry* GetActiveRegistry()
        {
            if (s_HostState.ActiveScene == nullptr)
                return nullptr;
            return &s_HostState.ActiveScene->GetRegistry();
        }

        entt::entity ResolveManagedEntityHandle(uint32_t entityHandle)
        {
            if (s_HostState.ActiveScene == nullptr)
                return entt::null;
            return s_HostState.ActiveScene->ResolveEntityReference(static_cast<entt::entity>(entityHandle));
        }

        ManagedVector3 ToManagedVector3(const glm::vec3& value)
        {
            ManagedVector3 result{};
            result.X = value.x;
            result.Y = value.y;
            result.Z = value.z;
            return result;
        }

        glm::vec3 ToGlmVector3(const ManagedVector3& value)
        {
            return glm::vec3(value.X, value.Y, value.Z);
        }

        ManagedVector2 ToManagedVector2(const glm::vec2& value)
        {
            ManagedVector2 result{};
            result.X = value.x;
            result.Y = value.y;
            return result;
        }

        glm::vec2 ToGlmVector2(const ManagedVector2& value)
        {
            return glm::vec2(value.X, value.Y);
        }

        ManagedVector4 ToManagedVector4(const glm::vec4& value)
        {
            ManagedVector4 result{};
            result.X = value.x;
            result.Y = value.y;
            result.Z = value.z;
            result.W = value.w;
            return result;
        }

        glm::vec4 ToGlmVector4(const ManagedVector4& value)
        {
            return glm::vec4(value.X, value.Y, value.Z, value.W);
        }

        ManagedRaycastHit2D ToManagedRaycastHit2D(const Physics2DRaycastHit& value)
        {
            ManagedRaycastHit2D result{};
            if (!value.HasHit || s_HostState.ActiveScene == nullptr)
                return result;

            result.HasHitValue = 1;
            result.HitEntityHandle = static_cast<uint32_t>(s_HostState.ActiveScene->ResolveEntityReference(value.Entity));
            result.Point = ToManagedVector2(value.Point);
            result.Normal = ToManagedVector2(value.Normal);
            result.Fraction = value.Fraction;
            return result;
        }

        Rigidbody2DComponent* TryGetManagedRigidbody2DComponent(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return nullptr;
            return registry->try_get<Rigidbody2DComponent>(entity);
        }

        BoxCollider2DComponent* TryGetManagedBoxCollider2DComponent(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return nullptr;
            return registry->try_get<BoxCollider2DComponent>(entity);
        }

        CircleCollider2DComponent* TryGetManagedCircleCollider2DComponent(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return nullptr;
            return registry->try_get<CircleCollider2DComponent>(entity);
        }

        PolygonCollider2DComponent* TryGetManagedPolygonCollider2DComponent(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return nullptr;
            return registry->try_get<PolygonCollider2DComponent>(entity);
        }

        EdgeCollider2DComponent* TryGetManagedEdgeCollider2DComponent(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return nullptr;
            return registry->try_get<EdgeCollider2DComponent>(entity);
        }

        CapsuleCollider2DComponent* TryGetManagedCapsuleCollider2DComponent(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return nullptr;
            return registry->try_get<CapsuleCollider2DComponent>(entity);
        }

        Joint2DComponent* TryGetManagedJoint2DComponent(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return nullptr;
            return registry->try_get<Joint2DComponent>(entity);
        }

        SpriteComponent* TryGetManagedSpriteComponent(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return nullptr;
            return registry->try_get<SpriteComponent>(entity);
        }

        MaterialComponent* TryGetManagedMaterialComponent(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return nullptr;
            return registry->try_get<MaterialComponent>(entity);
        }

        CanvasComponent* TryGetManagedCanvasComponent(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return nullptr;
            return registry->try_get<CanvasComponent>(entity);
        }

        RectTransformComponent* TryGetManagedRectTransformComponent(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return nullptr;
            return registry->try_get<RectTransformComponent>(entity);
        }

        DirectionalLight2DComponent* TryGetManagedDirectionalLight2DComponent(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return nullptr;
            return registry->try_get<DirectionalLight2DComponent>(entity);
        }

        PointLight2DComponent* TryGetManagedPointLight2DComponent(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return nullptr;
            return registry->try_get<PointLight2DComponent>(entity);
        }

        UIImageComponent* TryGetManagedUIImageComponent(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return nullptr;
            return registry->try_get<UIImageComponent>(entity);
        }

        UIPanelComponent* TryGetManagedUIPanelComponent(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return nullptr;
            return registry->try_get<UIPanelComponent>(entity);
        }

        UITextComponent* TryGetManagedUITextComponent(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return nullptr;
            return registry->try_get<UITextComponent>(entity);
        }

        UIButtonComponent* TryGetManagedUIButtonComponent(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return nullptr;
            return registry->try_get<UIButtonComponent>(entity);
        }

        UISliderComponent* TryGetManagedUISliderComponent(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return nullptr;
            return registry->try_get<UISliderComponent>(entity);
        }

        AudioListener2DComponent* TryGetManagedAudioListener2DComponent(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return nullptr;
            return registry->try_get<AudioListener2DComponent>(entity);
        }

        AudioListener3DComponent* TryGetManagedAudioListener3DComponent(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return nullptr;
            return registry->try_get<AudioListener3DComponent>(entity);
        }

        AudioSourceComponent* TryGetManagedAudioSourceComponent(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return nullptr;
            return registry->try_get<AudioSourceComponent>(entity);
        }

        AnimatorComponent* TryGetManagedAnimatorComponent(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return nullptr;
            return registry->try_get<AnimatorComponent>(entity);
        }

        AnimationEventReceiverComponent* TryGetManagedAnimationEventReceiverComponent(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return nullptr;
            return registry->try_get<AnimationEventReceiverComponent>(entity);
        }

        ParticleEmitterComponent* TryGetManagedParticleEmitterComponent(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return nullptr;
            return registry->try_get<ParticleEmitterComponent>(entity);
        }

        Grid2DComponent* TryGetManagedGrid2DComponent(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return nullptr;
            return registry->try_get<Grid2DComponent>(entity);
        }

        TilemapLayerComponent* TryGetManagedTilemapLayerComponent(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return nullptr;
            return registry->try_get<TilemapLayerComponent>(entity);
        }

        ScriptPropertyValue BuildDefaultScriptPropertyValue(ScriptPropertyType type)
        {
            switch (type)
            {
                case ScriptPropertyType::Float:
                    return 0.0f;
                case ScriptPropertyType::Integer:
                    return int32_t(0);
                case ScriptPropertyType::Boolean:
                    return false;
                case ScriptPropertyType::Vector3:
                    return glm::vec3(0.0f);
                case ScriptPropertyType::String:
                    return std::string{};
                case ScriptPropertyType::Entity:
                    return ScriptEntityReference{};
                case ScriptPropertyType::Prefab:
                    return Prefab{};
                case ScriptPropertyType::Vector2:
                    return glm::vec2(0.0f);
                case ScriptPropertyType::Vector4:
                    return glm::vec4(0.0f);
                case ScriptPropertyType::Enum:
                    return ScriptEnumValue{};
            }

            return 0.0f;
        }

        bool TryMapManagedFieldType(Coral::Type& fieldType, ScriptPropertyType& outType)
        {
            const std::string fullName = ToUtf8(fieldType.GetFullName());
            if (fullName == kSystemSingleTypeName)
            {
                outType = ScriptPropertyType::Float;
                return true;
            }
            if (fullName == kSystemInt32TypeName)
            {
                outType = ScriptPropertyType::Integer;
                return true;
            }
            if (fullName == kSystemBooleanTypeName)
            {
                outType = ScriptPropertyType::Boolean;
                return true;
            }
            if (fullName == kSystemStringTypeName)
            {
                outType = ScriptPropertyType::String;
                return true;
            }
            if (fullName == kManagedVector2TypeName)
            {
                outType = ScriptPropertyType::Vector2;
                return true;
            }
            if (fullName == kManagedVector3TypeName)
            {
                outType = ScriptPropertyType::Vector3;
                return true;
            }
            if (fullName == kManagedVector4TypeName)
            {
                outType = ScriptPropertyType::Vector4;
                return true;
            }
            if (fullName == kManagedEntityTypeName)
            {
                outType = ScriptPropertyType::Entity;
                return true;
            }

            try
            {
                Coral::Type& baseType = fieldType.GetBaseType();
                if (baseType)
                {
                    const std::string baseName = ToUtf8(baseType.GetFullName());
                    if (baseName == kSystemEnumBaseTypeName)
                    {
                        outType = ScriptPropertyType::Enum;
                        return true;
                    }
                }
            }
            catch (...) {}

            return false;
        }

        ScriptEnumValue BuildManagedEnumDefault(Coral::Type& fieldType)
        {
            ScriptEnumValue result{};
            result.EnumTypeName = ToUtf8(fieldType.GetFullName());
            try
            {
                const auto fields = fieldType.GetFields();
                for (const auto& f : fields)
                {
                    if (f.GetAccessibility() == Coral::TypeAccessibility::Public)
                    {
                        const std::string name = ToUtf8(f.GetName());
                        if (name != "value__")
                            result.EnumNames.push_back(name);
                    }
                }
            }
            catch (...) {}
            result.Value = 0;
            return result;
        }

        bool TryReadDefaultFieldValue(const Coral::ManagedObject& instance,
                                      const ReflectedFieldDefinition& fieldDefinition,
                                      ScriptPropertyValue& outValue)
        {
            if (!instance.IsValid())
                return false;

            switch (fieldDefinition.Type)
            {
                case ScriptPropertyType::Float:
                    outValue = instance.GetFieldValue<float>(fieldDefinition.Name);
                    return true;
                case ScriptPropertyType::Integer:
                    outValue = instance.GetFieldValue<int32_t>(fieldDefinition.Name);
                    return true;
                case ScriptPropertyType::Boolean:
                    outValue = instance.GetFieldValue<bool>(fieldDefinition.Name);
                    return true;
                case ScriptPropertyType::Vector3:
                {
                    const ManagedVector3 value = instance.GetFieldValue<ManagedVector3>(fieldDefinition.Name);
                    outValue = ToGlmVector3(value);
                    return true;
                }
                case ScriptPropertyType::String:
                    outValue = instance.GetFieldValue<std::string>(fieldDefinition.Name);
                    return true;
                case ScriptPropertyType::Entity:
                    outValue = ScriptEntityReference{};
                    return true;
                case ScriptPropertyType::Prefab:
                    outValue = Prefab{};
                    return true;
                case ScriptPropertyType::Vector2:
                {
                    const ManagedVector2 value = instance.GetFieldValue<ManagedVector2>(fieldDefinition.Name);
                    outValue = ToGlmVector2(value);
                    return true;
                }
                case ScriptPropertyType::Vector4:
                {
                    const ManagedVector4 value = instance.GetFieldValue<ManagedVector4>(fieldDefinition.Name);
                    outValue = ToGlmVector4(value);
                    return true;
                }
                case ScriptPropertyType::Enum:
                {
                    const int32_t value = instance.GetFieldValue<int32_t>(fieldDefinition.Name);
                    if (const auto* existingEnum = std::get_if<ScriptEnumValue>(&fieldDefinition.DefaultValue))
                    {
                        ScriptEnumValue enumVal = *existingEnum;
                        enumVal.Value = value;
                        outValue = std::move(enumVal);
                    }
                    else
                    {
                        ScriptEnumValue enumVal{};
                        enumVal.Value = value;
                        outValue = std::move(enumVal);
                    }
                    return true;
                }
            }

            return false;
        }

        bool ScriptPropertyValuesEqual(const ScriptPropertyValue& left, const ScriptPropertyValue& right)
        {
            if (left.index() != right.index())
                return false;

            if (const auto* floatValue = std::get_if<float>(&left))
                return *floatValue == std::get<float>(right);
            if (const auto* integerValue = std::get_if<int32_t>(&left))
                return *integerValue == std::get<int32_t>(right);
            if (const auto* booleanValue = std::get_if<bool>(&left))
                return *booleanValue == std::get<bool>(right);
            if (const auto* vectorValue = std::get_if<glm::vec3>(&left))
            {
                const glm::vec3& rightValue = std::get<glm::vec3>(right);
                return vectorValue->x == rightValue.x && vectorValue->y == rightValue.y && vectorValue->z == rightValue.z;
            }
            if (const auto* stringValue = std::get_if<std::string>(&left))
                return *stringValue == std::get<std::string>(right);
            if (const auto* entityValue = std::get_if<ScriptEntityReference>(&left))
            {
                const auto& rightValue = std::get<ScriptEntityReference>(right);
                return entityValue->Tag == rightValue.Tag &&
                       entityValue->PrefabAssetKey == rightValue.PrefabAssetKey &&
                       entityValue->SceneEntityId == rightValue.SceneEntityId;
            }
            if (const auto* prefabValue = std::get_if<Prefab>(&left))
                return prefabValue->AssetKey == std::get<Prefab>(right).AssetKey;
            if (const auto* vec2Value = std::get_if<glm::vec2>(&left))
            {
                const glm::vec2& rightValue = std::get<glm::vec2>(right);
                return vec2Value->x == rightValue.x && vec2Value->y == rightValue.y;
            }
            if (const auto* vec4Value = std::get_if<glm::vec4>(&left))
            {
                const glm::vec4& rightValue = std::get<glm::vec4>(right);
                return vec4Value->x == rightValue.x && vec4Value->y == rightValue.y && vec4Value->z == rightValue.z && vec4Value->w == rightValue.w;
            }
            if (const auto* enumValue = std::get_if<ScriptEnumValue>(&left))
            {
                const auto& rightValue = std::get<ScriptEnumValue>(right);
                return enumValue->Value == rightValue.Value && enumValue->EnumTypeName == rightValue.EnumTypeName;
            }

            return false;
        }

        bool TryReadRuntimeFieldValue(const RuntimeInstance& runtimeInstance,
                                      Scene* scene,
                                      const ReflectedFieldDefinition& fieldDefinition,
                                      ScriptPropertyValue& outValue,
                                      std::string* errorMessage)
        {
            try
            {
                switch (fieldDefinition.Type)
                {
                    case ScriptPropertyType::Float:
                        outValue = runtimeInstance.Object.GetFieldValue<float>(fieldDefinition.Name);
                        return true;
                    case ScriptPropertyType::Integer:
                        outValue = runtimeInstance.Object.GetFieldValue<int32_t>(fieldDefinition.Name);
                        return true;
                    case ScriptPropertyType::Boolean:
                        outValue = runtimeInstance.Object.GetFieldValue<bool>(fieldDefinition.Name);
                        return true;
                    case ScriptPropertyType::Vector3:
                    {
                        const ManagedVector3 value = runtimeInstance.Object.GetFieldValue<ManagedVector3>(fieldDefinition.Name);
                        outValue = ToGlmVector3(value);
                        return true;
                    }
                    case ScriptPropertyType::String:
                        outValue = runtimeInstance.Object.GetFieldValue<std::string>(fieldDefinition.Name);
                        return true;
                    case ScriptPropertyType::Entity:
                    {
                        ScriptEntityReference entityReference{};
                        Coral::ManagedObject entityObject = runtimeInstance.Object.GetFieldValue<Coral::ManagedObject>(fieldDefinition.Name);
                        if (entityObject.m_Handle != nullptr && scene != nullptr)
                        {
                            const uint32_t entityHandle = entityObject.GetPropertyValue<uint32_t>("Handle");
                            if (entityHandle != static_cast<uint32_t>(entt::null))
                            {
                                const entt::entity resolvedEntity = scene->ResolveEntityReference(static_cast<entt::entity>(entityHandle));
                                if (resolvedEntity != entt::null && scene->IsValid(resolvedEntity))
                                {
                                    if (const auto* tagComponent = scene->GetRegistry().try_get<TagComponent>(resolvedEntity))
                                        entityReference.Tag = tagComponent->Tag;
                                    entityReference.SceneEntityId = scene->GetEntityPersistentId(resolvedEntity);
                                }
                            }
                        }

                        outValue = std::move(entityReference);
                        return true;
                    }
                    case ScriptPropertyType::Prefab:
                        outValue = Prefab{};
                        return true;
                    case ScriptPropertyType::Vector2:
                    {
                        const ManagedVector2 value = runtimeInstance.Object.GetFieldValue<ManagedVector2>(fieldDefinition.Name);
                        outValue = ToGlmVector2(value);
                        return true;
                    }
                    case ScriptPropertyType::Vector4:
                    {
                        const ManagedVector4 value = runtimeInstance.Object.GetFieldValue<ManagedVector4>(fieldDefinition.Name);
                        outValue = ToGlmVector4(value);
                        return true;
                    }
                    case ScriptPropertyType::Enum:
                    {
                        const int32_t value = runtimeInstance.Object.GetFieldValue<int32_t>(fieldDefinition.Name);
                        if (const auto* existingEnum = std::get_if<ScriptEnumValue>(&fieldDefinition.DefaultValue))
                        {
                            ScriptEnumValue enumVal = *existingEnum;
                            enumVal.Value = value;
                            outValue = std::move(enumVal);
                        }
                        else
                        {
                            ScriptEnumValue enumVal{};
                            enumVal.Value = value;
                            outValue = std::move(enumVal);
                        }
                        return true;
                    }
                }
            }
            catch (const std::exception& exception)
            {
                if (errorMessage != nullptr)
                    *errorMessage = exception.what();
                return false;
            }
            catch (...)
            {
                if (errorMessage != nullptr)
                    *errorMessage = "non-standard exception while reading managed exposed properties";
                return false;
            }

            return false;
        }

        bool HasManagedAttribute(Coral::FieldInfo& fieldInfo, const char* attributeTypeName)
        {
            try
            {
                auto attributes = fieldInfo.GetAttributes();
                for (auto& attr : attributes)
                {
                    Coral::Type& attrType = attr.GetType();
                    if (attrType)
                    {
                        const std::string attrFullName = ToUtf8(attrType.GetFullName());
                        if (attrFullName == attributeTypeName)
                            return true;
                    }
                }
            }
            catch (...) {}
            return false;
        }

        std::vector<ReflectedFieldDefinition> ReflectManagedFields(Coral::Type& type)
        {
            std::vector<ReflectedFieldDefinition> reflectedFields;

            Coral::ManagedObject defaultInstance = type.CreateInstance();
            const bool hasDefaultInstance = defaultInstance.IsValid();

            for (auto fieldInfo : type.GetFields())
            {
                const bool isPublic = fieldInfo.GetAccessibility() == Coral::TypeAccessibility::Public;

                if (!isPublic)
                {
                    if (!HasManagedAttribute(fieldInfo, kSerializeFieldAttributeTypeName))
                        continue;
                }

                if (isPublic && HasManagedAttribute(fieldInfo, kHideInInspectorAttributeTypeName))
                    continue;

                const std::string fieldName = ToUtf8(fieldInfo.GetName());
                if (fieldName.empty())
                    continue;

                ScriptPropertyType propertyType = ScriptPropertyType::Float;
                Coral::Type& fieldType = fieldInfo.GetType();
                if (!TryMapManagedFieldType(fieldType, propertyType))
                    continue;

                ReflectedFieldDefinition fieldDefinition{};
                fieldDefinition.Name = fieldName;
                fieldDefinition.Type = propertyType;

                if (propertyType == ScriptPropertyType::Enum)
                    fieldDefinition.DefaultValue = BuildManagedEnumDefault(fieldType);
                else
                    fieldDefinition.DefaultValue = BuildDefaultScriptPropertyValue(propertyType);

                ScriptPropertyValue reflectedDefaultValue = fieldDefinition.DefaultValue;
                if (hasDefaultInstance && TryReadDefaultFieldValue(defaultInstance, fieldDefinition, reflectedDefaultValue))
                    fieldDefinition.DefaultValue = std::move(reflectedDefaultValue);

                reflectedFields.push_back(std::move(fieldDefinition));
            }

            if (hasDefaultInstance)
                defaultInstance.Destroy();

            return reflectedFields;
        }

        std::string GetUnqualifiedManagedClassName(std::string_view className)
        {
            if (className.empty())
                return {};

            const size_t namespaceSeparator = className.rfind('.');
            if (namespaceSeparator == std::string_view::npos)
                return std::string(className);
            return std::string(className.substr(namespaceSeparator + 1));
        }

        const DiscoveredScriptClass* ResolveDiscoveredClassMetadata(std::string_view className)
        {
            if (className.empty())
                return nullptr;

            const auto exactIterator = std::find_if(s_HostState.Snapshot.Classes.begin(),
                                                    s_HostState.Snapshot.Classes.end(),
                                                    [&](const DiscoveredScriptClass& discoveredClass) {
                                                        return discoveredClass.FullName == className;
                                                    });
            if (exactIterator != s_HostState.Snapshot.Classes.end())
                return &(*exactIterator);

            const std::string requestedUnqualifiedName = GetUnqualifiedManagedClassName(className);
            if (requestedUnqualifiedName.empty())
                return nullptr;

            const DiscoveredScriptClass* matchedClass = nullptr;
            for (const DiscoveredScriptClass& discoveredClass : s_HostState.Snapshot.Classes)
            {
                if (GetUnqualifiedManagedClassName(discoveredClass.FullName) != requestedUnqualifiedName)
                    continue;

                if (matchedClass != nullptr)
                    return nullptr;

                matchedClass = &discoveredClass;
            }

            return matchedClass;
        }

        const DiscoveredScriptClass* FindDiscoveredClassMetadata(std::string_view className)
        {
            return ResolveDiscoveredClassMetadata(className);
        }

        Coral::ManagedObject CreateManagedEntityObject(Scene* scene, const ScriptEntityReference& entityReference)
        {
            if (s_HostState.ContractAssembly == nullptr)
                return {};

            Coral::Type& entityType = s_HostState.ContractAssembly->GetLocalType(kManagedEntityTypeName);
            if (!entityType)
                return {};

            uint32_t handle = static_cast<uint32_t>(entt::null);
            if (scene != nullptr)
            {
                entt::entity resolvedEntity = entt::null;
                if (!entityReference.SceneEntityId.empty())
                    resolvedEntity = scene->FindEntityByPersistentId(entityReference.SceneEntityId);

                if (resolvedEntity == entt::null && !entityReference.Tag.empty())
                {
                    auto& registry = scene->GetRegistry();
                    auto view = registry.view<TagComponent>();
                    for (entt::entity entity : view)
                    {
                        const auto& tagComponent = view.get<TagComponent>(entity);
                        if (tagComponent.Tag != entityReference.Tag)
                            continue;

                        resolvedEntity = entity;
                        break;
                    }
                }

                if (resolvedEntity != entt::null && scene->IsValid(resolvedEntity))
                    handle = static_cast<uint32_t>(scene->ResolveEntityReference(resolvedEntity));
            }

            return entityType.CreateInstance(handle);
        }

        bool ApplyManagedFieldValue(RuntimeInstance& runtimeInstance,
                                    Scene* scene,
                                    const ReflectedFieldDefinition& fieldDefinition,
                                    const ScriptPropertyValue& propertyValue,
                                    std::string* errorMessage)
        {
            try
            {
                switch (fieldDefinition.Type)
                {
                    case ScriptPropertyType::Float:
                    {
                        const float value = std::holds_alternative<float>(propertyValue)
                            ? std::get<float>(propertyValue)
                            : std::get<float>(fieldDefinition.DefaultValue);
                        runtimeInstance.Object.SetFieldValue(fieldDefinition.Name, value);
                        break;
                    }
                    case ScriptPropertyType::Integer:
                    {
                        const int32_t value = std::holds_alternative<int32_t>(propertyValue)
                            ? std::get<int32_t>(propertyValue)
                            : std::get<int32_t>(fieldDefinition.DefaultValue);
                        runtimeInstance.Object.SetFieldValue(fieldDefinition.Name, value);
                        break;
                    }
                    case ScriptPropertyType::Boolean:
                    {
                        const bool value = std::holds_alternative<bool>(propertyValue)
                            ? std::get<bool>(propertyValue)
                            : std::get<bool>(fieldDefinition.DefaultValue);
                        runtimeInstance.Object.SetFieldValue(fieldDefinition.Name, value);
                        break;
                    }
                    case ScriptPropertyType::Vector3:
                    {
                        const glm::vec3 value = std::holds_alternative<glm::vec3>(propertyValue)
                            ? std::get<glm::vec3>(propertyValue)
                            : std::get<glm::vec3>(fieldDefinition.DefaultValue);
                        runtimeInstance.Object.SetFieldValue(fieldDefinition.Name, ToManagedVector3(value));
                        break;
                    }
                    case ScriptPropertyType::String:
                    {
                        const std::string value = std::holds_alternative<std::string>(propertyValue)
                            ? std::get<std::string>(propertyValue)
                            : std::get<std::string>(fieldDefinition.DefaultValue);
                        runtimeInstance.Object.SetFieldValue(fieldDefinition.Name, value);
                        break;
                    }
                    case ScriptPropertyType::Entity:
                    {
                        const ScriptEntityReference value = std::holds_alternative<ScriptEntityReference>(propertyValue)
                            ? std::get<ScriptEntityReference>(propertyValue)
                            : std::get<ScriptEntityReference>(fieldDefinition.DefaultValue);
                        Coral::ManagedObject entityObject = CreateManagedEntityObject(scene, value);
                        if (!entityObject.IsValid())
                        {
                            if (errorMessage != nullptr)
                                *errorMessage = "failed to construct managed entity field value for '" + fieldDefinition.Name + "'";
                            return false;
                        }

                        runtimeInstance.Object.SetFieldValue(fieldDefinition.Name, entityObject);
                        entityObject.Destroy();
                        break;
                    }
                    case ScriptPropertyType::Prefab:
                        break;
                    case ScriptPropertyType::Vector2:
                    {
                        const glm::vec2 value = std::holds_alternative<glm::vec2>(propertyValue)
                            ? std::get<glm::vec2>(propertyValue)
                            : std::get<glm::vec2>(fieldDefinition.DefaultValue);
                        runtimeInstance.Object.SetFieldValue(fieldDefinition.Name, ToManagedVector2(value));
                        break;
                    }
                    case ScriptPropertyType::Vector4:
                    {
                        const glm::vec4 value = std::holds_alternative<glm::vec4>(propertyValue)
                            ? std::get<glm::vec4>(propertyValue)
                            : std::get<glm::vec4>(fieldDefinition.DefaultValue);
                        runtimeInstance.Object.SetFieldValue(fieldDefinition.Name, ToManagedVector4(value));
                        break;
                    }
                    case ScriptPropertyType::Enum:
                    {
                        int32_t value = 0;
                        if (const auto* enumVal = std::get_if<ScriptEnumValue>(&propertyValue))
                            value = enumVal->Value;
                        else if (const auto* defaultEnum = std::get_if<ScriptEnumValue>(&fieldDefinition.DefaultValue))
                            value = defaultEnum->Value;
                        runtimeInstance.Object.SetFieldValue(fieldDefinition.Name, value);
                        break;
                    }
                }
            }
            catch (const std::exception& exception)
            {
                if (errorMessage != nullptr)
                    *errorMessage = exception.what();
                return false;
            }
            catch (...)
            {
                if (errorMessage != nullptr)
                    *errorMessage = "non-standard exception while synchronizing managed exposed properties";
                return false;
            }

            return true;
        }

        RuntimeInstance* FindMutableRuntimeInstance(uint64_t instanceId)
        {
            const auto iterator = s_HostState.RuntimeInstances.find(instanceId);
            if (iterator == s_HostState.RuntimeInstances.end())
                return nullptr;
            return &iterator->second;
        }

    }
}

