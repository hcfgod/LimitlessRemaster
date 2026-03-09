#include "Scripting/ManagedScriptHost.h"

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
    namespace
    {
        constexpr const char* kCoralManagedAssemblyFileName = "Coral.Managed.dll";
        constexpr const char* kContractAssemblyFileName = "Limitless.Managed.dll";
        constexpr const char* kScriptBaseTypeName = "Limitless.Managed.ScriptableEntity";
        constexpr const char* kScriptBridgeTypeName = "Limitless.Managed.ScriptBridge";
        constexpr const char* kManagedVector3TypeName = "Limitless.Managed.Vector3";
        constexpr const char* kManagedEntityTypeName = "Limitless.Managed.Entity";
        constexpr const char* kSystemSingleTypeName = "System.Single";
        constexpr const char* kSystemInt32TypeName = "System.Int32";
        constexpr const char* kSystemBooleanTypeName = "System.Boolean";
        constexpr const char* kSystemStringTypeName = "System.String";

        struct RuntimeInstance final
        {
            uint64_t Id = 0;
            std::string ClassName;
            uint32_t EntityHandle = 0;
            Coral::ManagedObject Object;
            uint64_t LastSynchronizedExposedPropertiesRevision = 0;
        };

        struct ManagedVector3 final
        {
            float X = 0.0f;
            float Y = 0.0f;
            float Z = 0.0f;
        };

        struct ManagedVector2 final
        {
            float X = 0.0f;
            float Y = 0.0f;
        };

        struct ManagedVector4 final
        {
            float X = 0.0f;
            float Y = 0.0f;
            float Z = 0.0f;
            float W = 0.0f;
        };

        struct ManagedRaycastHit2D final
        {
            int32_t HasHitValue = 0;
            uint32_t HitEntityHandle = static_cast<uint32_t>(entt::null);
            ManagedVector2 Point{};
            ManagedVector2 Normal{ 0.0f, 1.0f };
            float Fraction = 0.0f;
        };

        struct HostState final
        {
            std::unique_ptr<Coral::HostInstance> Host;
            std::unique_ptr<Coral::AssemblyLoadContext> Context;
            Coral::ManagedAssembly* ContractAssembly = nullptr;
            ManagedScriptPayload::PayloadManifest PayloadManifest;
            DiscoverySnapshot Snapshot;
            std::unordered_map<std::string, Coral::Type*> DiscoveredTypes;
            std::unordered_map<uint64_t, RuntimeInstance> RuntimeInstances;
            uint64_t NextRuntimeInstanceId = 1;
            uint64_t NextLoadedPayloadId = 1;
            Scene* ActiveScene = nullptr;
            uint64_t ActiveRuntimeInstanceId = 0;
            std::string LastManagedExceptionMessage;
        };

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
            if (fullName == kManagedVector3TypeName)
            {
                outType = ScriptPropertyType::Vector3;
                return true;
            }
            if (fullName == kManagedEntityTypeName)
            {
                outType = ScriptPropertyType::Entity;
                return true;
            }

            return false;
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
                return entityValue->Tag == rightValue.Tag && entityValue->PrefabAssetKey == rightValue.PrefabAssetKey;
            }
            if (const auto* prefabValue = std::get_if<Prefab>(&left))
                return prefabValue->AssetKey == std::get<Prefab>(right).AssetKey;

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
                        if (entityObject.IsValid() && scene != nullptr)
                        {
                            const uint32_t entityHandle = entityObject.GetPropertyValue<uint32_t>("Handle");
                            if (entityHandle != static_cast<uint32_t>(entt::null))
                            {
                                const entt::entity resolvedEntity = scene->ResolveEntityReference(static_cast<entt::entity>(entityHandle));
                                if (resolvedEntity != entt::null && scene->IsValid(resolvedEntity))
                                {
                                    if (const auto* tagComponent = scene->GetRegistry().try_get<TagComponent>(resolvedEntity))
                                        entityReference.Tag = tagComponent->Tag;
                                }
                            }
                        }

                        outValue = std::move(entityReference);
                        return true;
                    }
                    case ScriptPropertyType::Prefab:
                        outValue = Prefab{};
                        return true;
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

        std::vector<ReflectedFieldDefinition> ReflectManagedFields(Coral::Type& type)
        {
            std::vector<ReflectedFieldDefinition> reflectedFields;

            Coral::ManagedObject defaultInstance = type.CreateInstance();
            const bool hasDefaultInstance = defaultInstance.IsValid();

            for (auto fieldInfo : type.GetFields())
            {
                if (fieldInfo.GetAccessibility() != Coral::TypeAccessibility::Public)
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
            if (scene != nullptr && !entityReference.Tag.empty())
            {
                auto& registry = scene->GetRegistry();
                auto view = registry.view<TagComponent>();
                for (entt::entity entity : view)
                {
                    const auto& tagComponent = view.get<TagComponent>(entity);
                    if (tagComponent.Tag != entityReference.Tag)
                        continue;

                    handle = static_cast<uint32_t>(scene->ResolveEntityReference(entity));
                    break;
                }
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

        const RuntimeInstance* FindRuntimeInstance(uint64_t instanceId)
        {
            const auto iterator = s_HostState.RuntimeInstances.find(instanceId);
            if (iterator == s_HostState.RuntimeInstances.end())
                return nullptr;
            return &iterator->second;
        }

        const RuntimeInstance* FindActiveRuntimeInstance()
        {
            return FindRuntimeInstance(s_HostState.ActiveRuntimeInstanceId);
        }

        std::string BuildManagedLogPrefix()
        {
            const RuntimeInstance* runtimeInstance = FindActiveRuntimeInstance();
            if (!runtimeInstance)
                return "Managed script";

            return "Managed script '" + runtimeInstance->ClassName + "' on entity " + std::to_string(runtimeInstance->EntityHandle);
        }

        void LogCoralMessage(std::string_view message, Coral::MessageLevel level)
        {
            if (level & Coral::MessageLevel::Error)
            {
                LT_ERROR("Managed scripting (Coral): {}", message);
                return;
            }
            if (level & Coral::MessageLevel::Warning)
            {
                LT_WARN("Managed scripting (Coral): {}", message);
                return;
            }
            if (level & Coral::MessageLevel::Info)
            {
                LT_INFO("Managed scripting (Coral): {}", message);
                return;
            }
            LT_TRACE("Managed scripting (Coral): {}", message);
        }

        void CaptureManagedException(std::string_view message)
        {
            s_HostState.LastManagedExceptionMessage.assign(message.begin(), message.end());
        }

        void ManagedLogInfoIcall(Coral::String message)
        {
            LT_INFO("{}: {}", BuildManagedLogPrefix(), ToUtf8Borrowed(message));
        }

        void ManagedLogWarningIcall(Coral::String message)
        {
            LT_WARN("{}: {}", BuildManagedLogPrefix(), ToUtf8Borrowed(message));
        }

        void ManagedLogErrorIcall(Coral::String message)
        {
            LT_ERROR("{}: {}", BuildManagedLogPrefix(), ToUtf8Borrowed(message));
        }

        void ManagedSetRandomSeedIcall(uint32_t seed)
        {
            Random::SetSeed(seed);
        }

        int32_t ManagedRandomRangeIntIcall(int32_t minInclusive, int32_t maxExclusive)
        {
            return Random::Range(minInclusive, maxExclusive);
        }

        float ManagedRandomRangeFloatIcall(float minInclusive, float maxInclusive)
        {
            return Random::Range(minInclusive, maxInclusive);
        }

        float ManagedRandomValueIcall()
        {
            return Random::Value();
        }

        bool ManagedLoadSceneIcall(Coral::String sceneIdentifier, int loadMode)
        {
            return SceneManager::LoadScene(ToUtf8Borrowed(sceneIdentifier), static_cast<LoadSceneMode>(loadMode));
        }

        bool ManagedReloadCurrentSceneIcall()
        {
            return SceneManager::ReloadCurrentScene();
        }

        bool ManagedSetActiveSceneIcall(Coral::String sceneIdentifier)
        {
            return SceneManager::SetActiveScene(ToUtf8Borrowed(sceneIdentifier));
        }

        bool ManagedUnloadSceneIcall(Coral::String sceneIdentifier)
        {
            return SceneManager::UnloadScene(ToUtf8Borrowed(sceneIdentifier));
        }

        uint32_t ManagedCreateEntityIcall(Coral::String name)
        {
            if (s_HostState.ActiveScene == nullptr)
                return static_cast<uint32_t>(entt::null);

            return static_cast<uint32_t>(s_HostState.ActiveScene->CreateEntity(ToUtf8Borrowed(name)));
        }

        bool ManagedDestroyEntityIcall(uint32_t entityHandle)
        {
            if (s_HostState.ActiveScene == nullptr)
                return false;

            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (entity == entt::null)
                return false;

            s_HostState.ActiveScene->DestroyEntity(entity);
            return true;
        }

        bool ManagedEntityExistsIcall(uint32_t entityHandle)
        {
            if (s_HostState.ActiveScene == nullptr)
                return false;

            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            return entity != entt::null && s_HostState.ActiveScene->IsValid(entity);
        }

        bool ManagedGetEntityEnabledIcall(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return false;

            const auto* tagComponent = registry->try_get<TagComponent>(entity);
            return tagComponent != nullptr && tagComponent->Enabled;
        }

        bool ManagedSetEntityEnabledIcall(uint32_t entityHandle, bool enabled)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return false;

            auto* tagComponent = registry->try_get<TagComponent>(entity);
            if (tagComponent == nullptr)
                return false;

            tagComponent->Enabled = enabled;
            return true;
        }

        bool ManagedIsEntityEnabledInHierarchyIcall(uint32_t entityHandle)
        {
            if (s_HostState.ActiveScene == nullptr)
                return false;

            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            return entity != entt::null && s_HostState.ActiveScene->IsEntityEnabledInHierarchy(entity);
        }

        bool ManagedSetParentIcall(uint32_t childEntityHandle, uint32_t parentEntityHandle)
        {
            if (s_HostState.ActiveScene == nullptr)
                return false;

            const entt::entity childEntity = ResolveManagedEntityHandle(childEntityHandle);
            entt::entity parentEntity = entt::null;
            if (parentEntityHandle != static_cast<uint32_t>(entt::null))
                parentEntity = ResolveManagedEntityHandle(parentEntityHandle);
            if (childEntity == entt::null)
                return false;
            if (parentEntityHandle != static_cast<uint32_t>(entt::null) && parentEntity == entt::null)
                return false;

            return s_HostState.ActiveScene->SetParent(childEntity, parentEntity);
        }

        uint32_t ManagedGetParentIcall(uint32_t entityHandle)
        {
            if (s_HostState.ActiveScene == nullptr)
                return static_cast<uint32_t>(entt::null);

            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (entity == entt::null)
                return static_cast<uint32_t>(entt::null);

            const entt::entity parentEntity = s_HostState.ActiveScene->GetParent(entity);
            if (parentEntity == entt::null)
                return static_cast<uint32_t>(entt::null);
            return static_cast<uint32_t>(s_HostState.ActiveScene->ResolveEntityReference(parentEntity));
        }

        uint32_t ManagedGetChildCountIcall(uint32_t entityHandle)
        {
            if (s_HostState.ActiveScene == nullptr)
                return 0;

            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (entity == entt::null)
                return 0;

            return static_cast<uint32_t>(s_HostState.ActiveScene->GetChildren(entity).size());
        }

        uint32_t ManagedGetChildAtIcall(uint32_t entityHandle, uint32_t index)
        {
            if (s_HostState.ActiveScene == nullptr)
                return static_cast<uint32_t>(entt::null);

            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (entity == entt::null)
                return static_cast<uint32_t>(entt::null);

            const auto children = s_HostState.ActiveScene->GetChildren(entity);
            if (index >= children.size())
                return static_cast<uint32_t>(entt::null);
            return static_cast<uint32_t>(s_HostState.ActiveScene->ResolveEntityReference(children[index]));
        }

        uint32_t ManagedFindEntityByTagIcall(Coral::String tag)
        {
            entt::registry* registry = GetActiveRegistry();
            if (registry == nullptr)
                return static_cast<uint32_t>(entt::null);

            const std::string requestedTag = ToUtf8Borrowed(tag);
            if (requestedTag.empty())
                return static_cast<uint32_t>(entt::null);

            auto view = registry->view<TagComponent>();
            for (entt::entity entity : view)
            {
                const auto& tagComponent = view.get<TagComponent>(entity);
                if (tagComponent.Tag == requestedTag)
                    return static_cast<uint32_t>(entity);
            }

            return static_cast<uint32_t>(entt::null);
        }

        bool ManagedHasTagComponentIcall(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            return registry != nullptr && entity != entt::null && registry->all_of<TagComponent>(entity);
        }

        Coral::String ManagedGetTagIcall(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return Coral::String::New("");

            const auto* tagComponent = registry->try_get<TagComponent>(entity);
            if (tagComponent == nullptr)
                return Coral::String::New("");
            return Coral::String::New(tagComponent->Tag);
        }

        bool ManagedSetTagIcall(uint32_t entityHandle, Coral::String tag)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return false;

            auto* tagComponent = registry->try_get<TagComponent>(entity);
            if (tagComponent == nullptr)
                return false;

            tagComponent->Tag = ToUtf8Borrowed(tag);
            return true;
        }

        bool ManagedHasTransformComponentIcall(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            return registry != nullptr && entity != entt::null && registry->all_of<TransformComponent>(entity);
        }

        ManagedVector3 ManagedGetTransformPositionIcall(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return {};

            const auto* transformComponent = registry->try_get<TransformComponent>(entity);
            return transformComponent ? ToManagedVector3(transformComponent->Position) : ManagedVector3{};
        }

        ManagedVector3 ManagedGetTransformRotationIcall(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return {};

            const auto* transformComponent = registry->try_get<TransformComponent>(entity);
            return transformComponent ? ToManagedVector3(transformComponent->Rotation) : ManagedVector3{};
        }

        ManagedVector3 ManagedGetTransformScaleIcall(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return {};

            const auto* transformComponent = registry->try_get<TransformComponent>(entity);
            return transformComponent ? ToManagedVector3(transformComponent->Scale) : ManagedVector3{};
        }

        void ManagedSetTransformPositionIcall(uint32_t entityHandle, ManagedVector3 value)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null || s_HostState.ActiveScene == nullptr)
                return;

            auto* transformComponent = registry->try_get<TransformComponent>(entity);
            if (transformComponent == nullptr)
                return;

            transformComponent->Position = ToGlmVector3(value);
            s_HostState.ActiveScene->MarkTransformDirty(entity);
        }

        void ManagedSetTransformRotationIcall(uint32_t entityHandle, ManagedVector3 value)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null || s_HostState.ActiveScene == nullptr)
                return;

            auto* transformComponent = registry->try_get<TransformComponent>(entity);
            if (transformComponent == nullptr)
                return;

            transformComponent->Rotation = ToGlmVector3(value);
            s_HostState.ActiveScene->MarkTransformDirty(entity);
        }

        void ManagedSetTransformScaleIcall(uint32_t entityHandle, ManagedVector3 value)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null || s_HostState.ActiveScene == nullptr)
                return;

            auto* transformComponent = registry->try_get<TransformComponent>(entity);
            if (transformComponent == nullptr)
                return;

            transformComponent->Scale = ToGlmVector3(value);
            s_HostState.ActiveScene->MarkTransformDirty(entity);
        }

        bool ManagedHasCameraComponentIcall(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            return registry != nullptr && entity != entt::null && registry->all_of<CameraComponent>(entity);
        }

        int ManagedGetCameraProjectionIcall(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return static_cast<int>(CameraComponent::ProjectionType::Orthographic2D);

            const auto* cameraComponent = registry->try_get<CameraComponent>(entity);
            return cameraComponent ? static_cast<int>(cameraComponent->Projection) : static_cast<int>(CameraComponent::ProjectionType::Orthographic2D);
        }

        void ManagedSetCameraProjectionIcall(uint32_t entityHandle, int projection)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return;

            auto* cameraComponent = registry->try_get<CameraComponent>(entity);
            if (cameraComponent == nullptr)
                return;

            cameraComponent->Projection = static_cast<CameraComponent::ProjectionType>(projection);
        }

        bool ManagedGetCameraPrimaryIcall(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return false;

            const auto* cameraComponent = registry->try_get<CameraComponent>(entity);
            return cameraComponent != nullptr && cameraComponent->IsPrimary;
        }

        void ManagedSetCameraPrimaryIcall(uint32_t entityHandle, bool primary)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return;

            auto* cameraComponent = registry->try_get<CameraComponent>(entity);
            if (cameraComponent == nullptr)
                return;

            cameraComponent->IsPrimary = primary;
        }

        float ManagedGetCameraZoomIcall(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return 1.0f;

            const auto* cameraComponent = registry->try_get<CameraComponent>(entity);
            return cameraComponent ? cameraComponent->Zoom : 1.0f;
        }

        void ManagedSetCameraZoomIcall(uint32_t entityHandle, float zoom)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return;

            auto* cameraComponent = registry->try_get<CameraComponent>(entity);
            if (cameraComponent == nullptr)
                return;

            cameraComponent->Zoom = zoom;
        }

        float ManagedGetCameraNearPlaneIcall(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return -1.0f;

            const auto* cameraComponent = registry->try_get<CameraComponent>(entity);
            return cameraComponent ? cameraComponent->NearPlane : -1.0f;
        }

        void ManagedSetCameraNearPlaneIcall(uint32_t entityHandle, float nearPlane)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return;

            auto* cameraComponent = registry->try_get<CameraComponent>(entity);
            if (cameraComponent == nullptr)
                return;

            cameraComponent->NearPlane = nearPlane;
        }

        float ManagedGetCameraFarPlaneIcall(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return 1.0f;

            const auto* cameraComponent = registry->try_get<CameraComponent>(entity);
            return cameraComponent ? cameraComponent->FarPlane : 1.0f;
        }

        void ManagedSetCameraFarPlaneIcall(uint32_t entityHandle, float farPlane)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return;

            auto* cameraComponent = registry->try_get<CameraComponent>(entity);
            if (cameraComponent == nullptr)
                return;

            cameraComponent->FarPlane = farPlane;
        }

        float ManagedGetCameraFieldOfViewYDegreesIcall(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return 60.0f;

            const auto* cameraComponent = registry->try_get<CameraComponent>(entity);
            return cameraComponent ? cameraComponent->FieldOfViewYDegrees : 60.0f;
        }

        void ManagedSetCameraFieldOfViewYDegreesIcall(uint32_t entityHandle, float fieldOfViewYDegrees)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return;

            auto* cameraComponent = registry->try_get<CameraComponent>(entity);
            if (cameraComponent == nullptr)
                return;

            cameraComponent->FieldOfViewYDegrees = fieldOfViewYDegrees;
        }

        bool ManagedHasBoxCollider2DComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedBoxCollider2DComponent(entityHandle) != nullptr;
        }

        ManagedVector2 ManagedGetBoxCollider2DOffsetIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedBoxCollider2DComponent(entityHandle);
            return collider ? ToManagedVector2(collider->Offset) : ManagedVector2{};
        }

        void ManagedSetBoxCollider2DOffsetIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* collider = TryGetManagedBoxCollider2DComponent(entityHandle))
                collider->Offset = ToGlmVector2(value);
        }

        ManagedVector2 ManagedGetBoxCollider2DSizeIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedBoxCollider2DComponent(entityHandle);
            return collider ? ToManagedVector2(collider->Size) : ManagedVector2{ 1.0f, 1.0f };
        }

        void ManagedSetBoxCollider2DSizeIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* collider = TryGetManagedBoxCollider2DComponent(entityHandle))
                collider->Size = ToGlmVector2(value);
        }

        float ManagedGetBoxCollider2DDensityIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedBoxCollider2DComponent(entityHandle);
            return collider ? collider->Density : 1.0f;
        }

        void ManagedSetBoxCollider2DDensityIcall(uint32_t entityHandle, float value)
        {
            if (auto* collider = TryGetManagedBoxCollider2DComponent(entityHandle))
                collider->Density = value;
        }

        float ManagedGetBoxCollider2DFrictionIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedBoxCollider2DComponent(entityHandle);
            return collider ? collider->Friction : 0.5f;
        }

        void ManagedSetBoxCollider2DFrictionIcall(uint32_t entityHandle, float value)
        {
            if (auto* collider = TryGetManagedBoxCollider2DComponent(entityHandle))
                collider->Friction = value;
        }

        float ManagedGetBoxCollider2DRestitutionIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedBoxCollider2DComponent(entityHandle);
            return collider ? collider->Restitution : 0.0f;
        }

        void ManagedSetBoxCollider2DRestitutionIcall(uint32_t entityHandle, float value)
        {
            if (auto* collider = TryGetManagedBoxCollider2DComponent(entityHandle))
                collider->Restitution = value;
        }

        bool ManagedGetBoxCollider2DIsSensorIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedBoxCollider2DComponent(entityHandle);
            return collider ? collider->IsSensor : false;
        }

        void ManagedSetBoxCollider2DIsSensorIcall(uint32_t entityHandle, bool value)
        {
            if (auto* collider = TryGetManagedBoxCollider2DComponent(entityHandle))
                collider->IsSensor = value;
        }

        uint64_t ManagedGetBoxCollider2DCollisionLayerIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedBoxCollider2DComponent(entityHandle);
            return collider ? collider->CollisionLayer : 1ull;
        }

        void ManagedSetBoxCollider2DCollisionLayerIcall(uint32_t entityHandle, uint64_t value)
        {
            if (auto* collider = TryGetManagedBoxCollider2DComponent(entityHandle))
                collider->CollisionLayer = value;
        }

        uint64_t ManagedGetBoxCollider2DCollisionMaskIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedBoxCollider2DComponent(entityHandle);
            return collider ? collider->CollisionMask : ~0ull;
        }

        void ManagedSetBoxCollider2DCollisionMaskIcall(uint32_t entityHandle, uint64_t value)
        {
            if (auto* collider = TryGetManagedBoxCollider2DComponent(entityHandle))
                collider->CollisionMask = value;
        }

        bool ManagedHasCircleCollider2DComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedCircleCollider2DComponent(entityHandle) != nullptr;
        }

        ManagedVector2 ManagedGetCircleCollider2DOffsetIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedCircleCollider2DComponent(entityHandle);
            return collider ? ToManagedVector2(collider->Offset) : ManagedVector2{};
        }

        void ManagedSetCircleCollider2DOffsetIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* collider = TryGetManagedCircleCollider2DComponent(entityHandle))
                collider->Offset = ToGlmVector2(value);
        }

        float ManagedGetCircleCollider2DRadiusIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedCircleCollider2DComponent(entityHandle);
            return collider ? collider->Radius : 0.5f;
        }

        void ManagedSetCircleCollider2DRadiusIcall(uint32_t entityHandle, float value)
        {
            if (auto* collider = TryGetManagedCircleCollider2DComponent(entityHandle))
                collider->Radius = value;
        }

        float ManagedGetCircleCollider2DDensityIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedCircleCollider2DComponent(entityHandle);
            return collider ? collider->Density : 1.0f;
        }

        void ManagedSetCircleCollider2DDensityIcall(uint32_t entityHandle, float value)
        {
            if (auto* collider = TryGetManagedCircleCollider2DComponent(entityHandle))
                collider->Density = value;
        }

        float ManagedGetCircleCollider2DFrictionIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedCircleCollider2DComponent(entityHandle);
            return collider ? collider->Friction : 0.5f;
        }

        void ManagedSetCircleCollider2DFrictionIcall(uint32_t entityHandle, float value)
        {
            if (auto* collider = TryGetManagedCircleCollider2DComponent(entityHandle))
                collider->Friction = value;
        }

        float ManagedGetCircleCollider2DRestitutionIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedCircleCollider2DComponent(entityHandle);
            return collider ? collider->Restitution : 0.0f;
        }

        void ManagedSetCircleCollider2DRestitutionIcall(uint32_t entityHandle, float value)
        {
            if (auto* collider = TryGetManagedCircleCollider2DComponent(entityHandle))
                collider->Restitution = value;
        }

        bool ManagedGetCircleCollider2DIsSensorIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedCircleCollider2DComponent(entityHandle);
            return collider ? collider->IsSensor : false;
        }

        void ManagedSetCircleCollider2DIsSensorIcall(uint32_t entityHandle, bool value)
        {
            if (auto* collider = TryGetManagedCircleCollider2DComponent(entityHandle))
                collider->IsSensor = value;
        }

        uint64_t ManagedGetCircleCollider2DCollisionLayerIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedCircleCollider2DComponent(entityHandle);
            return collider ? collider->CollisionLayer : 1ull;
        }

        void ManagedSetCircleCollider2DCollisionLayerIcall(uint32_t entityHandle, uint64_t value)
        {
            if (auto* collider = TryGetManagedCircleCollider2DComponent(entityHandle))
                collider->CollisionLayer = value;
        }

        uint64_t ManagedGetCircleCollider2DCollisionMaskIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedCircleCollider2DComponent(entityHandle);
            return collider ? collider->CollisionMask : ~0ull;
        }

        void ManagedSetCircleCollider2DCollisionMaskIcall(uint32_t entityHandle, uint64_t value)
        {
            if (auto* collider = TryGetManagedCircleCollider2DComponent(entityHandle))
                collider->CollisionMask = value;
        }

        bool ManagedHasPolygonCollider2DComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedPolygonCollider2DComponent(entityHandle) != nullptr;
        }

        ManagedVector2 ManagedGetPolygonCollider2DOffsetIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedPolygonCollider2DComponent(entityHandle);
            return collider ? ToManagedVector2(collider->Offset) : ManagedVector2{};
        }

        void ManagedSetPolygonCollider2DOffsetIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* collider = TryGetManagedPolygonCollider2DComponent(entityHandle))
                collider->Offset = ToGlmVector2(value);
        }

        int ManagedGetPolygonCollider2DPointCountIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedPolygonCollider2DComponent(entityHandle);
            return collider ? static_cast<int>(collider->Points.size()) : 0;
        }

        void ManagedSetPolygonCollider2DPointCountIcall(uint32_t entityHandle, int pointCount)
        {
            if (auto* collider = TryGetManagedPolygonCollider2DComponent(entityHandle))
            {
                const size_t clampedPointCount = static_cast<size_t>(std::clamp(pointCount, 0, static_cast<int>(kPhysics2DPolygonMaxPoints)));
                collider->Points.resize(clampedPointCount, glm::vec2(0.0f));
            }
        }

        ManagedVector2 ManagedGetPolygonCollider2DPointIcall(uint32_t entityHandle, int index)
        {
            const auto* collider = TryGetManagedPolygonCollider2DComponent(entityHandle);
            if (collider == nullptr || index < 0 || static_cast<size_t>(index) >= collider->Points.size())
                return ManagedVector2{};
            return ToManagedVector2(collider->Points[static_cast<size_t>(index)]);
        }

        void ManagedSetPolygonCollider2DPointIcall(uint32_t entityHandle, int index, ManagedVector2 value)
        {
            if (auto* collider = TryGetManagedPolygonCollider2DComponent(entityHandle))
            {
                if (index < 0 || static_cast<size_t>(index) >= collider->Points.size())
                    return;
                collider->Points[static_cast<size_t>(index)] = ToGlmVector2(value);
            }
        }

        float ManagedGetPolygonCollider2DDensityIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedPolygonCollider2DComponent(entityHandle);
            return collider ? collider->Density : 1.0f;
        }

        void ManagedSetPolygonCollider2DDensityIcall(uint32_t entityHandle, float value)
        {
            if (auto* collider = TryGetManagedPolygonCollider2DComponent(entityHandle))
                collider->Density = value;
        }

        float ManagedGetPolygonCollider2DFrictionIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedPolygonCollider2DComponent(entityHandle);
            return collider ? collider->Friction : 0.5f;
        }

        void ManagedSetPolygonCollider2DFrictionIcall(uint32_t entityHandle, float value)
        {
            if (auto* collider = TryGetManagedPolygonCollider2DComponent(entityHandle))
                collider->Friction = value;
        }

        float ManagedGetPolygonCollider2DRestitutionIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedPolygonCollider2DComponent(entityHandle);
            return collider ? collider->Restitution : 0.0f;
        }

        void ManagedSetPolygonCollider2DRestitutionIcall(uint32_t entityHandle, float value)
        {
            if (auto* collider = TryGetManagedPolygonCollider2DComponent(entityHandle))
                collider->Restitution = value;
        }

        bool ManagedGetPolygonCollider2DIsSensorIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedPolygonCollider2DComponent(entityHandle);
            return collider ? collider->IsSensor : false;
        }

        void ManagedSetPolygonCollider2DIsSensorIcall(uint32_t entityHandle, bool value)
        {
            if (auto* collider = TryGetManagedPolygonCollider2DComponent(entityHandle))
                collider->IsSensor = value;
        }

        uint64_t ManagedGetPolygonCollider2DCollisionLayerIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedPolygonCollider2DComponent(entityHandle);
            return collider ? collider->CollisionLayer : 1ull;
        }

        void ManagedSetPolygonCollider2DCollisionLayerIcall(uint32_t entityHandle, uint64_t value)
        {
            if (auto* collider = TryGetManagedPolygonCollider2DComponent(entityHandle))
                collider->CollisionLayer = value;
        }

        uint64_t ManagedGetPolygonCollider2DCollisionMaskIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedPolygonCollider2DComponent(entityHandle);
            return collider ? collider->CollisionMask : ~0ull;
        }

        void ManagedSetPolygonCollider2DCollisionMaskIcall(uint32_t entityHandle, uint64_t value)
        {
            if (auto* collider = TryGetManagedPolygonCollider2DComponent(entityHandle))
                collider->CollisionMask = value;
        }

        bool ManagedHasEdgeCollider2DComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedEdgeCollider2DComponent(entityHandle) != nullptr;
        }

        ManagedVector2 ManagedGetEdgeCollider2DOffsetIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedEdgeCollider2DComponent(entityHandle);
            return collider ? ToManagedVector2(collider->Offset) : ManagedVector2{};
        }

        void ManagedSetEdgeCollider2DOffsetIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* collider = TryGetManagedEdgeCollider2DComponent(entityHandle))
                collider->Offset = ToGlmVector2(value);
        }

        ManagedVector2 ManagedGetEdgeCollider2DPointAIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedEdgeCollider2DComponent(entityHandle);
            return collider ? ToManagedVector2(collider->PointA) : ManagedVector2{ -0.5f, 0.0f };
        }

        void ManagedSetEdgeCollider2DPointAIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* collider = TryGetManagedEdgeCollider2DComponent(entityHandle))
                collider->PointA = ToGlmVector2(value);
        }

        ManagedVector2 ManagedGetEdgeCollider2DPointBIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedEdgeCollider2DComponent(entityHandle);
            return collider ? ToManagedVector2(collider->PointB) : ManagedVector2{ 0.5f, 0.0f };
        }

        void ManagedSetEdgeCollider2DPointBIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* collider = TryGetManagedEdgeCollider2DComponent(entityHandle))
                collider->PointB = ToGlmVector2(value);
        }

        float ManagedGetEdgeCollider2DFrictionIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedEdgeCollider2DComponent(entityHandle);
            return collider ? collider->Friction : 0.5f;
        }

        void ManagedSetEdgeCollider2DFrictionIcall(uint32_t entityHandle, float value)
        {
            if (auto* collider = TryGetManagedEdgeCollider2DComponent(entityHandle))
                collider->Friction = value;
        }

        float ManagedGetEdgeCollider2DRestitutionIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedEdgeCollider2DComponent(entityHandle);
            return collider ? collider->Restitution : 0.0f;
        }

        void ManagedSetEdgeCollider2DRestitutionIcall(uint32_t entityHandle, float value)
        {
            if (auto* collider = TryGetManagedEdgeCollider2DComponent(entityHandle))
                collider->Restitution = value;
        }

        bool ManagedGetEdgeCollider2DIsSensorIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedEdgeCollider2DComponent(entityHandle);
            return collider ? collider->IsSensor : false;
        }

        void ManagedSetEdgeCollider2DIsSensorIcall(uint32_t entityHandle, bool value)
        {
            if (auto* collider = TryGetManagedEdgeCollider2DComponent(entityHandle))
                collider->IsSensor = value;
        }

        uint64_t ManagedGetEdgeCollider2DCollisionLayerIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedEdgeCollider2DComponent(entityHandle);
            return collider ? collider->CollisionLayer : 1ull;
        }

        void ManagedSetEdgeCollider2DCollisionLayerIcall(uint32_t entityHandle, uint64_t value)
        {
            if (auto* collider = TryGetManagedEdgeCollider2DComponent(entityHandle))
                collider->CollisionLayer = value;
        }

        uint64_t ManagedGetEdgeCollider2DCollisionMaskIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedEdgeCollider2DComponent(entityHandle);
            return collider ? collider->CollisionMask : ~0ull;
        }

        void ManagedSetEdgeCollider2DCollisionMaskIcall(uint32_t entityHandle, uint64_t value)
        {
            if (auto* collider = TryGetManagedEdgeCollider2DComponent(entityHandle))
                collider->CollisionMask = value;
        }

        bool ManagedHasCapsuleCollider2DComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedCapsuleCollider2DComponent(entityHandle) != nullptr;
        }

        ManagedVector2 ManagedGetCapsuleCollider2DOffsetIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedCapsuleCollider2DComponent(entityHandle);
            return collider ? ToManagedVector2(collider->Offset) : ManagedVector2{};
        }

        void ManagedSetCapsuleCollider2DOffsetIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* collider = TryGetManagedCapsuleCollider2DComponent(entityHandle))
                collider->Offset = ToGlmVector2(value);
        }

        ManagedVector2 ManagedGetCapsuleCollider2DSizeIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedCapsuleCollider2DComponent(entityHandle);
            return collider ? ToManagedVector2(collider->Size) : ManagedVector2{ 1.0f, 2.0f };
        }

        void ManagedSetCapsuleCollider2DSizeIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* collider = TryGetManagedCapsuleCollider2DComponent(entityHandle))
                collider->Size = ToGlmVector2(value);
        }

        int ManagedGetCapsuleCollider2DDirectionIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedCapsuleCollider2DComponent(entityHandle);
            return collider ? static_cast<int>(collider->Direction) : static_cast<int>(CapsuleCollider2DComponent::Orientation::Vertical);
        }

        void ManagedSetCapsuleCollider2DDirectionIcall(uint32_t entityHandle, int value)
        {
            if (auto* collider = TryGetManagedCapsuleCollider2DComponent(entityHandle))
                collider->Direction = static_cast<CapsuleCollider2DComponent::Orientation>(value);
        }

        float ManagedGetCapsuleCollider2DDensityIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedCapsuleCollider2DComponent(entityHandle);
            return collider ? collider->Density : 1.0f;
        }

        void ManagedSetCapsuleCollider2DDensityIcall(uint32_t entityHandle, float value)
        {
            if (auto* collider = TryGetManagedCapsuleCollider2DComponent(entityHandle))
                collider->Density = value;
        }

        float ManagedGetCapsuleCollider2DFrictionIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedCapsuleCollider2DComponent(entityHandle);
            return collider ? collider->Friction : 0.5f;
        }

        void ManagedSetCapsuleCollider2DFrictionIcall(uint32_t entityHandle, float value)
        {
            if (auto* collider = TryGetManagedCapsuleCollider2DComponent(entityHandle))
                collider->Friction = value;
        }

        float ManagedGetCapsuleCollider2DRestitutionIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedCapsuleCollider2DComponent(entityHandle);
            return collider ? collider->Restitution : 0.0f;
        }

        void ManagedSetCapsuleCollider2DRestitutionIcall(uint32_t entityHandle, float value)
        {
            if (auto* collider = TryGetManagedCapsuleCollider2DComponent(entityHandle))
                collider->Restitution = value;
        }

        bool ManagedGetCapsuleCollider2DIsSensorIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedCapsuleCollider2DComponent(entityHandle);
            return collider ? collider->IsSensor : false;
        }

        void ManagedSetCapsuleCollider2DIsSensorIcall(uint32_t entityHandle, bool value)
        {
            if (auto* collider = TryGetManagedCapsuleCollider2DComponent(entityHandle))
                collider->IsSensor = value;
        }

        uint64_t ManagedGetCapsuleCollider2DCollisionLayerIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedCapsuleCollider2DComponent(entityHandle);
            return collider ? collider->CollisionLayer : 1ull;
        }

        void ManagedSetCapsuleCollider2DCollisionLayerIcall(uint32_t entityHandle, uint64_t value)
        {
            if (auto* collider = TryGetManagedCapsuleCollider2DComponent(entityHandle))
                collider->CollisionLayer = value;
        }

        uint64_t ManagedGetCapsuleCollider2DCollisionMaskIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedCapsuleCollider2DComponent(entityHandle);
            return collider ? collider->CollisionMask : ~0ull;
        }

        void ManagedSetCapsuleCollider2DCollisionMaskIcall(uint32_t entityHandle, uint64_t value)
        {
            if (auto* collider = TryGetManagedCapsuleCollider2DComponent(entityHandle))
                collider->CollisionMask = value;
        }

        bool ManagedHasJoint2DComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedJoint2DComponent(entityHandle) != nullptr;
        }

        int ManagedGetJoint2DTypeIcall(uint32_t entityHandle)
        {
            const auto* joint = TryGetManagedJoint2DComponent(entityHandle);
            return joint ? static_cast<int>(joint->Type) : static_cast<int>(Joint2DComponent::JointType::Distance);
        }

        void ManagedSetJoint2DTypeIcall(uint32_t entityHandle, int value)
        {
            if (auto* joint = TryGetManagedJoint2DComponent(entityHandle))
                joint->Type = static_cast<Joint2DComponent::JointType>(value);
        }

        uint32_t ManagedGetJoint2DConnectedEntityIcall(uint32_t entityHandle)
        {
            if (s_HostState.ActiveScene == nullptr)
                return static_cast<uint32_t>(entt::null);

            const auto* joint = TryGetManagedJoint2DComponent(entityHandle);
            if (joint == nullptr || joint->ConnectedEntity == entt::null)
                return static_cast<uint32_t>(entt::null);
            return static_cast<uint32_t>(s_HostState.ActiveScene->ResolveEntityReference(joint->ConnectedEntity));
        }

        void ManagedSetJoint2DConnectedEntityIcall(uint32_t entityHandle, uint32_t connectedEntityHandle)
        {
            if (auto* joint = TryGetManagedJoint2DComponent(entityHandle))
            {
                if (connectedEntityHandle == static_cast<uint32_t>(entt::null))
                {
                    joint->ConnectedEntity = entt::null;
                    return;
                }

                joint->ConnectedEntity = ResolveManagedEntityHandle(connectedEntityHandle);
            }
        }

        bool ManagedGetJoint2DCollideConnectedIcall(uint32_t entityHandle)
        {
            const auto* joint = TryGetManagedJoint2DComponent(entityHandle);
            return joint ? joint->CollideConnected : false;
        }

        void ManagedSetJoint2DCollideConnectedIcall(uint32_t entityHandle, bool value)
        {
            if (auto* joint = TryGetManagedJoint2DComponent(entityHandle))
                joint->CollideConnected = value;
        }

        ManagedVector2 ManagedGetJoint2DAnchorAIcall(uint32_t entityHandle)
        {
            const auto* joint = TryGetManagedJoint2DComponent(entityHandle);
            return joint ? ToManagedVector2(joint->AnchorA) : ManagedVector2{};
        }

        void ManagedSetJoint2DAnchorAIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* joint = TryGetManagedJoint2DComponent(entityHandle))
                joint->AnchorA = ToGlmVector2(value);
        }

        ManagedVector2 ManagedGetJoint2DAnchorBIcall(uint32_t entityHandle)
        {
            const auto* joint = TryGetManagedJoint2DComponent(entityHandle);
            return joint ? ToManagedVector2(joint->AnchorB) : ManagedVector2{};
        }

        void ManagedSetJoint2DAnchorBIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* joint = TryGetManagedJoint2DComponent(entityHandle))
                joint->AnchorB = ToGlmVector2(value);
        }

        ManagedVector2 ManagedGetJoint2DAxisIcall(uint32_t entityHandle)
        {
            const auto* joint = TryGetManagedJoint2DComponent(entityHandle);
            return joint ? ToManagedVector2(joint->Axis) : ManagedVector2{ 1.0f, 0.0f };
        }

        void ManagedSetJoint2DAxisIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* joint = TryGetManagedJoint2DComponent(entityHandle))
                joint->Axis = ToGlmVector2(value);
        }

        bool ManagedGetJoint2DEnableLimitIcall(uint32_t entityHandle)
        {
            const auto* joint = TryGetManagedJoint2DComponent(entityHandle);
            return joint ? joint->EnableLimit : false;
        }

        void ManagedSetJoint2DEnableLimitIcall(uint32_t entityHandle, bool value)
        {
            if (auto* joint = TryGetManagedJoint2DComponent(entityHandle))
                joint->EnableLimit = value;
        }

        ManagedVector2 ManagedGetJoint2DLimitsIcall(uint32_t entityHandle)
        {
            const auto* joint = TryGetManagedJoint2DComponent(entityHandle);
            return joint ? ToManagedVector2(joint->Limits) : ManagedVector2{ -1.0f, 1.0f };
        }

        void ManagedSetJoint2DLimitsIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* joint = TryGetManagedJoint2DComponent(entityHandle))
                joint->Limits = ToGlmVector2(value);
        }

        bool ManagedGetJoint2DEnableMotorIcall(uint32_t entityHandle)
        {
            const auto* joint = TryGetManagedJoint2DComponent(entityHandle);
            return joint ? joint->EnableMotor : false;
        }

        void ManagedSetJoint2DEnableMotorIcall(uint32_t entityHandle, bool value)
        {
            if (auto* joint = TryGetManagedJoint2DComponent(entityHandle))
                joint->EnableMotor = value;
        }

        float ManagedGetJoint2DMotorSpeedIcall(uint32_t entityHandle)
        {
            const auto* joint = TryGetManagedJoint2DComponent(entityHandle);
            return joint ? joint->MotorSpeed : 0.0f;
        }

        void ManagedSetJoint2DMotorSpeedIcall(uint32_t entityHandle, float value)
        {
            if (auto* joint = TryGetManagedJoint2DComponent(entityHandle))
                joint->MotorSpeed = value;
        }

        float ManagedGetJoint2DMaxMotorForceOrTorqueIcall(uint32_t entityHandle)
        {
            const auto* joint = TryGetManagedJoint2DComponent(entityHandle);
            return joint ? joint->MaxMotorForceOrTorque : 10.0f;
        }

        void ManagedSetJoint2DMaxMotorForceOrTorqueIcall(uint32_t entityHandle, float value)
        {
            if (auto* joint = TryGetManagedJoint2DComponent(entityHandle))
                joint->MaxMotorForceOrTorque = value;
        }

        bool ManagedGetJoint2DEnableSpringIcall(uint32_t entityHandle)
        {
            const auto* joint = TryGetManagedJoint2DComponent(entityHandle);
            return joint ? joint->EnableSpring : false;
        }

        void ManagedSetJoint2DEnableSpringIcall(uint32_t entityHandle, bool value)
        {
            if (auto* joint = TryGetManagedJoint2DComponent(entityHandle))
                joint->EnableSpring = value;
        }

        float ManagedGetJoint2DHertzIcall(uint32_t entityHandle)
        {
            const auto* joint = TryGetManagedJoint2DComponent(entityHandle);
            return joint ? joint->Hertz : 5.0f;
        }

        void ManagedSetJoint2DHertzIcall(uint32_t entityHandle, float value)
        {
            if (auto* joint = TryGetManagedJoint2DComponent(entityHandle))
                joint->Hertz = value;
        }

        float ManagedGetJoint2DDampingRatioIcall(uint32_t entityHandle)
        {
            const auto* joint = TryGetManagedJoint2DComponent(entityHandle);
            return joint ? joint->DampingRatio : 0.7f;
        }

        void ManagedSetJoint2DDampingRatioIcall(uint32_t entityHandle, float value)
        {
            if (auto* joint = TryGetManagedJoint2DComponent(entityHandle))
                joint->DampingRatio = value;
        }

        bool ManagedHasSpriteComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedSpriteComponent(entityHandle) != nullptr;
        }

        Coral::String ManagedGetSpriteTextureKeyIcall(uint32_t entityHandle)
        {
            const auto* sprite = TryGetManagedSpriteComponent(entityHandle);
            return sprite ? Coral::String::New(sprite->TextureKey) : Coral::String::New("");
        }

        void ManagedSetSpriteTextureKeyIcall(uint32_t entityHandle, Coral::String textureKey)
        {
            if (auto* sprite = TryGetManagedSpriteComponent(entityHandle))
                sprite->TextureKey = ToUtf8Borrowed(textureKey);
        }

        ManagedVector4 ManagedGetSpriteColorIcall(uint32_t entityHandle)
        {
            const auto* sprite = TryGetManagedSpriteComponent(entityHandle);
            return sprite ? ToManagedVector4(sprite->Color) : ManagedVector4{ 1.0f, 1.0f, 1.0f, 1.0f };
        }

        void ManagedSetSpriteColorIcall(uint32_t entityHandle, ManagedVector4 value)
        {
            if (auto* sprite = TryGetManagedSpriteComponent(entityHandle))
                sprite->Color = ToGlmVector4(value);
        }

        ManagedVector2 ManagedGetSpriteTilingFactorIcall(uint32_t entityHandle)
        {
            const auto* sprite = TryGetManagedSpriteComponent(entityHandle);
            return sprite ? ToManagedVector2(sprite->TilingFactor) : ManagedVector2{ 1.0f, 1.0f };
        }

        void ManagedSetSpriteTilingFactorIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* sprite = TryGetManagedSpriteComponent(entityHandle))
                sprite->TilingFactor = ToGlmVector2(value);
        }

        int ManagedGetSpriteRenderOrderIcall(uint32_t entityHandle)
        {
            const auto* sprite = TryGetManagedSpriteComponent(entityHandle);
            return sprite ? sprite->RenderOrder : 0;
        }

        void ManagedSetSpriteRenderOrderIcall(uint32_t entityHandle, int value)
        {
            if (auto* sprite = TryGetManagedSpriteComponent(entityHandle))
                sprite->RenderOrder = value;
        }

        bool ManagedGetSpriteCastShadowsIcall(uint32_t entityHandle)
        {
            const auto* sprite = TryGetManagedSpriteComponent(entityHandle);
            return sprite ? sprite->CastShadows : true;
        }

        void ManagedSetSpriteCastShadowsIcall(uint32_t entityHandle, bool value)
        {
            if (auto* sprite = TryGetManagedSpriteComponent(entityHandle))
                sprite->CastShadows = value;
        }

        bool ManagedGetSpriteReceiveShadowsIcall(uint32_t entityHandle)
        {
            const auto* sprite = TryGetManagedSpriteComponent(entityHandle);
            return sprite ? sprite->ReceiveShadows : true;
        }

        void ManagedSetSpriteReceiveShadowsIcall(uint32_t entityHandle, bool value)
        {
            if (auto* sprite = TryGetManagedSpriteComponent(entityHandle))
                sprite->ReceiveShadows = value;
        }

        int ManagedGetSpriteSubSpriteIndexIcall(uint32_t entityHandle)
        {
            const auto* sprite = TryGetManagedSpriteComponent(entityHandle);
            return sprite ? sprite->SubSpriteIndex : -1;
        }

        void ManagedSetSpriteSubSpriteIndexIcall(uint32_t entityHandle, int value)
        {
            if (auto* sprite = TryGetManagedSpriteComponent(entityHandle))
                sprite->SubSpriteIndex = value;
        }

        ManagedVector2 ManagedGetSpriteUvMinIcall(uint32_t entityHandle)
        {
            const auto* sprite = TryGetManagedSpriteComponent(entityHandle);
            return sprite ? ToManagedVector2(sprite->UvMin) : ManagedVector2{};
        }

        void ManagedSetSpriteUvMinIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* sprite = TryGetManagedSpriteComponent(entityHandle))
                sprite->UvMin = ToGlmVector2(value);
        }

        ManagedVector2 ManagedGetSpriteUvMaxIcall(uint32_t entityHandle)
        {
            const auto* sprite = TryGetManagedSpriteComponent(entityHandle);
            return sprite ? ToManagedVector2(sprite->UvMax) : ManagedVector2{ 1.0f, 1.0f };
        }

        void ManagedSetSpriteUvMaxIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* sprite = TryGetManagedSpriteComponent(entityHandle))
                sprite->UvMax = ToGlmVector2(value);
        }

        bool ManagedHasMaterialComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedMaterialComponent(entityHandle) != nullptr;
        }

        Coral::String ManagedGetMaterialKeyIcall(uint32_t entityHandle)
        {
            const auto* material = TryGetManagedMaterialComponent(entityHandle);
            return material ? Coral::String::New(material->MaterialKey) : Coral::String::New("");
        }

        void ManagedSetMaterialKeyIcall(uint32_t entityHandle, Coral::String materialKey)
        {
            if (auto* material = TryGetManagedMaterialComponent(entityHandle))
                material->MaterialKey = ToUtf8Borrowed(materialKey);
        }

        bool ManagedHasCanvasComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedCanvasComponent(entityHandle) != nullptr;
        }

        int ManagedGetCanvasRenderModeIcall(uint32_t entityHandle)
        {
            const auto* canvas = TryGetManagedCanvasComponent(entityHandle);
            return canvas ? static_cast<int>(canvas->Mode) : static_cast<int>(CanvasComponent::RenderMode::ScreenSpace);
        }

        void ManagedSetCanvasRenderModeIcall(uint32_t entityHandle, int value)
        {
            if (auto* canvas = TryGetManagedCanvasComponent(entityHandle))
                canvas->Mode = static_cast<CanvasComponent::RenderMode>(value);
        }

        int ManagedGetCanvasSortOrderIcall(uint32_t entityHandle)
        {
            const auto* canvas = TryGetManagedCanvasComponent(entityHandle);
            return canvas ? canvas->SortOrder : 0;
        }

        void ManagedSetCanvasSortOrderIcall(uint32_t entityHandle, int value)
        {
            if (auto* canvas = TryGetManagedCanvasComponent(entityHandle))
                canvas->SortOrder = value;
        }

        ManagedVector2 ManagedGetCanvasReferenceResolutionIcall(uint32_t entityHandle)
        {
            const auto* canvas = TryGetManagedCanvasComponent(entityHandle);
            return canvas ? ToManagedVector2(canvas->ReferenceResolution) : ManagedVector2{ 1920.0f, 1080.0f };
        }

        void ManagedSetCanvasReferenceResolutionIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* canvas = TryGetManagedCanvasComponent(entityHandle))
                canvas->ReferenceResolution = ToGlmVector2(value);
        }

        bool ManagedHasRectTransformComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedRectTransformComponent(entityHandle) != nullptr;
        }

        ManagedVector2 ManagedGetRectTransformAnchorMinIcall(uint32_t entityHandle)
        {
            const auto* rectTransform = TryGetManagedRectTransformComponent(entityHandle);
            return rectTransform ? ToManagedVector2(rectTransform->AnchorMin) : ManagedVector2{ 0.5f, 0.5f };
        }

        void ManagedSetRectTransformAnchorMinIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* rectTransform = TryGetManagedRectTransformComponent(entityHandle))
                rectTransform->AnchorMin = ToGlmVector2(value);
        }

        ManagedVector2 ManagedGetRectTransformAnchorMaxIcall(uint32_t entityHandle)
        {
            const auto* rectTransform = TryGetManagedRectTransformComponent(entityHandle);
            return rectTransform ? ToManagedVector2(rectTransform->AnchorMax) : ManagedVector2{ 0.5f, 0.5f };
        }

        void ManagedSetRectTransformAnchorMaxIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* rectTransform = TryGetManagedRectTransformComponent(entityHandle))
                rectTransform->AnchorMax = ToGlmVector2(value);
        }

        ManagedVector2 ManagedGetRectTransformPivotIcall(uint32_t entityHandle)
        {
            const auto* rectTransform = TryGetManagedRectTransformComponent(entityHandle);
            return rectTransform ? ToManagedVector2(rectTransform->Pivot) : ManagedVector2{ 0.5f, 0.5f };
        }

        void ManagedSetRectTransformPivotIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* rectTransform = TryGetManagedRectTransformComponent(entityHandle))
                rectTransform->Pivot = ToGlmVector2(value);
        }

        ManagedVector2 ManagedGetRectTransformSizeDeltaIcall(uint32_t entityHandle)
        {
            const auto* rectTransform = TryGetManagedRectTransformComponent(entityHandle);
            return rectTransform ? ToManagedVector2(rectTransform->SizeDelta) : ManagedVector2{ 100.0f, 40.0f };
        }

        void ManagedSetRectTransformSizeDeltaIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* rectTransform = TryGetManagedRectTransformComponent(entityHandle))
                rectTransform->SizeDelta = ToGlmVector2(value);
        }

        ManagedVector2 ManagedGetRectTransformAnchoredPositionIcall(uint32_t entityHandle)
        {
            const auto* rectTransform = TryGetManagedRectTransformComponent(entityHandle);
            return rectTransform ? ToManagedVector2(rectTransform->AnchoredPosition) : ManagedVector2{};
        }

        void ManagedSetRectTransformAnchoredPositionIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* rectTransform = TryGetManagedRectTransformComponent(entityHandle))
                rectTransform->AnchoredPosition = ToGlmVector2(value);
        }

        bool ManagedHasDirectionalLight2DComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedDirectionalLight2DComponent(entityHandle) != nullptr;
        }

        bool ManagedGetDirectionalLight2DEnabledIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle);
            return light ? light->Enabled : true;
        }

        void ManagedSetDirectionalLight2DEnabledIcall(uint32_t entityHandle, bool value)
        {
            if (auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle))
                light->Enabled = value;
        }

        ManagedVector3 ManagedGetDirectionalLight2DColorIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle);
            return light ? ToManagedVector3(light->Color) : ManagedVector3{ 1.0f, 1.0f, 1.0f };
        }

        void ManagedSetDirectionalLight2DColorIcall(uint32_t entityHandle, ManagedVector3 value)
        {
            if (auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle))
                light->Color = ToGlmVector3(value);
        }

        float ManagedGetDirectionalLight2DIntensityIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle);
            return light ? light->Intensity : 1.0f;
        }

        void ManagedSetDirectionalLight2DIntensityIcall(uint32_t entityHandle, float value)
        {
            if (auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle))
                light->Intensity = value;
        }

        bool ManagedGetDirectionalLight2DUseEntityRotationIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle);
            return light ? light->UseEntityRotation : true;
        }

        void ManagedSetDirectionalLight2DUseEntityRotationIcall(uint32_t entityHandle, bool value)
        {
            if (auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle))
                light->UseEntityRotation = value;
        }

        ManagedVector2 ManagedGetDirectionalLight2DDirectionIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle);
            return light ? ToManagedVector2(light->Direction) : ManagedVector2{ 0.0f, -1.0f };
        }

        void ManagedSetDirectionalLight2DDirectionIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle))
                light->Direction = ToGlmVector2(value);
        }

        bool ManagedGetDirectionalLight2DCastShadowsIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle);
            return light ? light->CastShadows : true;
        }

        void ManagedSetDirectionalLight2DCastShadowsIcall(uint32_t entityHandle, bool value)
        {
            if (auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle))
                light->CastShadows = value;
        }

        float ManagedGetDirectionalLight2DShadowStrengthIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle);
            return light ? light->ShadowStrength : 1.0f;
        }

        void ManagedSetDirectionalLight2DShadowStrengthIcall(uint32_t entityHandle, float value)
        {
            if (auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle))
                light->ShadowStrength = value;
        }

        float ManagedGetDirectionalLight2DShadowSoftnessIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle);
            return light ? light->ShadowSoftness : 1.0f;
        }

        void ManagedSetDirectionalLight2DShadowSoftnessIcall(uint32_t entityHandle, float value)
        {
            if (auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle))
                light->ShadowSoftness = value;
        }

        int ManagedGetDirectionalLight2DShadowSamplesIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle);
            return light ? light->ShadowSamples : 8;
        }

        void ManagedSetDirectionalLight2DShadowSamplesIcall(uint32_t entityHandle, int value)
        {
            if (auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle))
                light->ShadowSamples = value;
        }

        float ManagedGetDirectionalLight2DShadowDistanceIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle);
            return light ? light->ShadowDistance : 25.0f;
        }

        void ManagedSetDirectionalLight2DShadowDistanceIcall(uint32_t entityHandle, float value)
        {
            if (auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle))
                light->ShadowDistance = value;
        }

        float ManagedGetDirectionalLight2DShadowBiasIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle);
            return light ? light->ShadowBias : 0.02f;
        }

        void ManagedSetDirectionalLight2DShadowBiasIcall(uint32_t entityHandle, float value)
        {
            if (auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle))
                light->ShadowBias = value;
        }

        bool ManagedHasPointLight2DComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedPointLight2DComponent(entityHandle) != nullptr;
        }

        bool ManagedGetPointLight2DEnabledIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedPointLight2DComponent(entityHandle);
            return light ? light->Enabled : true;
        }

        void ManagedSetPointLight2DEnabledIcall(uint32_t entityHandle, bool value)
        {
            if (auto* light = TryGetManagedPointLight2DComponent(entityHandle))
                light->Enabled = value;
        }

        ManagedVector3 ManagedGetPointLight2DColorIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedPointLight2DComponent(entityHandle);
            return light ? ToManagedVector3(light->Color) : ManagedVector3{ 1.0f, 1.0f, 1.0f };
        }

        void ManagedSetPointLight2DColorIcall(uint32_t entityHandle, ManagedVector3 value)
        {
            if (auto* light = TryGetManagedPointLight2DComponent(entityHandle))
                light->Color = ToGlmVector3(value);
        }

        float ManagedGetPointLight2DIntensityIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedPointLight2DComponent(entityHandle);
            return light ? light->Intensity : 1.0f;
        }

        void ManagedSetPointLight2DIntensityIcall(uint32_t entityHandle, float value)
        {
            if (auto* light = TryGetManagedPointLight2DComponent(entityHandle))
                light->Intensity = value;
        }

        float ManagedGetPointLight2DRadiusIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedPointLight2DComponent(entityHandle);
            return light ? light->Radius : 5.0f;
        }

        void ManagedSetPointLight2DRadiusIcall(uint32_t entityHandle, float value)
        {
            if (auto* light = TryGetManagedPointLight2DComponent(entityHandle))
                light->Radius = value;
        }

        float ManagedGetPointLight2DFalloffIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedPointLight2DComponent(entityHandle);
            return light ? light->Falloff : 2.0f;
        }

        void ManagedSetPointLight2DFalloffIcall(uint32_t entityHandle, float value)
        {
            if (auto* light = TryGetManagedPointLight2DComponent(entityHandle))
                light->Falloff = value;
        }

        bool ManagedGetPointLight2DCastShadowsIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedPointLight2DComponent(entityHandle);
            return light ? light->CastShadows : true;
        }

        void ManagedSetPointLight2DCastShadowsIcall(uint32_t entityHandle, bool value)
        {
            if (auto* light = TryGetManagedPointLight2DComponent(entityHandle))
                light->CastShadows = value;
        }

        float ManagedGetPointLight2DShadowStrengthIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedPointLight2DComponent(entityHandle);
            return light ? light->ShadowStrength : 1.0f;
        }

        void ManagedSetPointLight2DShadowStrengthIcall(uint32_t entityHandle, float value)
        {
            if (auto* light = TryGetManagedPointLight2DComponent(entityHandle))
                light->ShadowStrength = value;
        }

        float ManagedGetPointLight2DShadowSoftnessIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedPointLight2DComponent(entityHandle);
            return light ? light->ShadowSoftness : 1.0f;
        }

        void ManagedSetPointLight2DShadowSoftnessIcall(uint32_t entityHandle, float value)
        {
            if (auto* light = TryGetManagedPointLight2DComponent(entityHandle))
                light->ShadowSoftness = value;
        }

        int ManagedGetPointLight2DShadowSamplesIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedPointLight2DComponent(entityHandle);
            return light ? light->ShadowSamples : 8;
        }

        void ManagedSetPointLight2DShadowSamplesIcall(uint32_t entityHandle, int value)
        {
            if (auto* light = TryGetManagedPointLight2DComponent(entityHandle))
                light->ShadowSamples = value;
        }

        float ManagedGetPointLight2DShadowBiasIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedPointLight2DComponent(entityHandle);
            return light ? light->ShadowBias : 0.0015f;
        }

        void ManagedSetPointLight2DShadowBiasIcall(uint32_t entityHandle, float value)
        {
            if (auto* light = TryGetManagedPointLight2DComponent(entityHandle))
                light->ShadowBias = value;
        }

        bool ManagedHasUIImageComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedUIImageComponent(entityHandle) != nullptr;
        }

        bool ManagedGetUIImageRaycastTargetIcall(uint32_t entityHandle)
        {
            const auto* image = TryGetManagedUIImageComponent(entityHandle);
            return image ? image->RaycastTarget : true;
        }

        void ManagedSetUIImageRaycastTargetIcall(uint32_t entityHandle, bool value)
        {
            if (auto* image = TryGetManagedUIImageComponent(entityHandle))
                image->RaycastTarget = value;
        }

        bool ManagedHasUIPanelComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedUIPanelComponent(entityHandle) != nullptr;
        }

        ManagedVector4 ManagedGetUIPanelBackgroundColorIcall(uint32_t entityHandle)
        {
            const auto* panel = TryGetManagedUIPanelComponent(entityHandle);
            return panel ? ToManagedVector4(panel->BackgroundColor) : ManagedVector4{ 0.12f, 0.12f, 0.12f, 0.9f };
        }

        void ManagedSetUIPanelBackgroundColorIcall(uint32_t entityHandle, ManagedVector4 value)
        {
            if (auto* panel = TryGetManagedUIPanelComponent(entityHandle))
                panel->BackgroundColor = ToGlmVector4(value);
        }

        bool ManagedGetUIPanelUseSpriteTextureIcall(uint32_t entityHandle)
        {
            const auto* panel = TryGetManagedUIPanelComponent(entityHandle);
            return panel ? panel->UseSpriteTexture : false;
        }

        void ManagedSetUIPanelUseSpriteTextureIcall(uint32_t entityHandle, bool value)
        {
            if (auto* panel = TryGetManagedUIPanelComponent(entityHandle))
                panel->UseSpriteTexture = value;
        }

        bool ManagedGetUIPanelRaycastTargetIcall(uint32_t entityHandle)
        {
            const auto* panel = TryGetManagedUIPanelComponent(entityHandle);
            return panel ? panel->RaycastTarget : false;
        }

        void ManagedSetUIPanelRaycastTargetIcall(uint32_t entityHandle, bool value)
        {
            if (auto* panel = TryGetManagedUIPanelComponent(entityHandle))
                panel->RaycastTarget = value;
        }

        bool ManagedHasUITextComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedUITextComponent(entityHandle) != nullptr;
        }

        Coral::String ManagedGetUITextValueIcall(uint32_t entityHandle)
        {
            const auto* text = TryGetManagedUITextComponent(entityHandle);
            return text ? Coral::String::New(text->Text) : Coral::String::New("");
        }

        void ManagedSetUITextValueIcall(uint32_t entityHandle, Coral::String value)
        {
            if (auto* text = TryGetManagedUITextComponent(entityHandle))
                text->Text = ToUtf8Borrowed(value);
        }

        Coral::String ManagedGetUITextFontFilePathIcall(uint32_t entityHandle)
        {
            const auto* text = TryGetManagedUITextComponent(entityHandle);
            return text ? Coral::String::New(text->FontFilePath) : Coral::String::New("");
        }

        void ManagedSetUITextFontFilePathIcall(uint32_t entityHandle, Coral::String value)
        {
            if (auto* text = TryGetManagedUITextComponent(entityHandle))
                text->FontFilePath = ToUtf8Borrowed(value);
        }

        float ManagedGetUITextFontSizeIcall(uint32_t entityHandle)
        {
            const auto* text = TryGetManagedUITextComponent(entityHandle);
            return text ? text->FontSize : 32.0f;
        }

        void ManagedSetUITextFontSizeIcall(uint32_t entityHandle, float value)
        {
            if (auto* text = TryGetManagedUITextComponent(entityHandle))
                text->FontSize = value;
        }

        ManagedVector4 ManagedGetUITextColorIcall(uint32_t entityHandle)
        {
            const auto* text = TryGetManagedUITextComponent(entityHandle);
            return text ? ToManagedVector4(text->Color) : ManagedVector4{ 1.0f, 1.0f, 1.0f, 1.0f };
        }

        void ManagedSetUITextColorIcall(uint32_t entityHandle, ManagedVector4 value)
        {
            if (auto* text = TryGetManagedUITextComponent(entityHandle))
                text->Color = ToGlmVector4(value);
        }

        bool ManagedGetUITextRaycastTargetIcall(uint32_t entityHandle)
        {
            const auto* text = TryGetManagedUITextComponent(entityHandle);
            return text ? text->RaycastTarget : false;
        }

        void ManagedSetUITextRaycastTargetIcall(uint32_t entityHandle, bool value)
        {
            if (auto* text = TryGetManagedUITextComponent(entityHandle))
                text->RaycastTarget = value;
        }

        bool ManagedHasUIButtonComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedUIButtonComponent(entityHandle) != nullptr;
        }

        bool ManagedGetUIButtonInteractableIcall(uint32_t entityHandle)
        {
            const auto* button = TryGetManagedUIButtonComponent(entityHandle);
            return button ? button->Interactable : true;
        }

        void ManagedSetUIButtonInteractableIcall(uint32_t entityHandle, bool value)
        {
            if (auto* button = TryGetManagedUIButtonComponent(entityHandle))
                button->Interactable = value;
        }

        bool ManagedGetUIButtonUseStateColorsIcall(uint32_t entityHandle)
        {
            const auto* button = TryGetManagedUIButtonComponent(entityHandle);
            return button ? button->UseStateColors : true;
        }

        void ManagedSetUIButtonUseStateColorsIcall(uint32_t entityHandle, bool value)
        {
            if (auto* button = TryGetManagedUIButtonComponent(entityHandle))
                button->UseStateColors = value;
        }

        ManagedVector4 ManagedGetUIButtonNormalColorIcall(uint32_t entityHandle)
        {
            const auto* button = TryGetManagedUIButtonComponent(entityHandle);
            return button ? ToManagedVector4(button->NormalColor) : ManagedVector4{ 0.82f, 0.82f, 0.82f, 1.0f };
        }

        void ManagedSetUIButtonNormalColorIcall(uint32_t entityHandle, ManagedVector4 value)
        {
            if (auto* button = TryGetManagedUIButtonComponent(entityHandle))
                button->NormalColor = ToGlmVector4(value);
        }

        ManagedVector4 ManagedGetUIButtonHoveredColorIcall(uint32_t entityHandle)
        {
            const auto* button = TryGetManagedUIButtonComponent(entityHandle);
            return button ? ToManagedVector4(button->HoveredColor) : ManagedVector4{ 0.92f, 0.92f, 0.92f, 1.0f };
        }

        void ManagedSetUIButtonHoveredColorIcall(uint32_t entityHandle, ManagedVector4 value)
        {
            if (auto* button = TryGetManagedUIButtonComponent(entityHandle))
                button->HoveredColor = ToGlmVector4(value);
        }

        ManagedVector4 ManagedGetUIButtonPressedColorIcall(uint32_t entityHandle)
        {
            const auto* button = TryGetManagedUIButtonComponent(entityHandle);
            return button ? ToManagedVector4(button->PressedColor) : ManagedVector4{ 0.72f, 0.72f, 0.72f, 1.0f };
        }

        void ManagedSetUIButtonPressedColorIcall(uint32_t entityHandle, ManagedVector4 value)
        {
            if (auto* button = TryGetManagedUIButtonComponent(entityHandle))
                button->PressedColor = ToGlmVector4(value);
        }

        ManagedVector4 ManagedGetUIButtonDisabledColorIcall(uint32_t entityHandle)
        {
            const auto* button = TryGetManagedUIButtonComponent(entityHandle);
            return button ? ToManagedVector4(button->DisabledColor) : ManagedVector4{ 0.45f, 0.45f, 0.45f, 1.0f };
        }

        void ManagedSetUIButtonDisabledColorIcall(uint32_t entityHandle, ManagedVector4 value)
        {
            if (auto* button = TryGetManagedUIButtonComponent(entityHandle))
                button->DisabledColor = ToGlmVector4(value);
        }

        bool ManagedGetUIButtonIsHoveredIcall(uint32_t entityHandle)
        {
            const auto* button = TryGetManagedUIButtonComponent(entityHandle);
            return button ? button->IsHovered : false;
        }

        bool ManagedGetUIButtonIsPressedIcall(uint32_t entityHandle)
        {
            const auto* button = TryGetManagedUIButtonComponent(entityHandle);
            return button ? button->IsPressed : false;
        }

        Coral::String ManagedGetUIButtonOnClickEventIcall(uint32_t entityHandle)
        {
            const auto* button = TryGetManagedUIButtonComponent(entityHandle);
            return button ? Coral::String::New(button->OnClickEvent) : Coral::String::New("");
        }

        void ManagedSetUIButtonOnClickEventIcall(uint32_t entityHandle, Coral::String value)
        {
            if (auto* button = TryGetManagedUIButtonComponent(entityHandle))
                button->OnClickEvent = ToUtf8Borrowed(value);
        }

        Coral::String ManagedGetUIButtonOnHoverEnterEventIcall(uint32_t entityHandle)
        {
            const auto* button = TryGetManagedUIButtonComponent(entityHandle);
            return button ? Coral::String::New(button->OnHoverEnterEvent) : Coral::String::New("");
        }

        void ManagedSetUIButtonOnHoverEnterEventIcall(uint32_t entityHandle, Coral::String value)
        {
            if (auto* button = TryGetManagedUIButtonComponent(entityHandle))
                button->OnHoverEnterEvent = ToUtf8Borrowed(value);
        }

        Coral::String ManagedGetUIButtonOnHoverExitEventIcall(uint32_t entityHandle)
        {
            const auto* button = TryGetManagedUIButtonComponent(entityHandle);
            return button ? Coral::String::New(button->OnHoverExitEvent) : Coral::String::New("");
        }

        void ManagedSetUIButtonOnHoverExitEventIcall(uint32_t entityHandle, Coral::String value)
        {
            if (auto* button = TryGetManagedUIButtonComponent(entityHandle))
                button->OnHoverExitEvent = ToUtf8Borrowed(value);
        }

        Coral::String ManagedGetUIButtonOnPressedEventIcall(uint32_t entityHandle)
        {
            const auto* button = TryGetManagedUIButtonComponent(entityHandle);
            return button ? Coral::String::New(button->OnPressedEvent) : Coral::String::New("");
        }

        void ManagedSetUIButtonOnPressedEventIcall(uint32_t entityHandle, Coral::String value)
        {
            if (auto* button = TryGetManagedUIButtonComponent(entityHandle))
                button->OnPressedEvent = ToUtf8Borrowed(value);
        }

        bool ManagedHasUISliderComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedUISliderComponent(entityHandle) != nullptr;
        }

        bool ManagedGetUISliderInteractableIcall(uint32_t entityHandle)
        {
            const auto* slider = TryGetManagedUISliderComponent(entityHandle);
            return slider ? slider->Interactable : true;
        }

        void ManagedSetUISliderInteractableIcall(uint32_t entityHandle, bool value)
        {
            if (auto* slider = TryGetManagedUISliderComponent(entityHandle))
                slider->Interactable = value;
        }

        float ManagedGetUISliderMinValueIcall(uint32_t entityHandle)
        {
            const auto* slider = TryGetManagedUISliderComponent(entityHandle);
            return slider ? slider->MinValue : 0.0f;
        }

        void ManagedSetUISliderMinValueIcall(uint32_t entityHandle, float value)
        {
            if (auto* slider = TryGetManagedUISliderComponent(entityHandle))
                slider->MinValue = value;
        }

        float ManagedGetUISliderMaxValueIcall(uint32_t entityHandle)
        {
            const auto* slider = TryGetManagedUISliderComponent(entityHandle);
            return slider ? slider->MaxValue : 1.0f;
        }

        void ManagedSetUISliderMaxValueIcall(uint32_t entityHandle, float value)
        {
            if (auto* slider = TryGetManagedUISliderComponent(entityHandle))
                slider->MaxValue = value;
        }

        float ManagedGetUISliderValueIcall(uint32_t entityHandle)
        {
            const auto* slider = TryGetManagedUISliderComponent(entityHandle);
            return slider ? slider->Value : 0.0f;
        }

        void ManagedSetUISliderValueIcall(uint32_t entityHandle, float value)
        {
            if (auto* slider = TryGetManagedUISliderComponent(entityHandle))
                slider->Value = value;
        }

        ManagedVector4 ManagedGetUISliderBackgroundColorIcall(uint32_t entityHandle)
        {
            const auto* slider = TryGetManagedUISliderComponent(entityHandle);
            return slider ? ToManagedVector4(slider->BackgroundColor) : ManagedVector4{ 0.22f, 0.22f, 0.22f, 1.0f };
        }

        void ManagedSetUISliderBackgroundColorIcall(uint32_t entityHandle, ManagedVector4 value)
        {
            if (auto* slider = TryGetManagedUISliderComponent(entityHandle))
                slider->BackgroundColor = ToGlmVector4(value);
        }

        ManagedVector4 ManagedGetUISliderFillColorIcall(uint32_t entityHandle)
        {
            const auto* slider = TryGetManagedUISliderComponent(entityHandle);
            return slider ? ToManagedVector4(slider->FillColor) : ManagedVector4{ 0.22f, 0.72f, 1.0f, 0.95f };
        }

        void ManagedSetUISliderFillColorIcall(uint32_t entityHandle, ManagedVector4 value)
        {
            if (auto* slider = TryGetManagedUISliderComponent(entityHandle))
                slider->FillColor = ToGlmVector4(value);
        }

        ManagedVector4 ManagedGetUISliderHandleColorIcall(uint32_t entityHandle)
        {
            const auto* slider = TryGetManagedUISliderComponent(entityHandle);
            return slider ? ToManagedVector4(slider->HandleColor) : ManagedVector4{ 0.92f, 0.92f, 0.92f, 1.0f };
        }

        void ManagedSetUISliderHandleColorIcall(uint32_t entityHandle, ManagedVector4 value)
        {
            if (auto* slider = TryGetManagedUISliderComponent(entityHandle))
                slider->HandleColor = ToGlmVector4(value);
        }

        float ManagedGetUISliderHandleWidthIcall(uint32_t entityHandle)
        {
            const auto* slider = TryGetManagedUISliderComponent(entityHandle);
            return slider ? slider->HandleWidth : 16.0f;
        }

        void ManagedSetUISliderHandleWidthIcall(uint32_t entityHandle, float value)
        {
            if (auto* slider = TryGetManagedUISliderComponent(entityHandle))
                slider->HandleWidth = value;
        }

        float ManagedGetUISliderHandleHeightMultiplierIcall(uint32_t entityHandle)
        {
            const auto* slider = TryGetManagedUISliderComponent(entityHandle);
            return slider ? slider->HandleHeightMultiplier : 1.25f;
        }

        void ManagedSetUISliderHandleHeightMultiplierIcall(uint32_t entityHandle, float value)
        {
            if (auto* slider = TryGetManagedUISliderComponent(entityHandle))
                slider->HandleHeightMultiplier = value;
        }

        bool ManagedGetUISliderShowHandleIcall(uint32_t entityHandle)
        {
            const auto* slider = TryGetManagedUISliderComponent(entityHandle);
            return slider ? slider->ShowHandle : true;
        }

        void ManagedSetUISliderShowHandleIcall(uint32_t entityHandle, bool value)
        {
            if (auto* slider = TryGetManagedUISliderComponent(entityHandle))
                slider->ShowHandle = value;
        }

        bool ManagedGetUISliderRuntimeDraggingIcall(uint32_t entityHandle)
        {
            const auto* slider = TryGetManagedUISliderComponent(entityHandle);
            return slider ? slider->RuntimeDragging : false;
        }

        Coral::String ManagedGetUISliderOnValueChangedEventIcall(uint32_t entityHandle)
        {
            const auto* slider = TryGetManagedUISliderComponent(entityHandle);
            return slider ? Coral::String::New(slider->OnValueChangedEvent) : Coral::String::New("");
        }

        void ManagedSetUISliderOnValueChangedEventIcall(uint32_t entityHandle, Coral::String value)
        {
            if (auto* slider = TryGetManagedUISliderComponent(entityHandle))
                slider->OnValueChangedEvent = ToUtf8Borrowed(value);
        }

        bool ManagedHasAudioListener2DComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedAudioListener2DComponent(entityHandle) != nullptr;
        }

        bool ManagedGetAudioListener2DEnabledIcall(uint32_t entityHandle)
        {
            const auto* listener = TryGetManagedAudioListener2DComponent(entityHandle);
            return listener ? listener->Enabled : true;
        }

        void ManagedSetAudioListener2DEnabledIcall(uint32_t entityHandle, bool value)
        {
            if (auto* listener = TryGetManagedAudioListener2DComponent(entityHandle))
                listener->Enabled = value;
        }

        bool ManagedGetAudioListener2DUsePrimaryCameraPositionIcall(uint32_t entityHandle)
        {
            const auto* listener = TryGetManagedAudioListener2DComponent(entityHandle);
            return listener ? listener->UsePrimaryCameraPosition : true;
        }

        void ManagedSetAudioListener2DUsePrimaryCameraPositionIcall(uint32_t entityHandle, bool value)
        {
            if (auto* listener = TryGetManagedAudioListener2DComponent(entityHandle))
                listener->UsePrimaryCameraPosition = value;
        }

        bool ManagedHasAudioListener3DComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedAudioListener3DComponent(entityHandle) != nullptr;
        }

        bool ManagedGetAudioListener3DEnabledIcall(uint32_t entityHandle)
        {
            const auto* listener = TryGetManagedAudioListener3DComponent(entityHandle);
            return listener ? listener->Enabled : true;
        }

        void ManagedSetAudioListener3DEnabledIcall(uint32_t entityHandle, bool value)
        {
            if (auto* listener = TryGetManagedAudioListener3DComponent(entityHandle))
                listener->Enabled = value;
        }

        bool ManagedGetAudioListener3DUsePrimaryCameraTransformIcall(uint32_t entityHandle)
        {
            const auto* listener = TryGetManagedAudioListener3DComponent(entityHandle);
            return listener ? listener->UsePrimaryCameraTransform : true;
        }

        void ManagedSetAudioListener3DUsePrimaryCameraTransformIcall(uint32_t entityHandle, bool value)
        {
            if (auto* listener = TryGetManagedAudioListener3DComponent(entityHandle))
                listener->UsePrimaryCameraTransform = value;
        }

        bool ManagedHasAudioSourceComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedAudioSourceComponent(entityHandle) != nullptr;
        }

        Coral::String ManagedGetAudioSourceClipKeyIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource ? Coral::String::New(audioSource->AudioClipKey) : Coral::String::New("");
        }

        void ManagedSetAudioSourceClipKeyIcall(uint32_t entityHandle, Coral::String value)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
                audioSource->AudioClipKey = ToUtf8Borrowed(value);
        }

        float ManagedGetAudioSourceVolumeIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource ? audioSource->Volume : 1.0f;
        }

        void ManagedSetAudioSourceVolumeIcall(uint32_t entityHandle, float value)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
                audioSource->Volume = std::max(0.0f, value);
        }

        float ManagedGetAudioSourcePitchIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource ? audioSource->Pitch : 1.0f;
        }

        void ManagedSetAudioSourcePitchIcall(uint32_t entityHandle, float value)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
                audioSource->Pitch = std::max(0.01f, value);
        }

        bool ManagedGetAudioSourcePlayOnStartIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource ? audioSource->PlayOnStart : true;
        }

        void ManagedSetAudioSourcePlayOnStartIcall(uint32_t entityHandle, bool value)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
                audioSource->PlayOnStart = value;
        }

        bool ManagedGetAudioSourceLoopIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource ? audioSource->Loop : false;
        }

        void ManagedSetAudioSourceLoopIcall(uint32_t entityHandle, bool value)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
                audioSource->Loop = value;
        }

        bool ManagedGetAudioSourceMutedIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource ? audioSource->Muted : false;
        }

        void ManagedSetAudioSourceMutedIcall(uint32_t entityHandle, bool value)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
                audioSource->Muted = value;
        }

        int ManagedGetAudioSourcePlaybackSpaceIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource ? static_cast<int>(audioSource->Space) : static_cast<int>(AudioSourceComponent::PlaybackSpace::Global);
        }

        void ManagedSetAudioSourcePlaybackSpaceIcall(uint32_t entityHandle, int value)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
                audioSource->Space = static_cast<AudioSourceComponent::PlaybackSpace>(value);
        }

        Coral::String ManagedGetAudioSourceMixerGroupIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource ? Coral::String::New(audioSource->MixerGroup) : Coral::String::New("SFX");
        }

        void ManagedSetAudioSourceMixerGroupIcall(uint32_t entityHandle, Coral::String value)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
            {
                audioSource->MixerGroup = ToUtf8Borrowed(value);
                if (audioSource->MixerGroup.empty())
                    audioSource->MixerGroup = "SFX";
            }
        }

        float ManagedGetAudioSourceSpatialMinDistanceIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource ? audioSource->SpatialMinDistance : 1.0f;
        }

        void ManagedSetAudioSourceSpatialMinDistanceIcall(uint32_t entityHandle, float value)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
            {
                audioSource->SpatialMinDistance = std::max(0.001f, value);
                audioSource->SpatialMaxDistance = std::max(audioSource->SpatialMinDistance, audioSource->SpatialMaxDistance);
            }
        }

        float ManagedGetAudioSourceSpatialMaxDistanceIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource ? audioSource->SpatialMaxDistance : 20.0f;
        }

        void ManagedSetAudioSourceSpatialMaxDistanceIcall(uint32_t entityHandle, float value)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
                audioSource->SpatialMaxDistance = std::max(audioSource->SpatialMinDistance, value);
        }

        float ManagedGetAudioSourceSpatialRolloffExponentIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource ? audioSource->SpatialRolloffExponent : 1.0f;
        }

        void ManagedSetAudioSourceSpatialRolloffExponentIcall(uint32_t entityHandle, float value)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
                audioSource->SpatialRolloffExponent = std::max(0.01f, value);
        }

        float ManagedGetAudioSourceStereoPanStrengthIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource ? audioSource->StereoPanStrength : 1.0f;
        }

        void ManagedSetAudioSourceStereoPanStrengthIcall(uint32_t entityHandle, float value)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
                audioSource->StereoPanStrength = std::clamp(value, 0.0f, 1.0f);
        }

        int ManagedGetAudioSourceSpatialRolloffModeIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource ? static_cast<int>(audioSource->SpatialRolloffMode) : static_cast<int>(AudioSourceComponent::RolloffMode::Linear);
        }

        void ManagedSetAudioSourceSpatialRolloffModeIcall(uint32_t entityHandle, int value)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
                audioSource->SpatialRolloffMode = static_cast<AudioSourceComponent::RolloffMode>(value);
        }

        float ManagedGetAudioSourceDopplerFactorIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource ? audioSource->DopplerFactor : 1.0f;
        }

        void ManagedSetAudioSourceDopplerFactorIcall(uint32_t entityHandle, float value)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
                audioSource->DopplerFactor = std::max(0.0f, value);
        }

        bool ManagedGetAudioSourceEnableDirectionalAttenuationIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource ? audioSource->EnableDirectionalAttenuation : false;
        }

        void ManagedSetAudioSourceEnableDirectionalAttenuationIcall(uint32_t entityHandle, bool value)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
                audioSource->EnableDirectionalAttenuation = value;
        }

        float ManagedGetAudioSourceDirectionalInnerAngleDegreesIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource ? audioSource->DirectionalInnerAngleDegrees : 360.0f;
        }

        void ManagedSetAudioSourceDirectionalInnerAngleDegreesIcall(uint32_t entityHandle, float value)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
                audioSource->DirectionalInnerAngleDegrees = std::max(0.0f, value);
        }

        float ManagedGetAudioSourceDirectionalOuterAngleDegreesIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource ? audioSource->DirectionalOuterAngleDegrees : 360.0f;
        }

        void ManagedSetAudioSourceDirectionalOuterAngleDegreesIcall(uint32_t entityHandle, float value)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
                audioSource->DirectionalOuterAngleDegrees = std::max(0.0f, value);
        }

        float ManagedGetAudioSourceDirectionalOuterVolumeIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource ? audioSource->DirectionalOuterVolume : 1.0f;
        }

        void ManagedSetAudioSourceDirectionalOuterVolumeIcall(uint32_t entityHandle, float value)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
                audioSource->DirectionalOuterVolume = std::clamp(value, 0.0f, 1.0f);
        }

        Coral::String ManagedGetAudioSourceAttenuationCurveKeyIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource ? Coral::String::New(audioSource->AttenuationCurveKey) : Coral::String::New("");
        }

        void ManagedSetAudioSourceAttenuationCurveKeyIcall(uint32_t entityHandle, Coral::String value)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
                audioSource->AttenuationCurveKey = ToUtf8Borrowed(value);
        }

        bool ManagedGetAudioSourceIsPlayingIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource != nullptr &&
                audioSource->RuntimeVoiceId != 0 &&
                Audio::AudioEngine::GetInstance().IsVoiceActive(audioSource->RuntimeVoiceId);
        }

        bool ManagedRequestAudioSourcePlayIcall(uint32_t entityHandle)
        {
            auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            if (audioSource == nullptr || audioSource->AudioClipKey.empty())
                return false;

            if (audioSource->RuntimeVoiceId != 0)
                Audio::AudioEngine::GetInstance().Stop(audioSource->RuntimeVoiceId);

            audioSource->RuntimeVoiceId = 0;
            audioSource->RuntimePlaybackStarted = false;
            audioSource->RuntimePlayRequested = true;
            return true;
        }

        void ManagedStopAudioSourceIcall(uint32_t entityHandle)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
            {
                if (audioSource->RuntimeVoiceId != 0)
                    Audio::AudioEngine::GetInstance().Stop(audioSource->RuntimeVoiceId);
                audioSource->RuntimeVoiceId = 0;
                audioSource->RuntimePlaybackStarted = false;
                audioSource->RuntimePlayRequested = false;
                audioSource->RuntimePlayOnStartConsumed = true;
            }
        }

        bool ManagedHasAnimatorComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedAnimatorComponent(entityHandle) != nullptr;
        }

        Coral::String ManagedGetAnimatorControllerKeyIcall(uint32_t entityHandle)
        {
            const auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            return animator ? Coral::String::New(animator->ControllerKey) : Coral::String::New("");
        }

        void ManagedSetAnimatorControllerKeyIcall(uint32_t entityHandle, Coral::String value)
        {
            if (auto* animator = TryGetManagedAnimatorComponent(entityHandle))
            {
                animator->ControllerKey = ToUtf8Borrowed(value);
                animator->CachedController.reset();
                animator->ControllerLoadAttempted = false;
                animator->RuntimeInitialized = false;
            }
        }

        Coral::String ManagedGetAnimatorDefaultClipKeyIcall(uint32_t entityHandle)
        {
            const auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            return animator ? Coral::String::New(animator->DefaultClipKey) : Coral::String::New("");
        }

        void ManagedSetAnimatorDefaultClipKeyIcall(uint32_t entityHandle, Coral::String value)
        {
            if (auto* animator = TryGetManagedAnimatorComponent(entityHandle))
            {
                animator->DefaultClipKey = ToUtf8Borrowed(value);
                animator->CachedDefaultClip.reset();
                animator->DefaultClipLoadAttempted = false;
            }
        }

        float ManagedGetAnimatorPlaybackSpeedIcall(uint32_t entityHandle)
        {
            const auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            return animator ? animator->PlaybackSpeed : 1.0f;
        }

        void ManagedSetAnimatorPlaybackSpeedIcall(uint32_t entityHandle, float value)
        {
            if (auto* animator = TryGetManagedAnimatorComponent(entityHandle))
                animator->PlaybackSpeed = value;
        }

        bool ManagedGetAnimatorEnabledIcall(uint32_t entityHandle)
        {
            const auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            return animator ? animator->Enabled : true;
        }

        void ManagedSetAnimatorEnabledIcall(uint32_t entityHandle, bool value)
        {
            if (auto* animator = TryGetManagedAnimatorComponent(entityHandle))
                animator->Enabled = value;
        }

        bool ManagedGetAnimatorApplyToSpriteIcall(uint32_t entityHandle)
        {
            const auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            return animator ? animator->ApplyToSprite : true;
        }

        void ManagedSetAnimatorApplyToSpriteIcall(uint32_t entityHandle, bool value)
        {
            if (auto* animator = TryGetManagedAnimatorComponent(entityHandle))
                animator->ApplyToSprite = value;
        }

        bool ManagedGetAnimatorApplyToTransformIcall(uint32_t entityHandle)
        {
            const auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            return animator ? animator->ApplyToTransform : true;
        }

        void ManagedSetAnimatorApplyToTransformIcall(uint32_t entityHandle, bool value)
        {
            if (auto* animator = TryGetManagedAnimatorComponent(entityHandle))
                animator->ApplyToTransform = value;
        }

        bool ManagedGetAnimatorAutoPlayIcall(uint32_t entityHandle)
        {
            const auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            return animator ? animator->AutoPlay : true;
        }

        void ManagedSetAnimatorAutoPlayIcall(uint32_t entityHandle, bool value)
        {
            if (auto* animator = TryGetManagedAnimatorComponent(entityHandle))
                animator->AutoPlay = value;
        }

        bool ManagedPlayAnimatorStateIcall(uint32_t entityHandle, Coral::String stateName, bool restartIfSameState)
        {
            auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            if (animator == nullptr)
                return false;

            const std::string requestedState = ToUtf8Borrowed(stateName);
            if (requestedState.empty())
                return false;
            if (!restartIfSameState && animator->RuntimeCurrentStateName == requestedState)
                return true;

            animator->RuntimeCurrentStateName = requestedState;
            animator->RuntimeCurrentClipKey.clear();
            animator->RuntimeStateTimeSeconds = 0.0f;
            animator->RuntimePreviousStateTimeSeconds = 0.0f;
            animator->RuntimeCurrentStateDurationSeconds = 1.0f;
            animator->RuntimeInitialized = true;
            return true;
        }

        bool ManagedPlayAnimatorClipIcall(uint32_t entityHandle, Coral::String clipKey, bool restartIfSameClip)
        {
            auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            if (animator == nullptr)
                return false;

            const std::string requestedClip = ToUtf8Borrowed(clipKey);
            if (requestedClip.empty())
                return false;
            if (!restartIfSameClip && animator->RuntimeCurrentClipKey == requestedClip)
                return true;

            animator->RuntimeCurrentStateName.clear();
            animator->RuntimeCurrentClipKey = requestedClip;
            animator->RuntimeStateTimeSeconds = 0.0f;
            animator->RuntimePreviousStateTimeSeconds = 0.0f;
            animator->RuntimeCurrentStateDurationSeconds = 1.0f;
            animator->RuntimeInitialized = true;
            return true;
        }

        bool ManagedSetAnimatorBoolParameterIcall(uint32_t entityHandle, Coral::String parameterName, bool value)
        {
            auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            if (animator == nullptr)
                return false;

            const std::string parameter = ToUtf8Borrowed(parameterName);
            if (parameter.empty())
                return false;
            animator->SetBoolParameter(parameter, value);
            return true;
        }

        bool ManagedGetAnimatorBoolParameterIcall(uint32_t entityHandle, Coral::String parameterName, bool fallback)
        {
            const auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            if (animator == nullptr)
                return fallback;

            const std::string parameter = ToUtf8Borrowed(parameterName);
            if (parameter.empty())
                return fallback;
            return animator->GetBoolParameter(parameter, fallback);
        }

        bool ManagedSetAnimatorFloatParameterIcall(uint32_t entityHandle, Coral::String parameterName, float value)
        {
            auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            if (animator == nullptr)
                return false;

            const std::string parameter = ToUtf8Borrowed(parameterName);
            if (parameter.empty())
                return false;
            animator->SetFloatParameter(parameter, value);
            return true;
        }

        float ManagedGetAnimatorFloatParameterIcall(uint32_t entityHandle, Coral::String parameterName, float fallback)
        {
            const auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            if (animator == nullptr)
                return fallback;

            const std::string parameter = ToUtf8Borrowed(parameterName);
            if (parameter.empty())
                return fallback;
            return animator->GetFloatParameter(parameter, fallback);
        }

        bool ManagedSetAnimatorIntegerParameterIcall(uint32_t entityHandle, Coral::String parameterName, int value)
        {
            auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            if (animator == nullptr)
                return false;

            const std::string parameter = ToUtf8Borrowed(parameterName);
            if (parameter.empty())
                return false;
            animator->SetIntegerParameter(parameter, value);
            return true;
        }

        int ManagedGetAnimatorIntegerParameterIcall(uint32_t entityHandle, Coral::String parameterName, int fallback)
        {
            const auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            if (animator == nullptr)
                return fallback;

            const std::string parameter = ToUtf8Borrowed(parameterName);
            if (parameter.empty())
                return fallback;
            return animator->GetIntegerParameter(parameter, fallback);
        }

        bool ManagedSetAnimatorTriggerParameterIcall(uint32_t entityHandle, Coral::String parameterName)
        {
            auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            if (animator == nullptr)
                return false;

            const std::string parameter = ToUtf8Borrowed(parameterName);
            if (parameter.empty())
                return false;
            animator->SetTrigger(parameter);
            return true;
        }

        bool ManagedResetAnimatorTriggerParameterIcall(uint32_t entityHandle, Coral::String parameterName)
        {
            auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            if (animator == nullptr)
                return false;

            const std::string parameter = ToUtf8Borrowed(parameterName);
            if (parameter.empty())
                return false;
            animator->ResetTrigger(parameter);
            return true;
        }

        Coral::String ManagedGetAnimatorCurrentStateNameIcall(uint32_t entityHandle)
        {
            const auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            return animator ? Coral::String::New(animator->RuntimeCurrentStateName) : Coral::String::New("");
        }

        Coral::String ManagedGetAnimatorCurrentClipKeyIcall(uint32_t entityHandle)
        {
            const auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            return animator ? Coral::String::New(animator->RuntimeCurrentClipKey) : Coral::String::New("");
        }

        float ManagedGetAnimatorStateTimeSecondsIcall(uint32_t entityHandle)
        {
            const auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            return animator ? animator->RuntimeStateTimeSeconds : 0.0f;
        }

        float ManagedGetAnimatorCurrentStateDurationSecondsIcall(uint32_t entityHandle)
        {
            const auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            return animator ? animator->RuntimeCurrentStateDurationSeconds : 0.0f;
        }

        bool ManagedHasAnimationEventReceiverComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedAnimationEventReceiverComponent(entityHandle) != nullptr;
        }

        bool ManagedGetAnimationEventReceiverEnabledIcall(uint32_t entityHandle)
        {
            const auto* receiver = TryGetManagedAnimationEventReceiverComponent(entityHandle);
            return receiver ? receiver->Enabled : true;
        }

        void ManagedSetAnimationEventReceiverEnabledIcall(uint32_t entityHandle, bool value)
        {
            if (auto* receiver = TryGetManagedAnimationEventReceiverComponent(entityHandle))
                receiver->Enabled = value;
        }

        int ManagedGetAnimationEventReceiverDispatchedEventCountIcall(uint32_t entityHandle)
        {
            const auto* receiver = TryGetManagedAnimationEventReceiverComponent(entityHandle);
            return receiver ? static_cast<int>(receiver->RuntimeDispatchedEvents.size()) : 0;
        }

        Coral::String ManagedGetAnimationEventReceiverEventNameIcall(uint32_t entityHandle, int index)
        {
            const auto* receiver = TryGetManagedAnimationEventReceiverComponent(entityHandle);
            if (receiver == nullptr || index < 0 || index >= static_cast<int>(receiver->RuntimeDispatchedEvents.size()))
                return Coral::String::New("");
            return Coral::String::New(receiver->RuntimeDispatchedEvents[static_cast<size_t>(index)].Name);
        }

        Coral::String ManagedGetAnimationEventReceiverEventStringPayloadIcall(uint32_t entityHandle, int index)
        {
            const auto* receiver = TryGetManagedAnimationEventReceiverComponent(entityHandle);
            if (receiver == nullptr || index < 0 || index >= static_cast<int>(receiver->RuntimeDispatchedEvents.size()))
                return Coral::String::New("");
            return Coral::String::New(receiver->RuntimeDispatchedEvents[static_cast<size_t>(index)].StringPayload);
        }

        float ManagedGetAnimationEventReceiverEventFloatPayloadIcall(uint32_t entityHandle, int index)
        {
            const auto* receiver = TryGetManagedAnimationEventReceiverComponent(entityHandle);
            if (receiver == nullptr || index < 0 || index >= static_cast<int>(receiver->RuntimeDispatchedEvents.size()))
                return 0.0f;
            return receiver->RuntimeDispatchedEvents[static_cast<size_t>(index)].FloatPayload;
        }

        int ManagedGetAnimationEventReceiverEventIntegerPayloadIcall(uint32_t entityHandle, int index)
        {
            const auto* receiver = TryGetManagedAnimationEventReceiverComponent(entityHandle);
            if (receiver == nullptr || index < 0 || index >= static_cast<int>(receiver->RuntimeDispatchedEvents.size()))
                return 0;
            return receiver->RuntimeDispatchedEvents[static_cast<size_t>(index)].IntegerPayload;
        }

        bool ManagedGetAnimationEventReceiverEventBooleanPayloadIcall(uint32_t entityHandle, int index)
        {
            const auto* receiver = TryGetManagedAnimationEventReceiverComponent(entityHandle);
            if (receiver == nullptr || index < 0 || index >= static_cast<int>(receiver->RuntimeDispatchedEvents.size()))
                return false;
            return receiver->RuntimeDispatchedEvents[static_cast<size_t>(index)].BooleanPayload;
        }

        float ManagedGetAnimationEventReceiverEventTimeSecondsIcall(uint32_t entityHandle, int index)
        {
            const auto* receiver = TryGetManagedAnimationEventReceiverComponent(entityHandle);
            if (receiver == nullptr || index < 0 || index >= static_cast<int>(receiver->RuntimeDispatchedEvents.size()))
                return 0.0f;
            return receiver->RuntimeDispatchedEvents[static_cast<size_t>(index)].TimeSeconds;
        }

        float ManagedGetAnimationEventReceiverEventNormalizedTimeIcall(uint32_t entityHandle, int index)
        {
            const auto* receiver = TryGetManagedAnimationEventReceiverComponent(entityHandle);
            if (receiver == nullptr || index < 0 || index >= static_cast<int>(receiver->RuntimeDispatchedEvents.size()))
                return 0.0f;
            return receiver->RuntimeDispatchedEvents[static_cast<size_t>(index)].NormalizedTime;
        }

        bool ManagedHasParticleEmitterComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedParticleEmitterComponent(entityHandle) != nullptr;
        }

        float ManagedGetParticleEmitterSpawnRateIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->SpawnRate : 10.0f;
        }

        void ManagedSetParticleEmitterSpawnRateIcall(uint32_t entityHandle, float value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->SpawnRate = std::max(0.0f, value);
        }

        float ManagedGetParticleEmitterLifetimeMinIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->LifetimeMin : 1.0f;
        }

        void ManagedSetParticleEmitterLifetimeMinIcall(uint32_t entityHandle, float value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
            {
                emitter->LifetimeMin = std::max(0.0f, value);
                emitter->LifetimeMax = std::max(emitter->LifetimeMin, emitter->LifetimeMax);
            }
        }

        float ManagedGetParticleEmitterLifetimeMaxIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->LifetimeMax : 2.0f;
        }

        void ManagedSetParticleEmitterLifetimeMaxIcall(uint32_t entityHandle, float value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->LifetimeMax = std::max(emitter->LifetimeMin, value);
        }

        bool ManagedGetParticleEmitterLoopingIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->Looping : true;
        }

        void ManagedSetParticleEmitterLoopingIcall(uint32_t entityHandle, bool value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->Looping = value;
        }

        float ManagedGetParticleEmitterDurationIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->Duration : 5.0f;
        }

        void ManagedSetParticleEmitterDurationIcall(uint32_t entityHandle, float value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->Duration = std::max(0.0f, value);
        }

        bool ManagedGetParticleEmitterPlayOnStartIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->PlayOnStart : true;
        }

        void ManagedSetParticleEmitterPlayOnStartIcall(uint32_t entityHandle, bool value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->PlayOnStart = value;
        }

        bool ManagedGetParticleEmitterBurstEnabledIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->BurstEnabled : false;
        }

        void ManagedSetParticleEmitterBurstEnabledIcall(uint32_t entityHandle, bool value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->BurstEnabled = value;
        }

        int ManagedGetParticleEmitterBurstCountIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? static_cast<int>(emitter->BurstCount) : 10;
        }

        void ManagedSetParticleEmitterBurstCountIcall(uint32_t entityHandle, int value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->BurstCount = static_cast<uint32_t>(std::clamp(value, 0, static_cast<int>(ParticleEmitterComponent::kMaxParticlesCap)));
        }

        ManagedVector2 ManagedGetParticleEmitterSpawnOffsetMinIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? ToManagedVector2(emitter->SpawnOffsetMin) : ManagedVector2{};
        }

        void ManagedSetParticleEmitterSpawnOffsetMinIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->SpawnOffsetMin = ToGlmVector2(value);
        }

        ManagedVector2 ManagedGetParticleEmitterSpawnOffsetMaxIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? ToManagedVector2(emitter->SpawnOffsetMax) : ManagedVector2{};
        }

        void ManagedSetParticleEmitterSpawnOffsetMaxIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->SpawnOffsetMax = ToGlmVector2(value);
        }

        bool ManagedGetParticleEmitterUseRadialSpawnIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->UseRadialSpawn : false;
        }

        void ManagedSetParticleEmitterUseRadialSpawnIcall(uint32_t entityHandle, bool value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->UseRadialSpawn = value;
        }

        float ManagedGetParticleEmitterSpawnRadiusMinIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->SpawnRadiusMin : 0.0f;
        }

        void ManagedSetParticleEmitterSpawnRadiusMinIcall(uint32_t entityHandle, float value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
            {
                emitter->SpawnRadiusMin = std::max(0.0f, value);
                emitter->SpawnRadiusMax = std::max(emitter->SpawnRadiusMin, emitter->SpawnRadiusMax);
            }
        }

        float ManagedGetParticleEmitterSpawnRadiusMaxIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->SpawnRadiusMax : 0.0f;
        }

        void ManagedSetParticleEmitterSpawnRadiusMaxIcall(uint32_t entityHandle, float value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->SpawnRadiusMax = std::max(emitter->SpawnRadiusMin, value);
        }

        float ManagedGetParticleEmitterSpeedMinIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->SpeedMin : 50.0f;
        }

        void ManagedSetParticleEmitterSpeedMinIcall(uint32_t entityHandle, float value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
            {
                emitter->SpeedMin = std::max(0.0f, value);
                emitter->SpeedMax = std::max(emitter->SpeedMin, emitter->SpeedMax);
            }
        }

        float ManagedGetParticleEmitterSpeedMaxIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->SpeedMax : 100.0f;
        }

        void ManagedSetParticleEmitterSpeedMaxIcall(uint32_t entityHandle, float value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->SpeedMax = std::max(emitter->SpeedMin, value);
        }

        float ManagedGetParticleEmitterAngleMinIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->AngleMin : 0.0f;
        }

        void ManagedSetParticleEmitterAngleMinIcall(uint32_t entityHandle, float value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->AngleMin = value;
        }

        float ManagedGetParticleEmitterAngleMaxIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->AngleMax : 360.0f;
        }

        void ManagedSetParticleEmitterAngleMaxIcall(uint32_t entityHandle, float value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->AngleMax = value;
        }

        bool ManagedGetParticleEmitterRadialVelocityIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->RadialVelocity : false;
        }

        void ManagedSetParticleEmitterRadialVelocityIcall(uint32_t entityHandle, bool value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->RadialVelocity = value;
        }

        float ManagedGetParticleEmitterGravityModifierIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->GravityModifier : 0.0f;
        }

        void ManagedSetParticleEmitterGravityModifierIcall(uint32_t entityHandle, float value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->GravityModifier = value;
        }

        float ManagedGetParticleEmitterStartSizeMinIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->StartSizeMin : 1.0f;
        }

        void ManagedSetParticleEmitterStartSizeMinIcall(uint32_t entityHandle, float value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
            {
                emitter->StartSizeMin = std::max(0.0f, value);
                emitter->StartSizeMax = std::max(emitter->StartSizeMin, emitter->StartSizeMax);
            }
        }

        float ManagedGetParticleEmitterStartSizeMaxIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->StartSizeMax : 1.0f;
        }

        void ManagedSetParticleEmitterStartSizeMaxIcall(uint32_t entityHandle, float value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->StartSizeMax = std::max(emitter->StartSizeMin, value);
        }

        float ManagedGetParticleEmitterEndSizeIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->EndSize : 0.0f;
        }

        void ManagedSetParticleEmitterEndSizeIcall(uint32_t entityHandle, float value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->EndSize = std::max(0.0f, value);
        }

        ManagedVector4 ManagedGetParticleEmitterStartColorIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? ToManagedVector4(emitter->StartColor) : ManagedVector4{ 1.0f, 1.0f, 1.0f, 1.0f };
        }

        void ManagedSetParticleEmitterStartColorIcall(uint32_t entityHandle, ManagedVector4 value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->StartColor = ToGlmVector4(value);
        }

        ManagedVector4 ManagedGetParticleEmitterEndColorIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? ToManagedVector4(emitter->EndColor) : ManagedVector4{ 1.0f, 1.0f, 1.0f, 0.0f };
        }

        void ManagedSetParticleEmitterEndColorIcall(uint32_t entityHandle, ManagedVector4 value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->EndColor = ToGlmVector4(value);
        }

        float ManagedGetParticleEmitterStartRotationMinIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->StartRotationMin : 0.0f;
        }

        void ManagedSetParticleEmitterStartRotationMinIcall(uint32_t entityHandle, float value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->StartRotationMin = value;
        }

        float ManagedGetParticleEmitterStartRotationMaxIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->StartRotationMax : 0.0f;
        }

        void ManagedSetParticleEmitterStartRotationMaxIcall(uint32_t entityHandle, float value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->StartRotationMax = value;
        }

        float ManagedGetParticleEmitterRotationSpeedMinIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->RotationSpeedMin : 0.0f;
        }

        void ManagedSetParticleEmitterRotationSpeedMinIcall(uint32_t entityHandle, float value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->RotationSpeedMin = value;
        }

        float ManagedGetParticleEmitterRotationSpeedMaxIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->RotationSpeedMax : 0.0f;
        }

        void ManagedSetParticleEmitterRotationSpeedMaxIcall(uint32_t entityHandle, float value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->RotationSpeedMax = value;
        }

        Coral::String ManagedGetParticleEmitterTextureKeyIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? Coral::String::New(emitter->TextureKey) : Coral::String::New("");
        }

        void ManagedSetParticleEmitterTextureKeyIcall(uint32_t entityHandle, Coral::String value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
            {
                emitter->TextureKey = ToUtf8Borrowed(value);
                emitter->CachedTexture.reset();
                emitter->TextureLoadAttempted = false;
            }
        }

        int ManagedGetParticleEmitterMaxParticlesIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? static_cast<int>(emitter->MaxParticles) : 1024;
        }

        void ManagedSetParticleEmitterMaxParticlesIcall(uint32_t entityHandle, int value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
            {
                emitter->MaxParticles = static_cast<uint32_t>(std::clamp(value, 0, static_cast<int>(ParticleEmitterComponent::kMaxParticlesCap)));
                if (emitter->RuntimeState)
                    emitter->RuntimeState->Allocate(emitter->MaxParticles);
            }
        }

        bool ManagedGetParticleEmitterIsPlayingIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->Playing : false;
        }

        bool ManagedGetParticleEmitterIsPausedIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->Paused : false;
        }

        int ManagedGetParticleEmitterAliveParticleCountIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return (emitter && emitter->RuntimeState) ? static_cast<int>(emitter->RuntimeState->AliveCount) : 0;
        }

        void ManagedPlayParticleEmitterIcall(uint32_t entityHandle)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                ParticleEmitterPlay(*emitter);
        }

        void ManagedStopParticleEmitterIcall(uint32_t entityHandle, bool clearParticles)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                ParticleEmitterStop(*emitter, clearParticles);
        }

        void ManagedPauseParticleEmitterIcall(uint32_t entityHandle)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                ParticleEmitterPause(*emitter);
        }

        void ManagedResumeParticleEmitterIcall(uint32_t entityHandle)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                ParticleEmitterResume(*emitter);
        }

        void ManagedEmitParticleEmitterIcall(uint32_t entityHandle, int count)
        {
            auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            if (emitter == nullptr || count <= 0)
                return;

            glm::vec2 worldPosition(0.0f);
            if (s_HostState.ActiveScene != nullptr)
            {
                const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
                if (entity != entt::null)
                {
                    const glm::mat4 worldTransform = s_HostState.ActiveScene->GetWorldTransformMatrix(entity);
                    worldPosition = glm::vec2(worldTransform[3][0], worldTransform[3][1]);
                }
            }

            ParticleEmitterEmit(*emitter, static_cast<uint32_t>(count), worldPosition);
        }

        bool ManagedHasGrid2DComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedGrid2DComponent(entityHandle) != nullptr;
        }

        ManagedVector2 ManagedGetGrid2DCellSizeIcall(uint32_t entityHandle)
        {
            const auto* grid2D = TryGetManagedGrid2DComponent(entityHandle);
            return grid2D ? ToManagedVector2(grid2D->CellSize) : ManagedVector2{ 1.0f, 1.0f };
        }

        void ManagedSetGrid2DCellSizeIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* grid2D = TryGetManagedGrid2DComponent(entityHandle))
            {
                const glm::vec2 cellSize = ToGlmVector2(value);
                grid2D->CellSize = glm::vec2(std::max(0.001f, cellSize.x), std::max(0.001f, cellSize.y));
            }
        }

        ManagedVector2 ManagedGetGrid2DCellGapIcall(uint32_t entityHandle)
        {
            const auto* grid2D = TryGetManagedGrid2DComponent(entityHandle);
            return grid2D ? ToManagedVector2(grid2D->CellGap) : ManagedVector2{};
        }

        void ManagedSetGrid2DCellGapIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* grid2D = TryGetManagedGrid2DComponent(entityHandle))
                grid2D->CellGap = ToGlmVector2(value);
        }

        bool ManagedHasTilemapLayerComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedTilemapLayerComponent(entityHandle) != nullptr;
        }

        int ManagedGetTilemapLayerGridWidthIcall(uint32_t entityHandle)
        {
            const auto* layer = TryGetManagedTilemapLayerComponent(entityHandle);
            return layer ? std::max(1, layer->GridSize.x) : 64;
        }

        int ManagedGetTilemapLayerGridHeightIcall(uint32_t entityHandle)
        {
            const auto* layer = TryGetManagedTilemapLayerComponent(entityHandle);
            return layer ? std::max(1, layer->GridSize.y) : 64;
        }

        void ManagedResizeTilemapLayerGridIcall(uint32_t entityHandle, int width, int height)
        {
            if (auto* layer = TryGetManagedTilemapLayerComponent(entityHandle))
                layer->ResizeGrid(glm::ivec2(std::max(1, width), std::max(1, height)));
        }

        int ManagedGetTilemapLayerRenderOrderIcall(uint32_t entityHandle)
        {
            const auto* layer = TryGetManagedTilemapLayerComponent(entityHandle);
            return layer ? layer->RenderOrder : 0;
        }

        void ManagedSetTilemapLayerRenderOrderIcall(uint32_t entityHandle, int value)
        {
            if (auto* layer = TryGetManagedTilemapLayerComponent(entityHandle))
                layer->RenderOrder = value;
        }

        bool ManagedGetTilemapLayerCollisionEnabledIcall(uint32_t entityHandle)
        {
            const auto* layer = TryGetManagedTilemapLayerComponent(entityHandle);
            return layer ? layer->CollisionEnabled : false;
        }

        void ManagedSetTilemapLayerCollisionEnabledIcall(uint32_t entityHandle, bool value)
        {
            if (auto* layer = TryGetManagedTilemapLayerComponent(entityHandle))
                layer->CollisionEnabled = value;
        }

        bool ManagedGetTilemapLayerCastShadowsIcall(uint32_t entityHandle)
        {
            const auto* layer = TryGetManagedTilemapLayerComponent(entityHandle);
            return layer ? layer->CastShadows : false;
        }

        void ManagedSetTilemapLayerCastShadowsIcall(uint32_t entityHandle, bool value)
        {
            if (auto* layer = TryGetManagedTilemapLayerComponent(entityHandle))
                layer->CastShadows = value;
        }

        int ManagedGetTilemapLayerCellCountIcall(uint32_t entityHandle)
        {
            const auto* layer = TryGetManagedTilemapLayerComponent(entityHandle);
            return layer ? layer->GetCellCount() : 0;
        }

        bool ManagedIsTilemapLayerCellInBoundsIcall(uint32_t entityHandle, int cellX, int cellY)
        {
            const auto* layer = TryGetManagedTilemapLayerComponent(entityHandle);
            return layer ? IsLayerCellInBounds(*layer, cellX, cellY) : false;
        }

        int ManagedGetTilemapLayerTileIdIcall(uint32_t entityHandle, int cellX, int cellY)
        {
            auto* layer = TryGetManagedTilemapLayerComponent(entityHandle);
            if (layer == nullptr || !IsLayerCellInBounds(*layer, cellX, cellY))
                return 0;

            layer->EnsureStorage();
            const size_t index = LayerCellToIndex(*layer, cellX, cellY);
            return index < layer->Tiles.size() ? static_cast<int>(layer->Tiles[index]) : 0;
        }

        void ManagedSetTilemapLayerTileIdIcall(uint32_t entityHandle, int cellX, int cellY, int tileId)
        {
            auto* layer = TryGetManagedTilemapLayerComponent(entityHandle);
            if (layer == nullptr || !IsLayerCellInBounds(*layer, cellX, cellY))
                return;

            layer->EnsureStorage();
            const uint32_t resolvedTileId = (tileId > 0 && static_cast<size_t>(tileId) < layer->TileTable.size())
                ? static_cast<uint32_t>(tileId)
                : 0u;
            const size_t index = LayerCellToIndex(*layer, cellX, cellY);
            if (index < layer->Tiles.size())
                layer->Tiles[index] = resolvedTileId;
        }

        Coral::String ManagedGetTilemapLayerTileAssetKeyIcall(uint32_t entityHandle, int cellX, int cellY)
        {
            auto* layer = TryGetManagedTilemapLayerComponent(entityHandle);
            if (layer == nullptr || !IsLayerCellInBounds(*layer, cellX, cellY))
                return Coral::String::New("");

            layer->EnsureStorage();
            const size_t index = LayerCellToIndex(*layer, cellX, cellY);
            if (index >= layer->Tiles.size())
                return Coral::String::New("");

            const uint32_t tileId = layer->Tiles[index];
            if (tileId == 0u || static_cast<size_t>(tileId) >= layer->TileTable.size())
                return Coral::String::New("");

            return Coral::String::New(layer->TileTable[tileId]);
        }

        void ManagedSetTilemapLayerTileAssetKeyIcall(uint32_t entityHandle, int cellX, int cellY, Coral::String value)
        {
            auto* layer = TryGetManagedTilemapLayerComponent(entityHandle);
            if (layer == nullptr || !IsLayerCellInBounds(*layer, cellX, cellY))
                return;

            layer->EnsureStorage();
            const std::string tileAssetKey = ToUtf8Borrowed(value);
            const uint32_t tileId = layer->GetOrAddTileTableEntry(tileAssetKey);
            const size_t index = LayerCellToIndex(*layer, cellX, cellY);
            if (index < layer->Tiles.size())
                layer->Tiles[index] = tileId;
        }

        int ManagedGetTilemapLayerTileTableEntryCountIcall(uint32_t entityHandle)
        {
            const auto* layer = TryGetManagedTilemapLayerComponent(entityHandle);
            return layer ? static_cast<int>(layer->TileTable.size()) : 0;
        }

        Coral::String ManagedGetTilemapLayerTileTableEntryIcall(uint32_t entityHandle, int index)
        {
            const auto* layer = TryGetManagedTilemapLayerComponent(entityHandle);
            if (layer == nullptr || index < 0 || static_cast<size_t>(index) >= layer->TileTable.size())
                return Coral::String::New("");

            return Coral::String::New(layer->TileTable[static_cast<size_t>(index)]);
        }

        void ManagedSetTilemapLayerTileTableEntryIcall(uint32_t entityHandle, int index, Coral::String value)
        {
            auto* layer = TryGetManagedTilemapLayerComponent(entityHandle);
            if (layer == nullptr || index <= 0 || static_cast<size_t>(index) >= layer->TileTable.size())
                return;

            const std::string tileAssetKey = ToUtf8Borrowed(value);
            if (tileAssetKey.empty())
            {
                layer->TileTable[static_cast<size_t>(index)].clear();
                layer->EnsureStorage();
                for (uint32_t& tileId : layer->Tiles)
                {
                    if (tileId == static_cast<uint32_t>(index))
                        tileId = 0u;
                }
            }
            else
            {
                layer->TileTable[static_cast<size_t>(index)] = tileAssetKey;
            }

            layer->RenderCacheDirty = true;
        }

        int ManagedGetOrAddTilemapLayerTileTableEntryIcall(uint32_t entityHandle, Coral::String value)
        {
            auto* layer = TryGetManagedTilemapLayerComponent(entityHandle);
            if (layer == nullptr)
                return 0;

            return static_cast<int>(layer->GetOrAddTileTableEntry(ToUtf8Borrowed(value)));
        }

        bool ManagedHasRigidbody2DComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedRigidbody2DComponent(entityHandle) != nullptr;
        }

        int ManagedGetRigidbody2DBodyTypeIcall(uint32_t entityHandle)
        {
            const auto* rigidbody = TryGetManagedRigidbody2DComponent(entityHandle);
            return rigidbody ? static_cast<int>(rigidbody->Type) : static_cast<int>(Rigidbody2DComponent::BodyType::Static);
        }

        void ManagedSetRigidbody2DBodyTypeIcall(uint32_t entityHandle, int bodyType)
        {
            auto* rigidbody = TryGetManagedRigidbody2DComponent(entityHandle);
            if (rigidbody == nullptr)
                return;

            switch (bodyType)
            {
                case static_cast<int>(Rigidbody2DComponent::BodyType::Static):
                    rigidbody->Type = Rigidbody2DComponent::BodyType::Static;
                    break;
                case static_cast<int>(Rigidbody2DComponent::BodyType::Dynamic):
                    rigidbody->Type = Rigidbody2DComponent::BodyType::Dynamic;
                    break;
                case static_cast<int>(Rigidbody2DComponent::BodyType::Kinematic):
                    rigidbody->Type = Rigidbody2DComponent::BodyType::Kinematic;
                    break;
                default:
                    break;
            }
        }

        bool ManagedGetRigidbody2DFreezePositionXIcall(uint32_t entityHandle)
        {
            const auto* rigidbody = TryGetManagedRigidbody2DComponent(entityHandle);
            return rigidbody ? rigidbody->FreezePositionX : false;
        }

        void ManagedSetRigidbody2DFreezePositionXIcall(uint32_t entityHandle, bool value)
        {
            if (auto* rigidbody = TryGetManagedRigidbody2DComponent(entityHandle))
                rigidbody->FreezePositionX = value;
        }

        bool ManagedGetRigidbody2DFreezePositionYIcall(uint32_t entityHandle)
        {
            const auto* rigidbody = TryGetManagedRigidbody2DComponent(entityHandle);
            return rigidbody ? rigidbody->FreezePositionY : false;
        }

        void ManagedSetRigidbody2DFreezePositionYIcall(uint32_t entityHandle, bool value)
        {
            if (auto* rigidbody = TryGetManagedRigidbody2DComponent(entityHandle))
                rigidbody->FreezePositionY = value;
        }

        bool ManagedGetRigidbody2DFixedRotationIcall(uint32_t entityHandle)
        {
            const auto* rigidbody = TryGetManagedRigidbody2DComponent(entityHandle);
            return rigidbody ? rigidbody->FixedRotation : false;
        }

        void ManagedSetRigidbody2DFixedRotationIcall(uint32_t entityHandle, bool value)
        {
            if (auto* rigidbody = TryGetManagedRigidbody2DComponent(entityHandle))
                rigidbody->FixedRotation = value;
        }

        bool ManagedGetRigidbody2DUseCCDIcall(uint32_t entityHandle)
        {
            const auto* rigidbody = TryGetManagedRigidbody2DComponent(entityHandle);
            return rigidbody ? rigidbody->UseCCD : false;
        }

        void ManagedSetRigidbody2DUseCCDIcall(uint32_t entityHandle, bool value)
        {
            if (auto* rigidbody = TryGetManagedRigidbody2DComponent(entityHandle))
                rigidbody->UseCCD = value;
        }

        bool ManagedGetRigidbody2DEnableSleepIcall(uint32_t entityHandle)
        {
            const auto* rigidbody = TryGetManagedRigidbody2DComponent(entityHandle);
            return rigidbody ? rigidbody->EnableSleep : false;
        }

        void ManagedSetRigidbody2DEnableSleepIcall(uint32_t entityHandle, bool value)
        {
            if (auto* rigidbody = TryGetManagedRigidbody2DComponent(entityHandle))
                rigidbody->EnableSleep = value;
        }

        bool ManagedGetRigidbody2DStartAwakeIcall(uint32_t entityHandle)
        {
            const auto* rigidbody = TryGetManagedRigidbody2DComponent(entityHandle);
            return rigidbody ? rigidbody->StartAwake : false;
        }

        void ManagedSetRigidbody2DStartAwakeIcall(uint32_t entityHandle, bool value)
        {
            if (auto* rigidbody = TryGetManagedRigidbody2DComponent(entityHandle))
                rigidbody->StartAwake = value;
        }

        bool ManagedGetRigidbody2DInterpolateIcall(uint32_t entityHandle)
        {
            const auto* rigidbody = TryGetManagedRigidbody2DComponent(entityHandle);
            return rigidbody ? rigidbody->Interpolate : false;
        }

        void ManagedSetRigidbody2DInterpolateIcall(uint32_t entityHandle, bool value)
        {
            if (auto* rigidbody = TryGetManagedRigidbody2DComponent(entityHandle))
                rigidbody->Interpolate = value;
        }

        bool ManagedGetRigidbody2DHighContactQualityIcall(uint32_t entityHandle)
        {
            const auto* rigidbody = TryGetManagedRigidbody2DComponent(entityHandle);
            return rigidbody ? rigidbody->HighContactQuality : false;
        }

        void ManagedSetRigidbody2DHighContactQualityIcall(uint32_t entityHandle, bool value)
        {
            if (auto* rigidbody = TryGetManagedRigidbody2DComponent(entityHandle))
                rigidbody->HighContactQuality = value;
        }

        int ManagedGetRigidbody2DExtraSolverSubStepsIcall(uint32_t entityHandle)
        {
            const auto* rigidbody = TryGetManagedRigidbody2DComponent(entityHandle);
            return rigidbody ? rigidbody->ExtraSolverSubSteps : 0;
        }

        void ManagedSetRigidbody2DExtraSolverSubStepsIcall(uint32_t entityHandle, int value)
        {
            if (auto* rigidbody = TryGetManagedRigidbody2DComponent(entityHandle))
                rigidbody->ExtraSolverSubSteps = std::max(0, value);
        }

        float ManagedGetRigidbody2DGravityScaleIcall(uint32_t entityHandle)
        {
            const auto* rigidbody = TryGetManagedRigidbody2DComponent(entityHandle);
            return rigidbody ? rigidbody->GravityScale : 0.0f;
        }

        void ManagedSetRigidbody2DGravityScaleIcall(uint32_t entityHandle, float value)
        {
            if (auto* rigidbody = TryGetManagedRigidbody2DComponent(entityHandle))
                rigidbody->GravityScale = value;
        }

        float ManagedGetRigidbody2DLinearDampingIcall(uint32_t entityHandle)
        {
            const auto* rigidbody = TryGetManagedRigidbody2DComponent(entityHandle);
            return rigidbody ? rigidbody->LinearDamping : 0.0f;
        }

        void ManagedSetRigidbody2DLinearDampingIcall(uint32_t entityHandle, float value)
        {
            if (auto* rigidbody = TryGetManagedRigidbody2DComponent(entityHandle))
                rigidbody->LinearDamping = value;
        }

        float ManagedGetRigidbody2DAngularDampingIcall(uint32_t entityHandle)
        {
            const auto* rigidbody = TryGetManagedRigidbody2DComponent(entityHandle);
            return rigidbody ? rigidbody->AngularDamping : 0.0f;
        }

        void ManagedSetRigidbody2DAngularDampingIcall(uint32_t entityHandle, float value)
        {
            if (auto* rigidbody = TryGetManagedRigidbody2DComponent(entityHandle))
                rigidbody->AngularDamping = value;
        }

        ManagedVector2 ManagedGetRigidbody2DLinearVelocityIcall(uint32_t entityHandle)
        {
            const auto* rigidbody = TryGetManagedRigidbody2DComponent(entityHandle);
            return rigidbody ? ToManagedVector2(rigidbody->GetLinearVelocity()) : ManagedVector2{};
        }

        void ManagedSetRigidbody2DLinearVelocityIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* rigidbody = TryGetManagedRigidbody2DComponent(entityHandle))
                rigidbody->SetLinearVelocity(ToGlmVector2(value));
        }

        void ManagedSetRigidbody2DLinearVelocityXIcall(uint32_t entityHandle, float value)
        {
            if (auto* rigidbody = TryGetManagedRigidbody2DComponent(entityHandle))
                rigidbody->SetLinearVelocityX(value);
        }

        void ManagedSetRigidbody2DLinearVelocityYIcall(uint32_t entityHandle, float value)
        {
            if (auto* rigidbody = TryGetManagedRigidbody2DComponent(entityHandle))
                rigidbody->SetLinearVelocityY(value);
        }

        void ManagedAddRigidbody2DLinearVelocityIcall(uint32_t entityHandle, ManagedVector2 deltaVelocity)
        {
            if (auto* rigidbody = TryGetManagedRigidbody2DComponent(entityHandle))
                rigidbody->AddLinearVelocity(ToGlmVector2(deltaVelocity));
        }

        int ManagedGetRigidbody2DContactCountIcall(uint32_t entityHandle, bool includeSensorContacts)
        {
            const auto* rigidbody = TryGetManagedRigidbody2DComponent(entityHandle);
            return rigidbody ? rigidbody->GetContactCount(includeSensorContacts) : 0;
        }

        bool ManagedHasContactWithEntityIcall(uint32_t entityHandle, uint32_t otherEntityHandle, bool includeSensorContacts)
        {
            if (s_HostState.ActiveScene == nullptr)
                return false;

            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            const entt::entity otherEntity = ResolveManagedEntityHandle(otherEntityHandle);
            if (entity == entt::null || otherEntity == entt::null)
                return false;

            return s_HostState.ActiveScene->HasActivePhysics2DContact(entity, otherEntity, includeSensorContacts);
        }

        uint32_t ManagedGetContactEntityCountIcall(uint32_t entityHandle, bool includeSensorContacts)
        {
            if (s_HostState.ActiveScene == nullptr)
                return 0;

            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (entity == entt::null)
                return 0;

            return static_cast<uint32_t>(s_HostState.ActiveScene->GetActivePhysics2DContactEntityHandles(entity, includeSensorContacts).size());
        }

        uint32_t ManagedGetContactEntityAtIcall(uint32_t entityHandle, bool includeSensorContacts, uint32_t index)
        {
            if (s_HostState.ActiveScene == nullptr)
                return static_cast<uint32_t>(entt::null);

            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (entity == entt::null)
                return static_cast<uint32_t>(entt::null);

            const auto contactHandles = s_HostState.ActiveScene->GetActivePhysics2DContactEntityHandles(entity, includeSensorContacts);
            if (index >= contactHandles.size())
                return static_cast<uint32_t>(entt::null);
            return static_cast<uint32_t>(s_HostState.ActiveScene->ResolveEntityReference(contactHandles[index]));
        }

        ManagedRaycastHit2D ManagedRaycast2DIcall(ManagedVector2 origin, ManagedVector2 direction, float maxDistance, uint64_t collisionMask)
        {
            if (s_HostState.ActiveScene == nullptr)
                return {};

            const float safeDistance = std::max(0.0f, maxDistance);
            if (safeDistance <= 0.0f)
                return {};

            return ToManagedRaycastHit2D(
                s_HostState.ActiveScene->RaycastClosestAcrossPhysicsWorlds(ToGlmVector2(origin), ToGlmVector2(direction), safeDistance, collisionMask));
        }

        void RegisterInternalCalls(Coral::ManagedAssembly& contractAssembly)
        {
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "LogInfoIcall", reinterpret_cast<void*>(&ManagedLogInfoIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "LogWarningIcall", reinterpret_cast<void*>(&ManagedLogWarningIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "LogErrorIcall", reinterpret_cast<void*>(&ManagedLogErrorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetRandomSeedIcall", reinterpret_cast<void*>(&ManagedSetRandomSeedIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "RandomRangeIntIcall", reinterpret_cast<void*>(&ManagedRandomRangeIntIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "RandomRangeFloatIcall", reinterpret_cast<void*>(&ManagedRandomRangeFloatIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "RandomValueIcall", reinterpret_cast<void*>(&ManagedRandomValueIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "LoadSceneIcall", reinterpret_cast<void*>(&ManagedLoadSceneIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "ReloadCurrentSceneIcall", reinterpret_cast<void*>(&ManagedReloadCurrentSceneIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetActiveSceneIcall", reinterpret_cast<void*>(&ManagedSetActiveSceneIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "UnloadSceneIcall", reinterpret_cast<void*>(&ManagedUnloadSceneIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "CreateEntityIcall", reinterpret_cast<void*>(&ManagedCreateEntityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "DestroyEntityIcall", reinterpret_cast<void*>(&ManagedDestroyEntityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "EntityExistsIcall", reinterpret_cast<void*>(&ManagedEntityExistsIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetEntityEnabledIcall", reinterpret_cast<void*>(&ManagedGetEntityEnabledIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetEntityEnabledIcall", reinterpret_cast<void*>(&ManagedSetEntityEnabledIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "IsEntityEnabledInHierarchyIcall", reinterpret_cast<void*>(&ManagedIsEntityEnabledInHierarchyIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParentIcall", reinterpret_cast<void*>(&ManagedSetParentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParentIcall", reinterpret_cast<void*>(&ManagedGetParentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetChildCountIcall", reinterpret_cast<void*>(&ManagedGetChildCountIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetChildAtIcall", reinterpret_cast<void*>(&ManagedGetChildAtIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "FindEntityByTagIcall", reinterpret_cast<void*>(&ManagedFindEntityByTagIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasTagComponentIcall", reinterpret_cast<void*>(&ManagedHasTagComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetTagIcall", reinterpret_cast<void*>(&ManagedGetTagIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetTagIcall", reinterpret_cast<void*>(&ManagedSetTagIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasTransformComponentIcall", reinterpret_cast<void*>(&ManagedHasTransformComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetTransformPositionIcall", reinterpret_cast<void*>(&ManagedGetTransformPositionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetTransformPositionIcall", reinterpret_cast<void*>(&ManagedSetTransformPositionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetTransformRotationIcall", reinterpret_cast<void*>(&ManagedGetTransformRotationIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetTransformRotationIcall", reinterpret_cast<void*>(&ManagedSetTransformRotationIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetTransformScaleIcall", reinterpret_cast<void*>(&ManagedGetTransformScaleIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetTransformScaleIcall", reinterpret_cast<void*>(&ManagedSetTransformScaleIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasCameraComponentIcall", reinterpret_cast<void*>(&ManagedHasCameraComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCameraProjectionIcall", reinterpret_cast<void*>(&ManagedGetCameraProjectionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCameraProjectionIcall", reinterpret_cast<void*>(&ManagedSetCameraProjectionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCameraPrimaryIcall", reinterpret_cast<void*>(&ManagedGetCameraPrimaryIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCameraPrimaryIcall", reinterpret_cast<void*>(&ManagedSetCameraPrimaryIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCameraZoomIcall", reinterpret_cast<void*>(&ManagedGetCameraZoomIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCameraZoomIcall", reinterpret_cast<void*>(&ManagedSetCameraZoomIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCameraNearPlaneIcall", reinterpret_cast<void*>(&ManagedGetCameraNearPlaneIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCameraNearPlaneIcall", reinterpret_cast<void*>(&ManagedSetCameraNearPlaneIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCameraFarPlaneIcall", reinterpret_cast<void*>(&ManagedGetCameraFarPlaneIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCameraFarPlaneIcall", reinterpret_cast<void*>(&ManagedSetCameraFarPlaneIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCameraFieldOfViewYDegreesIcall", reinterpret_cast<void*>(&ManagedGetCameraFieldOfViewYDegreesIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCameraFieldOfViewYDegreesIcall", reinterpret_cast<void*>(&ManagedSetCameraFieldOfViewYDegreesIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasBoxCollider2DComponentIcall", reinterpret_cast<void*>(&ManagedHasBoxCollider2DComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetBoxCollider2DOffsetIcall", reinterpret_cast<void*>(&ManagedGetBoxCollider2DOffsetIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetBoxCollider2DOffsetIcall", reinterpret_cast<void*>(&ManagedSetBoxCollider2DOffsetIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetBoxCollider2DSizeIcall", reinterpret_cast<void*>(&ManagedGetBoxCollider2DSizeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetBoxCollider2DSizeIcall", reinterpret_cast<void*>(&ManagedSetBoxCollider2DSizeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetBoxCollider2DDensityIcall", reinterpret_cast<void*>(&ManagedGetBoxCollider2DDensityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetBoxCollider2DDensityIcall", reinterpret_cast<void*>(&ManagedSetBoxCollider2DDensityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetBoxCollider2DFrictionIcall", reinterpret_cast<void*>(&ManagedGetBoxCollider2DFrictionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetBoxCollider2DFrictionIcall", reinterpret_cast<void*>(&ManagedSetBoxCollider2DFrictionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetBoxCollider2DRestitutionIcall", reinterpret_cast<void*>(&ManagedGetBoxCollider2DRestitutionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetBoxCollider2DRestitutionIcall", reinterpret_cast<void*>(&ManagedSetBoxCollider2DRestitutionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetBoxCollider2DIsSensorIcall", reinterpret_cast<void*>(&ManagedGetBoxCollider2DIsSensorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetBoxCollider2DIsSensorIcall", reinterpret_cast<void*>(&ManagedSetBoxCollider2DIsSensorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetBoxCollider2DCollisionLayerIcall", reinterpret_cast<void*>(&ManagedGetBoxCollider2DCollisionLayerIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetBoxCollider2DCollisionLayerIcall", reinterpret_cast<void*>(&ManagedSetBoxCollider2DCollisionLayerIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetBoxCollider2DCollisionMaskIcall", reinterpret_cast<void*>(&ManagedGetBoxCollider2DCollisionMaskIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetBoxCollider2DCollisionMaskIcall", reinterpret_cast<void*>(&ManagedSetBoxCollider2DCollisionMaskIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasCircleCollider2DComponentIcall", reinterpret_cast<void*>(&ManagedHasCircleCollider2DComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCircleCollider2DOffsetIcall", reinterpret_cast<void*>(&ManagedGetCircleCollider2DOffsetIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCircleCollider2DOffsetIcall", reinterpret_cast<void*>(&ManagedSetCircleCollider2DOffsetIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCircleCollider2DRadiusIcall", reinterpret_cast<void*>(&ManagedGetCircleCollider2DRadiusIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCircleCollider2DRadiusIcall", reinterpret_cast<void*>(&ManagedSetCircleCollider2DRadiusIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCircleCollider2DDensityIcall", reinterpret_cast<void*>(&ManagedGetCircleCollider2DDensityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCircleCollider2DDensityIcall", reinterpret_cast<void*>(&ManagedSetCircleCollider2DDensityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCircleCollider2DFrictionIcall", reinterpret_cast<void*>(&ManagedGetCircleCollider2DFrictionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCircleCollider2DFrictionIcall", reinterpret_cast<void*>(&ManagedSetCircleCollider2DFrictionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCircleCollider2DRestitutionIcall", reinterpret_cast<void*>(&ManagedGetCircleCollider2DRestitutionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCircleCollider2DRestitutionIcall", reinterpret_cast<void*>(&ManagedSetCircleCollider2DRestitutionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCircleCollider2DIsSensorIcall", reinterpret_cast<void*>(&ManagedGetCircleCollider2DIsSensorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCircleCollider2DIsSensorIcall", reinterpret_cast<void*>(&ManagedSetCircleCollider2DIsSensorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCircleCollider2DCollisionLayerIcall", reinterpret_cast<void*>(&ManagedGetCircleCollider2DCollisionLayerIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCircleCollider2DCollisionLayerIcall", reinterpret_cast<void*>(&ManagedSetCircleCollider2DCollisionLayerIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCircleCollider2DCollisionMaskIcall", reinterpret_cast<void*>(&ManagedGetCircleCollider2DCollisionMaskIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCircleCollider2DCollisionMaskIcall", reinterpret_cast<void*>(&ManagedSetCircleCollider2DCollisionMaskIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasPolygonCollider2DComponentIcall", reinterpret_cast<void*>(&ManagedHasPolygonCollider2DComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetPolygonCollider2DOffsetIcall", reinterpret_cast<void*>(&ManagedGetPolygonCollider2DOffsetIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetPolygonCollider2DOffsetIcall", reinterpret_cast<void*>(&ManagedSetPolygonCollider2DOffsetIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetPolygonCollider2DPointCountIcall", reinterpret_cast<void*>(&ManagedGetPolygonCollider2DPointCountIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetPolygonCollider2DPointCountIcall", reinterpret_cast<void*>(&ManagedSetPolygonCollider2DPointCountIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetPolygonCollider2DPointIcall", reinterpret_cast<void*>(&ManagedGetPolygonCollider2DPointIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetPolygonCollider2DPointIcall", reinterpret_cast<void*>(&ManagedSetPolygonCollider2DPointIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetPolygonCollider2DDensityIcall", reinterpret_cast<void*>(&ManagedGetPolygonCollider2DDensityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetPolygonCollider2DDensityIcall", reinterpret_cast<void*>(&ManagedSetPolygonCollider2DDensityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetPolygonCollider2DFrictionIcall", reinterpret_cast<void*>(&ManagedGetPolygonCollider2DFrictionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetPolygonCollider2DFrictionIcall", reinterpret_cast<void*>(&ManagedSetPolygonCollider2DFrictionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetPolygonCollider2DRestitutionIcall", reinterpret_cast<void*>(&ManagedGetPolygonCollider2DRestitutionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetPolygonCollider2DRestitutionIcall", reinterpret_cast<void*>(&ManagedSetPolygonCollider2DRestitutionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetPolygonCollider2DIsSensorIcall", reinterpret_cast<void*>(&ManagedGetPolygonCollider2DIsSensorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetPolygonCollider2DIsSensorIcall", reinterpret_cast<void*>(&ManagedSetPolygonCollider2DIsSensorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetPolygonCollider2DCollisionLayerIcall", reinterpret_cast<void*>(&ManagedGetPolygonCollider2DCollisionLayerIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetPolygonCollider2DCollisionLayerIcall", reinterpret_cast<void*>(&ManagedSetPolygonCollider2DCollisionLayerIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetPolygonCollider2DCollisionMaskIcall", reinterpret_cast<void*>(&ManagedGetPolygonCollider2DCollisionMaskIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetPolygonCollider2DCollisionMaskIcall", reinterpret_cast<void*>(&ManagedSetPolygonCollider2DCollisionMaskIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasEdgeCollider2DComponentIcall", reinterpret_cast<void*>(&ManagedHasEdgeCollider2DComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetEdgeCollider2DOffsetIcall", reinterpret_cast<void*>(&ManagedGetEdgeCollider2DOffsetIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetEdgeCollider2DOffsetIcall", reinterpret_cast<void*>(&ManagedSetEdgeCollider2DOffsetIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetEdgeCollider2DPointAIcall", reinterpret_cast<void*>(&ManagedGetEdgeCollider2DPointAIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetEdgeCollider2DPointAIcall", reinterpret_cast<void*>(&ManagedSetEdgeCollider2DPointAIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetEdgeCollider2DPointBIcall", reinterpret_cast<void*>(&ManagedGetEdgeCollider2DPointBIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetEdgeCollider2DPointBIcall", reinterpret_cast<void*>(&ManagedSetEdgeCollider2DPointBIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetEdgeCollider2DFrictionIcall", reinterpret_cast<void*>(&ManagedGetEdgeCollider2DFrictionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetEdgeCollider2DFrictionIcall", reinterpret_cast<void*>(&ManagedSetEdgeCollider2DFrictionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetEdgeCollider2DRestitutionIcall", reinterpret_cast<void*>(&ManagedGetEdgeCollider2DRestitutionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetEdgeCollider2DRestitutionIcall", reinterpret_cast<void*>(&ManagedSetEdgeCollider2DRestitutionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetEdgeCollider2DIsSensorIcall", reinterpret_cast<void*>(&ManagedGetEdgeCollider2DIsSensorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetEdgeCollider2DIsSensorIcall", reinterpret_cast<void*>(&ManagedSetEdgeCollider2DIsSensorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetEdgeCollider2DCollisionLayerIcall", reinterpret_cast<void*>(&ManagedGetEdgeCollider2DCollisionLayerIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetEdgeCollider2DCollisionLayerIcall", reinterpret_cast<void*>(&ManagedSetEdgeCollider2DCollisionLayerIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetEdgeCollider2DCollisionMaskIcall", reinterpret_cast<void*>(&ManagedGetEdgeCollider2DCollisionMaskIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetEdgeCollider2DCollisionMaskIcall", reinterpret_cast<void*>(&ManagedSetEdgeCollider2DCollisionMaskIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasCapsuleCollider2DComponentIcall", reinterpret_cast<void*>(&ManagedHasCapsuleCollider2DComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCapsuleCollider2DOffsetIcall", reinterpret_cast<void*>(&ManagedGetCapsuleCollider2DOffsetIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCapsuleCollider2DOffsetIcall", reinterpret_cast<void*>(&ManagedSetCapsuleCollider2DOffsetIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCapsuleCollider2DSizeIcall", reinterpret_cast<void*>(&ManagedGetCapsuleCollider2DSizeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCapsuleCollider2DSizeIcall", reinterpret_cast<void*>(&ManagedSetCapsuleCollider2DSizeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCapsuleCollider2DDirectionIcall", reinterpret_cast<void*>(&ManagedGetCapsuleCollider2DDirectionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCapsuleCollider2DDirectionIcall", reinterpret_cast<void*>(&ManagedSetCapsuleCollider2DDirectionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCapsuleCollider2DDensityIcall", reinterpret_cast<void*>(&ManagedGetCapsuleCollider2DDensityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCapsuleCollider2DDensityIcall", reinterpret_cast<void*>(&ManagedSetCapsuleCollider2DDensityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCapsuleCollider2DFrictionIcall", reinterpret_cast<void*>(&ManagedGetCapsuleCollider2DFrictionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCapsuleCollider2DFrictionIcall", reinterpret_cast<void*>(&ManagedSetCapsuleCollider2DFrictionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCapsuleCollider2DRestitutionIcall", reinterpret_cast<void*>(&ManagedGetCapsuleCollider2DRestitutionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCapsuleCollider2DRestitutionIcall", reinterpret_cast<void*>(&ManagedSetCapsuleCollider2DRestitutionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCapsuleCollider2DIsSensorIcall", reinterpret_cast<void*>(&ManagedGetCapsuleCollider2DIsSensorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCapsuleCollider2DIsSensorIcall", reinterpret_cast<void*>(&ManagedSetCapsuleCollider2DIsSensorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCapsuleCollider2DCollisionLayerIcall", reinterpret_cast<void*>(&ManagedGetCapsuleCollider2DCollisionLayerIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCapsuleCollider2DCollisionLayerIcall", reinterpret_cast<void*>(&ManagedSetCapsuleCollider2DCollisionLayerIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCapsuleCollider2DCollisionMaskIcall", reinterpret_cast<void*>(&ManagedGetCapsuleCollider2DCollisionMaskIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCapsuleCollider2DCollisionMaskIcall", reinterpret_cast<void*>(&ManagedSetCapsuleCollider2DCollisionMaskIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasJoint2DComponentIcall", reinterpret_cast<void*>(&ManagedHasJoint2DComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetJoint2DTypeIcall", reinterpret_cast<void*>(&ManagedGetJoint2DTypeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetJoint2DTypeIcall", reinterpret_cast<void*>(&ManagedSetJoint2DTypeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetJoint2DConnectedEntityIcall", reinterpret_cast<void*>(&ManagedGetJoint2DConnectedEntityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetJoint2DConnectedEntityIcall", reinterpret_cast<void*>(&ManagedSetJoint2DConnectedEntityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetJoint2DCollideConnectedIcall", reinterpret_cast<void*>(&ManagedGetJoint2DCollideConnectedIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetJoint2DCollideConnectedIcall", reinterpret_cast<void*>(&ManagedSetJoint2DCollideConnectedIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetJoint2DAnchorAIcall", reinterpret_cast<void*>(&ManagedGetJoint2DAnchorAIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetJoint2DAnchorAIcall", reinterpret_cast<void*>(&ManagedSetJoint2DAnchorAIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetJoint2DAnchorBIcall", reinterpret_cast<void*>(&ManagedGetJoint2DAnchorBIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetJoint2DAnchorBIcall", reinterpret_cast<void*>(&ManagedSetJoint2DAnchorBIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetJoint2DAxisIcall", reinterpret_cast<void*>(&ManagedGetJoint2DAxisIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetJoint2DAxisIcall", reinterpret_cast<void*>(&ManagedSetJoint2DAxisIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetJoint2DEnableLimitIcall", reinterpret_cast<void*>(&ManagedGetJoint2DEnableLimitIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetJoint2DEnableLimitIcall", reinterpret_cast<void*>(&ManagedSetJoint2DEnableLimitIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetJoint2DLimitsIcall", reinterpret_cast<void*>(&ManagedGetJoint2DLimitsIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetJoint2DLimitsIcall", reinterpret_cast<void*>(&ManagedSetJoint2DLimitsIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetJoint2DEnableMotorIcall", reinterpret_cast<void*>(&ManagedGetJoint2DEnableMotorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetJoint2DEnableMotorIcall", reinterpret_cast<void*>(&ManagedSetJoint2DEnableMotorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetJoint2DMotorSpeedIcall", reinterpret_cast<void*>(&ManagedGetJoint2DMotorSpeedIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetJoint2DMotorSpeedIcall", reinterpret_cast<void*>(&ManagedSetJoint2DMotorSpeedIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetJoint2DMaxMotorForceOrTorqueIcall", reinterpret_cast<void*>(&ManagedGetJoint2DMaxMotorForceOrTorqueIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetJoint2DMaxMotorForceOrTorqueIcall", reinterpret_cast<void*>(&ManagedSetJoint2DMaxMotorForceOrTorqueIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetJoint2DEnableSpringIcall", reinterpret_cast<void*>(&ManagedGetJoint2DEnableSpringIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetJoint2DEnableSpringIcall", reinterpret_cast<void*>(&ManagedSetJoint2DEnableSpringIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetJoint2DHertzIcall", reinterpret_cast<void*>(&ManagedGetJoint2DHertzIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetJoint2DHertzIcall", reinterpret_cast<void*>(&ManagedSetJoint2DHertzIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetJoint2DDampingRatioIcall", reinterpret_cast<void*>(&ManagedGetJoint2DDampingRatioIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetJoint2DDampingRatioIcall", reinterpret_cast<void*>(&ManagedSetJoint2DDampingRatioIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasSpriteComponentIcall", reinterpret_cast<void*>(&ManagedHasSpriteComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetSpriteTextureKeyIcall", reinterpret_cast<void*>(&ManagedGetSpriteTextureKeyIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetSpriteTextureKeyIcall", reinterpret_cast<void*>(&ManagedSetSpriteTextureKeyIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetSpriteColorIcall", reinterpret_cast<void*>(&ManagedGetSpriteColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetSpriteColorIcall", reinterpret_cast<void*>(&ManagedSetSpriteColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetSpriteTilingFactorIcall", reinterpret_cast<void*>(&ManagedGetSpriteTilingFactorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetSpriteTilingFactorIcall", reinterpret_cast<void*>(&ManagedSetSpriteTilingFactorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetSpriteRenderOrderIcall", reinterpret_cast<void*>(&ManagedGetSpriteRenderOrderIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetSpriteRenderOrderIcall", reinterpret_cast<void*>(&ManagedSetSpriteRenderOrderIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetSpriteCastShadowsIcall", reinterpret_cast<void*>(&ManagedGetSpriteCastShadowsIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetSpriteCastShadowsIcall", reinterpret_cast<void*>(&ManagedSetSpriteCastShadowsIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetSpriteReceiveShadowsIcall", reinterpret_cast<void*>(&ManagedGetSpriteReceiveShadowsIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetSpriteReceiveShadowsIcall", reinterpret_cast<void*>(&ManagedSetSpriteReceiveShadowsIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetSpriteSubSpriteIndexIcall", reinterpret_cast<void*>(&ManagedGetSpriteSubSpriteIndexIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetSpriteSubSpriteIndexIcall", reinterpret_cast<void*>(&ManagedSetSpriteSubSpriteIndexIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetSpriteUvMinIcall", reinterpret_cast<void*>(&ManagedGetSpriteUvMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetSpriteUvMinIcall", reinterpret_cast<void*>(&ManagedSetSpriteUvMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetSpriteUvMaxIcall", reinterpret_cast<void*>(&ManagedGetSpriteUvMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetSpriteUvMaxIcall", reinterpret_cast<void*>(&ManagedSetSpriteUvMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasMaterialComponentIcall", reinterpret_cast<void*>(&ManagedHasMaterialComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetMaterialKeyIcall", reinterpret_cast<void*>(&ManagedGetMaterialKeyIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetMaterialKeyIcall", reinterpret_cast<void*>(&ManagedSetMaterialKeyIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasCanvasComponentIcall", reinterpret_cast<void*>(&ManagedHasCanvasComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCanvasRenderModeIcall", reinterpret_cast<void*>(&ManagedGetCanvasRenderModeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCanvasRenderModeIcall", reinterpret_cast<void*>(&ManagedSetCanvasRenderModeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCanvasSortOrderIcall", reinterpret_cast<void*>(&ManagedGetCanvasSortOrderIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCanvasSortOrderIcall", reinterpret_cast<void*>(&ManagedSetCanvasSortOrderIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCanvasReferenceResolutionIcall", reinterpret_cast<void*>(&ManagedGetCanvasReferenceResolutionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCanvasReferenceResolutionIcall", reinterpret_cast<void*>(&ManagedSetCanvasReferenceResolutionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasRectTransformComponentIcall", reinterpret_cast<void*>(&ManagedHasRectTransformComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetRectTransformAnchorMinIcall", reinterpret_cast<void*>(&ManagedGetRectTransformAnchorMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetRectTransformAnchorMinIcall", reinterpret_cast<void*>(&ManagedSetRectTransformAnchorMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetRectTransformAnchorMaxIcall", reinterpret_cast<void*>(&ManagedGetRectTransformAnchorMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetRectTransformAnchorMaxIcall", reinterpret_cast<void*>(&ManagedSetRectTransformAnchorMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetRectTransformPivotIcall", reinterpret_cast<void*>(&ManagedGetRectTransformPivotIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetRectTransformPivotIcall", reinterpret_cast<void*>(&ManagedSetRectTransformPivotIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetRectTransformSizeDeltaIcall", reinterpret_cast<void*>(&ManagedGetRectTransformSizeDeltaIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetRectTransformSizeDeltaIcall", reinterpret_cast<void*>(&ManagedSetRectTransformSizeDeltaIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetRectTransformAnchoredPositionIcall", reinterpret_cast<void*>(&ManagedGetRectTransformAnchoredPositionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetRectTransformAnchoredPositionIcall", reinterpret_cast<void*>(&ManagedSetRectTransformAnchoredPositionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasDirectionalLight2DComponentIcall", reinterpret_cast<void*>(&ManagedHasDirectionalLight2DComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetDirectionalLight2DEnabledIcall", reinterpret_cast<void*>(&ManagedGetDirectionalLight2DEnabledIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetDirectionalLight2DEnabledIcall", reinterpret_cast<void*>(&ManagedSetDirectionalLight2DEnabledIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetDirectionalLight2DColorIcall", reinterpret_cast<void*>(&ManagedGetDirectionalLight2DColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetDirectionalLight2DColorIcall", reinterpret_cast<void*>(&ManagedSetDirectionalLight2DColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetDirectionalLight2DIntensityIcall", reinterpret_cast<void*>(&ManagedGetDirectionalLight2DIntensityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetDirectionalLight2DIntensityIcall", reinterpret_cast<void*>(&ManagedSetDirectionalLight2DIntensityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetDirectionalLight2DUseEntityRotationIcall", reinterpret_cast<void*>(&ManagedGetDirectionalLight2DUseEntityRotationIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetDirectionalLight2DUseEntityRotationIcall", reinterpret_cast<void*>(&ManagedSetDirectionalLight2DUseEntityRotationIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetDirectionalLight2DDirectionIcall", reinterpret_cast<void*>(&ManagedGetDirectionalLight2DDirectionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetDirectionalLight2DDirectionIcall", reinterpret_cast<void*>(&ManagedSetDirectionalLight2DDirectionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetDirectionalLight2DCastShadowsIcall", reinterpret_cast<void*>(&ManagedGetDirectionalLight2DCastShadowsIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetDirectionalLight2DCastShadowsIcall", reinterpret_cast<void*>(&ManagedSetDirectionalLight2DCastShadowsIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetDirectionalLight2DShadowStrengthIcall", reinterpret_cast<void*>(&ManagedGetDirectionalLight2DShadowStrengthIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetDirectionalLight2DShadowStrengthIcall", reinterpret_cast<void*>(&ManagedSetDirectionalLight2DShadowStrengthIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetDirectionalLight2DShadowSoftnessIcall", reinterpret_cast<void*>(&ManagedGetDirectionalLight2DShadowSoftnessIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetDirectionalLight2DShadowSoftnessIcall", reinterpret_cast<void*>(&ManagedSetDirectionalLight2DShadowSoftnessIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetDirectionalLight2DShadowSamplesIcall", reinterpret_cast<void*>(&ManagedGetDirectionalLight2DShadowSamplesIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetDirectionalLight2DShadowSamplesIcall", reinterpret_cast<void*>(&ManagedSetDirectionalLight2DShadowSamplesIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetDirectionalLight2DShadowDistanceIcall", reinterpret_cast<void*>(&ManagedGetDirectionalLight2DShadowDistanceIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetDirectionalLight2DShadowDistanceIcall", reinterpret_cast<void*>(&ManagedSetDirectionalLight2DShadowDistanceIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetDirectionalLight2DShadowBiasIcall", reinterpret_cast<void*>(&ManagedGetDirectionalLight2DShadowBiasIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetDirectionalLight2DShadowBiasIcall", reinterpret_cast<void*>(&ManagedSetDirectionalLight2DShadowBiasIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasPointLight2DComponentIcall", reinterpret_cast<void*>(&ManagedHasPointLight2DComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetPointLight2DEnabledIcall", reinterpret_cast<void*>(&ManagedGetPointLight2DEnabledIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetPointLight2DEnabledIcall", reinterpret_cast<void*>(&ManagedSetPointLight2DEnabledIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetPointLight2DColorIcall", reinterpret_cast<void*>(&ManagedGetPointLight2DColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetPointLight2DColorIcall", reinterpret_cast<void*>(&ManagedSetPointLight2DColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetPointLight2DIntensityIcall", reinterpret_cast<void*>(&ManagedGetPointLight2DIntensityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetPointLight2DIntensityIcall", reinterpret_cast<void*>(&ManagedSetPointLight2DIntensityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetPointLight2DRadiusIcall", reinterpret_cast<void*>(&ManagedGetPointLight2DRadiusIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetPointLight2DRadiusIcall", reinterpret_cast<void*>(&ManagedSetPointLight2DRadiusIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetPointLight2DFalloffIcall", reinterpret_cast<void*>(&ManagedGetPointLight2DFalloffIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetPointLight2DFalloffIcall", reinterpret_cast<void*>(&ManagedSetPointLight2DFalloffIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetPointLight2DCastShadowsIcall", reinterpret_cast<void*>(&ManagedGetPointLight2DCastShadowsIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetPointLight2DCastShadowsIcall", reinterpret_cast<void*>(&ManagedSetPointLight2DCastShadowsIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetPointLight2DShadowStrengthIcall", reinterpret_cast<void*>(&ManagedGetPointLight2DShadowStrengthIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetPointLight2DShadowStrengthIcall", reinterpret_cast<void*>(&ManagedSetPointLight2DShadowStrengthIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetPointLight2DShadowSoftnessIcall", reinterpret_cast<void*>(&ManagedGetPointLight2DShadowSoftnessIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetPointLight2DShadowSoftnessIcall", reinterpret_cast<void*>(&ManagedSetPointLight2DShadowSoftnessIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetPointLight2DShadowSamplesIcall", reinterpret_cast<void*>(&ManagedGetPointLight2DShadowSamplesIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetPointLight2DShadowSamplesIcall", reinterpret_cast<void*>(&ManagedSetPointLight2DShadowSamplesIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetPointLight2DShadowBiasIcall", reinterpret_cast<void*>(&ManagedGetPointLight2DShadowBiasIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetPointLight2DShadowBiasIcall", reinterpret_cast<void*>(&ManagedSetPointLight2DShadowBiasIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasUIImageComponentIcall", reinterpret_cast<void*>(&ManagedHasUIImageComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUIImageRaycastTargetIcall", reinterpret_cast<void*>(&ManagedGetUIImageRaycastTargetIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUIImageRaycastTargetIcall", reinterpret_cast<void*>(&ManagedSetUIImageRaycastTargetIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasUIPanelComponentIcall", reinterpret_cast<void*>(&ManagedHasUIPanelComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUIPanelBackgroundColorIcall", reinterpret_cast<void*>(&ManagedGetUIPanelBackgroundColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUIPanelBackgroundColorIcall", reinterpret_cast<void*>(&ManagedSetUIPanelBackgroundColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUIPanelUseSpriteTextureIcall", reinterpret_cast<void*>(&ManagedGetUIPanelUseSpriteTextureIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUIPanelUseSpriteTextureIcall", reinterpret_cast<void*>(&ManagedSetUIPanelUseSpriteTextureIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUIPanelRaycastTargetIcall", reinterpret_cast<void*>(&ManagedGetUIPanelRaycastTargetIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUIPanelRaycastTargetIcall", reinterpret_cast<void*>(&ManagedSetUIPanelRaycastTargetIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasUITextComponentIcall", reinterpret_cast<void*>(&ManagedHasUITextComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUITextValueIcall", reinterpret_cast<void*>(&ManagedGetUITextValueIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUITextValueIcall", reinterpret_cast<void*>(&ManagedSetUITextValueIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUITextFontFilePathIcall", reinterpret_cast<void*>(&ManagedGetUITextFontFilePathIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUITextFontFilePathIcall", reinterpret_cast<void*>(&ManagedSetUITextFontFilePathIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUITextFontSizeIcall", reinterpret_cast<void*>(&ManagedGetUITextFontSizeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUITextFontSizeIcall", reinterpret_cast<void*>(&ManagedSetUITextFontSizeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUITextColorIcall", reinterpret_cast<void*>(&ManagedGetUITextColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUITextColorIcall", reinterpret_cast<void*>(&ManagedSetUITextColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUITextRaycastTargetIcall", reinterpret_cast<void*>(&ManagedGetUITextRaycastTargetIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUITextRaycastTargetIcall", reinterpret_cast<void*>(&ManagedSetUITextRaycastTargetIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasUIButtonComponentIcall", reinterpret_cast<void*>(&ManagedHasUIButtonComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUIButtonInteractableIcall", reinterpret_cast<void*>(&ManagedGetUIButtonInteractableIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUIButtonInteractableIcall", reinterpret_cast<void*>(&ManagedSetUIButtonInteractableIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUIButtonUseStateColorsIcall", reinterpret_cast<void*>(&ManagedGetUIButtonUseStateColorsIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUIButtonUseStateColorsIcall", reinterpret_cast<void*>(&ManagedSetUIButtonUseStateColorsIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUIButtonNormalColorIcall", reinterpret_cast<void*>(&ManagedGetUIButtonNormalColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUIButtonNormalColorIcall", reinterpret_cast<void*>(&ManagedSetUIButtonNormalColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUIButtonHoveredColorIcall", reinterpret_cast<void*>(&ManagedGetUIButtonHoveredColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUIButtonHoveredColorIcall", reinterpret_cast<void*>(&ManagedSetUIButtonHoveredColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUIButtonPressedColorIcall", reinterpret_cast<void*>(&ManagedGetUIButtonPressedColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUIButtonPressedColorIcall", reinterpret_cast<void*>(&ManagedSetUIButtonPressedColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUIButtonDisabledColorIcall", reinterpret_cast<void*>(&ManagedGetUIButtonDisabledColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUIButtonDisabledColorIcall", reinterpret_cast<void*>(&ManagedSetUIButtonDisabledColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUIButtonIsHoveredIcall", reinterpret_cast<void*>(&ManagedGetUIButtonIsHoveredIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUIButtonIsPressedIcall", reinterpret_cast<void*>(&ManagedGetUIButtonIsPressedIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUIButtonOnClickEventIcall", reinterpret_cast<void*>(&ManagedGetUIButtonOnClickEventIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUIButtonOnClickEventIcall", reinterpret_cast<void*>(&ManagedSetUIButtonOnClickEventIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUIButtonOnHoverEnterEventIcall", reinterpret_cast<void*>(&ManagedGetUIButtonOnHoverEnterEventIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUIButtonOnHoverEnterEventIcall", reinterpret_cast<void*>(&ManagedSetUIButtonOnHoverEnterEventIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUIButtonOnHoverExitEventIcall", reinterpret_cast<void*>(&ManagedGetUIButtonOnHoverExitEventIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUIButtonOnHoverExitEventIcall", reinterpret_cast<void*>(&ManagedSetUIButtonOnHoverExitEventIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUIButtonOnPressedEventIcall", reinterpret_cast<void*>(&ManagedGetUIButtonOnPressedEventIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUIButtonOnPressedEventIcall", reinterpret_cast<void*>(&ManagedSetUIButtonOnPressedEventIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasUISliderComponentIcall", reinterpret_cast<void*>(&ManagedHasUISliderComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUISliderInteractableIcall", reinterpret_cast<void*>(&ManagedGetUISliderInteractableIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUISliderInteractableIcall", reinterpret_cast<void*>(&ManagedSetUISliderInteractableIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUISliderMinValueIcall", reinterpret_cast<void*>(&ManagedGetUISliderMinValueIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUISliderMinValueIcall", reinterpret_cast<void*>(&ManagedSetUISliderMinValueIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUISliderMaxValueIcall", reinterpret_cast<void*>(&ManagedGetUISliderMaxValueIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUISliderMaxValueIcall", reinterpret_cast<void*>(&ManagedSetUISliderMaxValueIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUISliderValueIcall", reinterpret_cast<void*>(&ManagedGetUISliderValueIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUISliderValueIcall", reinterpret_cast<void*>(&ManagedSetUISliderValueIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUISliderBackgroundColorIcall", reinterpret_cast<void*>(&ManagedGetUISliderBackgroundColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUISliderBackgroundColorIcall", reinterpret_cast<void*>(&ManagedSetUISliderBackgroundColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUISliderFillColorIcall", reinterpret_cast<void*>(&ManagedGetUISliderFillColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUISliderFillColorIcall", reinterpret_cast<void*>(&ManagedSetUISliderFillColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUISliderHandleColorIcall", reinterpret_cast<void*>(&ManagedGetUISliderHandleColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUISliderHandleColorIcall", reinterpret_cast<void*>(&ManagedSetUISliderHandleColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUISliderHandleWidthIcall", reinterpret_cast<void*>(&ManagedGetUISliderHandleWidthIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUISliderHandleWidthIcall", reinterpret_cast<void*>(&ManagedSetUISliderHandleWidthIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUISliderHandleHeightMultiplierIcall", reinterpret_cast<void*>(&ManagedGetUISliderHandleHeightMultiplierIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUISliderHandleHeightMultiplierIcall", reinterpret_cast<void*>(&ManagedSetUISliderHandleHeightMultiplierIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUISliderShowHandleIcall", reinterpret_cast<void*>(&ManagedGetUISliderShowHandleIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUISliderShowHandleIcall", reinterpret_cast<void*>(&ManagedSetUISliderShowHandleIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUISliderRuntimeDraggingIcall", reinterpret_cast<void*>(&ManagedGetUISliderRuntimeDraggingIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUISliderOnValueChangedEventIcall", reinterpret_cast<void*>(&ManagedGetUISliderOnValueChangedEventIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUISliderOnValueChangedEventIcall", reinterpret_cast<void*>(&ManagedSetUISliderOnValueChangedEventIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasAudioListener2DComponentIcall", reinterpret_cast<void*>(&ManagedHasAudioListener2DComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioListener2DEnabledIcall", reinterpret_cast<void*>(&ManagedGetAudioListener2DEnabledIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioListener2DEnabledIcall", reinterpret_cast<void*>(&ManagedSetAudioListener2DEnabledIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioListener2DUsePrimaryCameraPositionIcall", reinterpret_cast<void*>(&ManagedGetAudioListener2DUsePrimaryCameraPositionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioListener2DUsePrimaryCameraPositionIcall", reinterpret_cast<void*>(&ManagedSetAudioListener2DUsePrimaryCameraPositionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasAudioListener3DComponentIcall", reinterpret_cast<void*>(&ManagedHasAudioListener3DComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioListener3DEnabledIcall", reinterpret_cast<void*>(&ManagedGetAudioListener3DEnabledIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioListener3DEnabledIcall", reinterpret_cast<void*>(&ManagedSetAudioListener3DEnabledIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioListener3DUsePrimaryCameraTransformIcall", reinterpret_cast<void*>(&ManagedGetAudioListener3DUsePrimaryCameraTransformIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioListener3DUsePrimaryCameraTransformIcall", reinterpret_cast<void*>(&ManagedSetAudioListener3DUsePrimaryCameraTransformIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasAudioSourceComponentIcall", reinterpret_cast<void*>(&ManagedHasAudioSourceComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourceClipKeyIcall", reinterpret_cast<void*>(&ManagedGetAudioSourceClipKeyIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioSourceClipKeyIcall", reinterpret_cast<void*>(&ManagedSetAudioSourceClipKeyIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourceVolumeIcall", reinterpret_cast<void*>(&ManagedGetAudioSourceVolumeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioSourceVolumeIcall", reinterpret_cast<void*>(&ManagedSetAudioSourceVolumeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourcePitchIcall", reinterpret_cast<void*>(&ManagedGetAudioSourcePitchIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioSourcePitchIcall", reinterpret_cast<void*>(&ManagedSetAudioSourcePitchIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourcePlayOnStartIcall", reinterpret_cast<void*>(&ManagedGetAudioSourcePlayOnStartIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioSourcePlayOnStartIcall", reinterpret_cast<void*>(&ManagedSetAudioSourcePlayOnStartIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourceLoopIcall", reinterpret_cast<void*>(&ManagedGetAudioSourceLoopIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioSourceLoopIcall", reinterpret_cast<void*>(&ManagedSetAudioSourceLoopIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourceMutedIcall", reinterpret_cast<void*>(&ManagedGetAudioSourceMutedIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioSourceMutedIcall", reinterpret_cast<void*>(&ManagedSetAudioSourceMutedIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourcePlaybackSpaceIcall", reinterpret_cast<void*>(&ManagedGetAudioSourcePlaybackSpaceIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioSourcePlaybackSpaceIcall", reinterpret_cast<void*>(&ManagedSetAudioSourcePlaybackSpaceIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourceMixerGroupIcall", reinterpret_cast<void*>(&ManagedGetAudioSourceMixerGroupIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioSourceMixerGroupIcall", reinterpret_cast<void*>(&ManagedSetAudioSourceMixerGroupIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourceSpatialMinDistanceIcall", reinterpret_cast<void*>(&ManagedGetAudioSourceSpatialMinDistanceIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioSourceSpatialMinDistanceIcall", reinterpret_cast<void*>(&ManagedSetAudioSourceSpatialMinDistanceIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourceSpatialMaxDistanceIcall", reinterpret_cast<void*>(&ManagedGetAudioSourceSpatialMaxDistanceIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioSourceSpatialMaxDistanceIcall", reinterpret_cast<void*>(&ManagedSetAudioSourceSpatialMaxDistanceIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourceSpatialRolloffExponentIcall", reinterpret_cast<void*>(&ManagedGetAudioSourceSpatialRolloffExponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioSourceSpatialRolloffExponentIcall", reinterpret_cast<void*>(&ManagedSetAudioSourceSpatialRolloffExponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourceStereoPanStrengthIcall", reinterpret_cast<void*>(&ManagedGetAudioSourceStereoPanStrengthIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioSourceStereoPanStrengthIcall", reinterpret_cast<void*>(&ManagedSetAudioSourceStereoPanStrengthIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourceSpatialRolloffModeIcall", reinterpret_cast<void*>(&ManagedGetAudioSourceSpatialRolloffModeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioSourceSpatialRolloffModeIcall", reinterpret_cast<void*>(&ManagedSetAudioSourceSpatialRolloffModeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourceDopplerFactorIcall", reinterpret_cast<void*>(&ManagedGetAudioSourceDopplerFactorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioSourceDopplerFactorIcall", reinterpret_cast<void*>(&ManagedSetAudioSourceDopplerFactorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourceEnableDirectionalAttenuationIcall", reinterpret_cast<void*>(&ManagedGetAudioSourceEnableDirectionalAttenuationIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioSourceEnableDirectionalAttenuationIcall", reinterpret_cast<void*>(&ManagedSetAudioSourceEnableDirectionalAttenuationIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourceDirectionalInnerAngleDegreesIcall", reinterpret_cast<void*>(&ManagedGetAudioSourceDirectionalInnerAngleDegreesIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioSourceDirectionalInnerAngleDegreesIcall", reinterpret_cast<void*>(&ManagedSetAudioSourceDirectionalInnerAngleDegreesIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourceDirectionalOuterAngleDegreesIcall", reinterpret_cast<void*>(&ManagedGetAudioSourceDirectionalOuterAngleDegreesIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioSourceDirectionalOuterAngleDegreesIcall", reinterpret_cast<void*>(&ManagedSetAudioSourceDirectionalOuterAngleDegreesIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourceDirectionalOuterVolumeIcall", reinterpret_cast<void*>(&ManagedGetAudioSourceDirectionalOuterVolumeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioSourceDirectionalOuterVolumeIcall", reinterpret_cast<void*>(&ManagedSetAudioSourceDirectionalOuterVolumeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourceAttenuationCurveKeyIcall", reinterpret_cast<void*>(&ManagedGetAudioSourceAttenuationCurveKeyIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioSourceAttenuationCurveKeyIcall", reinterpret_cast<void*>(&ManagedSetAudioSourceAttenuationCurveKeyIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourceIsPlayingIcall", reinterpret_cast<void*>(&ManagedGetAudioSourceIsPlayingIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "RequestAudioSourcePlayIcall", reinterpret_cast<void*>(&ManagedRequestAudioSourcePlayIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "StopAudioSourceIcall", reinterpret_cast<void*>(&ManagedStopAudioSourceIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasAnimatorComponentIcall", reinterpret_cast<void*>(&ManagedHasAnimatorComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimatorControllerKeyIcall", reinterpret_cast<void*>(&ManagedGetAnimatorControllerKeyIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAnimatorControllerKeyIcall", reinterpret_cast<void*>(&ManagedSetAnimatorControllerKeyIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimatorDefaultClipKeyIcall", reinterpret_cast<void*>(&ManagedGetAnimatorDefaultClipKeyIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAnimatorDefaultClipKeyIcall", reinterpret_cast<void*>(&ManagedSetAnimatorDefaultClipKeyIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimatorPlaybackSpeedIcall", reinterpret_cast<void*>(&ManagedGetAnimatorPlaybackSpeedIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAnimatorPlaybackSpeedIcall", reinterpret_cast<void*>(&ManagedSetAnimatorPlaybackSpeedIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimatorEnabledIcall", reinterpret_cast<void*>(&ManagedGetAnimatorEnabledIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAnimatorEnabledIcall", reinterpret_cast<void*>(&ManagedSetAnimatorEnabledIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimatorApplyToSpriteIcall", reinterpret_cast<void*>(&ManagedGetAnimatorApplyToSpriteIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAnimatorApplyToSpriteIcall", reinterpret_cast<void*>(&ManagedSetAnimatorApplyToSpriteIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimatorApplyToTransformIcall", reinterpret_cast<void*>(&ManagedGetAnimatorApplyToTransformIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAnimatorApplyToTransformIcall", reinterpret_cast<void*>(&ManagedSetAnimatorApplyToTransformIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimatorAutoPlayIcall", reinterpret_cast<void*>(&ManagedGetAnimatorAutoPlayIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAnimatorAutoPlayIcall", reinterpret_cast<void*>(&ManagedSetAnimatorAutoPlayIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "PlayAnimatorStateIcall", reinterpret_cast<void*>(&ManagedPlayAnimatorStateIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "PlayAnimatorClipIcall", reinterpret_cast<void*>(&ManagedPlayAnimatorClipIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAnimatorBoolParameterIcall", reinterpret_cast<void*>(&ManagedSetAnimatorBoolParameterIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimatorBoolParameterIcall", reinterpret_cast<void*>(&ManagedGetAnimatorBoolParameterIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAnimatorFloatParameterIcall", reinterpret_cast<void*>(&ManagedSetAnimatorFloatParameterIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimatorFloatParameterIcall", reinterpret_cast<void*>(&ManagedGetAnimatorFloatParameterIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAnimatorIntegerParameterIcall", reinterpret_cast<void*>(&ManagedSetAnimatorIntegerParameterIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimatorIntegerParameterIcall", reinterpret_cast<void*>(&ManagedGetAnimatorIntegerParameterIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAnimatorTriggerParameterIcall", reinterpret_cast<void*>(&ManagedSetAnimatorTriggerParameterIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "ResetAnimatorTriggerParameterIcall", reinterpret_cast<void*>(&ManagedResetAnimatorTriggerParameterIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimatorCurrentStateNameIcall", reinterpret_cast<void*>(&ManagedGetAnimatorCurrentStateNameIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimatorCurrentClipKeyIcall", reinterpret_cast<void*>(&ManagedGetAnimatorCurrentClipKeyIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimatorStateTimeSecondsIcall", reinterpret_cast<void*>(&ManagedGetAnimatorStateTimeSecondsIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimatorCurrentStateDurationSecondsIcall", reinterpret_cast<void*>(&ManagedGetAnimatorCurrentStateDurationSecondsIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasAnimationEventReceiverComponentIcall", reinterpret_cast<void*>(&ManagedHasAnimationEventReceiverComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimationEventReceiverEnabledIcall", reinterpret_cast<void*>(&ManagedGetAnimationEventReceiverEnabledIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAnimationEventReceiverEnabledIcall", reinterpret_cast<void*>(&ManagedSetAnimationEventReceiverEnabledIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimationEventReceiverDispatchedEventCountIcall", reinterpret_cast<void*>(&ManagedGetAnimationEventReceiverDispatchedEventCountIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimationEventReceiverEventNameIcall", reinterpret_cast<void*>(&ManagedGetAnimationEventReceiverEventNameIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimationEventReceiverEventStringPayloadIcall", reinterpret_cast<void*>(&ManagedGetAnimationEventReceiverEventStringPayloadIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimationEventReceiverEventFloatPayloadIcall", reinterpret_cast<void*>(&ManagedGetAnimationEventReceiverEventFloatPayloadIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimationEventReceiverEventIntegerPayloadIcall", reinterpret_cast<void*>(&ManagedGetAnimationEventReceiverEventIntegerPayloadIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimationEventReceiverEventBooleanPayloadIcall", reinterpret_cast<void*>(&ManagedGetAnimationEventReceiverEventBooleanPayloadIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimationEventReceiverEventTimeSecondsIcall", reinterpret_cast<void*>(&ManagedGetAnimationEventReceiverEventTimeSecondsIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimationEventReceiverEventNormalizedTimeIcall", reinterpret_cast<void*>(&ManagedGetAnimationEventReceiverEventNormalizedTimeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasParticleEmitterComponentIcall", reinterpret_cast<void*>(&ManagedHasParticleEmitterComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterSpawnRateIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterSpawnRateIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterSpawnRateIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterSpawnRateIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterLifetimeMinIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterLifetimeMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterLifetimeMinIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterLifetimeMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterLifetimeMaxIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterLifetimeMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterLifetimeMaxIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterLifetimeMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterLoopingIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterLoopingIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterLoopingIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterLoopingIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterDurationIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterDurationIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterDurationIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterDurationIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterPlayOnStartIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterPlayOnStartIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterPlayOnStartIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterPlayOnStartIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterBurstEnabledIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterBurstEnabledIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterBurstEnabledIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterBurstEnabledIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterBurstCountIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterBurstCountIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterBurstCountIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterBurstCountIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterSpawnOffsetMinIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterSpawnOffsetMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterSpawnOffsetMinIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterSpawnOffsetMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterSpawnOffsetMaxIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterSpawnOffsetMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterSpawnOffsetMaxIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterSpawnOffsetMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterUseRadialSpawnIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterUseRadialSpawnIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterUseRadialSpawnIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterUseRadialSpawnIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterSpawnRadiusMinIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterSpawnRadiusMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterSpawnRadiusMinIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterSpawnRadiusMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterSpawnRadiusMaxIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterSpawnRadiusMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterSpawnRadiusMaxIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterSpawnRadiusMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterSpeedMinIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterSpeedMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterSpeedMinIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterSpeedMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterSpeedMaxIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterSpeedMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterSpeedMaxIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterSpeedMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterAngleMinIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterAngleMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterAngleMinIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterAngleMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterAngleMaxIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterAngleMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterAngleMaxIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterAngleMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterRadialVelocityIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterRadialVelocityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterRadialVelocityIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterRadialVelocityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterGravityModifierIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterGravityModifierIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterGravityModifierIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterGravityModifierIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterStartSizeMinIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterStartSizeMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterStartSizeMinIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterStartSizeMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterStartSizeMaxIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterStartSizeMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterStartSizeMaxIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterStartSizeMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterEndSizeIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterEndSizeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterEndSizeIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterEndSizeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterStartColorIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterStartColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterStartColorIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterStartColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterEndColorIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterEndColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterEndColorIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterEndColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterStartRotationMinIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterStartRotationMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterStartRotationMinIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterStartRotationMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterStartRotationMaxIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterStartRotationMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterStartRotationMaxIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterStartRotationMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterRotationSpeedMinIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterRotationSpeedMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterRotationSpeedMinIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterRotationSpeedMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterRotationSpeedMaxIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterRotationSpeedMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterRotationSpeedMaxIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterRotationSpeedMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterTextureKeyIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterTextureKeyIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterTextureKeyIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterTextureKeyIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterMaxParticlesIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterMaxParticlesIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterMaxParticlesIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterMaxParticlesIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterIsPlayingIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterIsPlayingIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterIsPausedIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterIsPausedIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterAliveParticleCountIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterAliveParticleCountIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "PlayParticleEmitterIcall", reinterpret_cast<void*>(&ManagedPlayParticleEmitterIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "StopParticleEmitterIcall", reinterpret_cast<void*>(&ManagedStopParticleEmitterIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "PauseParticleEmitterIcall", reinterpret_cast<void*>(&ManagedPauseParticleEmitterIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "ResumeParticleEmitterIcall", reinterpret_cast<void*>(&ManagedResumeParticleEmitterIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "EmitParticleEmitterIcall", reinterpret_cast<void*>(&ManagedEmitParticleEmitterIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasGrid2DComponentIcall", reinterpret_cast<void*>(&ManagedHasGrid2DComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetGrid2DCellSizeIcall", reinterpret_cast<void*>(&ManagedGetGrid2DCellSizeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetGrid2DCellSizeIcall", reinterpret_cast<void*>(&ManagedSetGrid2DCellSizeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetGrid2DCellGapIcall", reinterpret_cast<void*>(&ManagedGetGrid2DCellGapIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetGrid2DCellGapIcall", reinterpret_cast<void*>(&ManagedSetGrid2DCellGapIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasTilemapLayerComponentIcall", reinterpret_cast<void*>(&ManagedHasTilemapLayerComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetTilemapLayerGridWidthIcall", reinterpret_cast<void*>(&ManagedGetTilemapLayerGridWidthIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetTilemapLayerGridHeightIcall", reinterpret_cast<void*>(&ManagedGetTilemapLayerGridHeightIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "ResizeTilemapLayerGridIcall", reinterpret_cast<void*>(&ManagedResizeTilemapLayerGridIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetTilemapLayerRenderOrderIcall", reinterpret_cast<void*>(&ManagedGetTilemapLayerRenderOrderIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetTilemapLayerRenderOrderIcall", reinterpret_cast<void*>(&ManagedSetTilemapLayerRenderOrderIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetTilemapLayerCollisionEnabledIcall", reinterpret_cast<void*>(&ManagedGetTilemapLayerCollisionEnabledIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetTilemapLayerCollisionEnabledIcall", reinterpret_cast<void*>(&ManagedSetTilemapLayerCollisionEnabledIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetTilemapLayerCastShadowsIcall", reinterpret_cast<void*>(&ManagedGetTilemapLayerCastShadowsIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetTilemapLayerCastShadowsIcall", reinterpret_cast<void*>(&ManagedSetTilemapLayerCastShadowsIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetTilemapLayerCellCountIcall", reinterpret_cast<void*>(&ManagedGetTilemapLayerCellCountIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "IsTilemapLayerCellInBoundsIcall", reinterpret_cast<void*>(&ManagedIsTilemapLayerCellInBoundsIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetTilemapLayerTileIdIcall", reinterpret_cast<void*>(&ManagedGetTilemapLayerTileIdIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetTilemapLayerTileIdIcall", reinterpret_cast<void*>(&ManagedSetTilemapLayerTileIdIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetTilemapLayerTileAssetKeyIcall", reinterpret_cast<void*>(&ManagedGetTilemapLayerTileAssetKeyIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetTilemapLayerTileAssetKeyIcall", reinterpret_cast<void*>(&ManagedSetTilemapLayerTileAssetKeyIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetTilemapLayerTileTableEntryCountIcall", reinterpret_cast<void*>(&ManagedGetTilemapLayerTileTableEntryCountIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetTilemapLayerTileTableEntryIcall", reinterpret_cast<void*>(&ManagedGetTilemapLayerTileTableEntryIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetTilemapLayerTileTableEntryIcall", reinterpret_cast<void*>(&ManagedSetTilemapLayerTileTableEntryIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetOrAddTilemapLayerTileTableEntryIcall", reinterpret_cast<void*>(&ManagedGetOrAddTilemapLayerTileTableEntryIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasRigidbody2DComponentIcall", reinterpret_cast<void*>(&ManagedHasRigidbody2DComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetRigidbody2DBodyTypeIcall", reinterpret_cast<void*>(&ManagedGetRigidbody2DBodyTypeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetRigidbody2DBodyTypeIcall", reinterpret_cast<void*>(&ManagedSetRigidbody2DBodyTypeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetRigidbody2DFreezePositionXIcall", reinterpret_cast<void*>(&ManagedGetRigidbody2DFreezePositionXIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetRigidbody2DFreezePositionXIcall", reinterpret_cast<void*>(&ManagedSetRigidbody2DFreezePositionXIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetRigidbody2DFreezePositionYIcall", reinterpret_cast<void*>(&ManagedGetRigidbody2DFreezePositionYIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetRigidbody2DFreezePositionYIcall", reinterpret_cast<void*>(&ManagedSetRigidbody2DFreezePositionYIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetRigidbody2DFixedRotationIcall", reinterpret_cast<void*>(&ManagedGetRigidbody2DFixedRotationIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetRigidbody2DFixedRotationIcall", reinterpret_cast<void*>(&ManagedSetRigidbody2DFixedRotationIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetRigidbody2DUseCCDIcall", reinterpret_cast<void*>(&ManagedGetRigidbody2DUseCCDIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetRigidbody2DUseCCDIcall", reinterpret_cast<void*>(&ManagedSetRigidbody2DUseCCDIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetRigidbody2DEnableSleepIcall", reinterpret_cast<void*>(&ManagedGetRigidbody2DEnableSleepIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetRigidbody2DEnableSleepIcall", reinterpret_cast<void*>(&ManagedSetRigidbody2DEnableSleepIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetRigidbody2DStartAwakeIcall", reinterpret_cast<void*>(&ManagedGetRigidbody2DStartAwakeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetRigidbody2DStartAwakeIcall", reinterpret_cast<void*>(&ManagedSetRigidbody2DStartAwakeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetRigidbody2DInterpolateIcall", reinterpret_cast<void*>(&ManagedGetRigidbody2DInterpolateIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetRigidbody2DInterpolateIcall", reinterpret_cast<void*>(&ManagedSetRigidbody2DInterpolateIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetRigidbody2DHighContactQualityIcall", reinterpret_cast<void*>(&ManagedGetRigidbody2DHighContactQualityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetRigidbody2DHighContactQualityIcall", reinterpret_cast<void*>(&ManagedSetRigidbody2DHighContactQualityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetRigidbody2DExtraSolverSubStepsIcall", reinterpret_cast<void*>(&ManagedGetRigidbody2DExtraSolverSubStepsIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetRigidbody2DExtraSolverSubStepsIcall", reinterpret_cast<void*>(&ManagedSetRigidbody2DExtraSolverSubStepsIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetRigidbody2DGravityScaleIcall", reinterpret_cast<void*>(&ManagedGetRigidbody2DGravityScaleIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetRigidbody2DGravityScaleIcall", reinterpret_cast<void*>(&ManagedSetRigidbody2DGravityScaleIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetRigidbody2DLinearDampingIcall", reinterpret_cast<void*>(&ManagedGetRigidbody2DLinearDampingIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetRigidbody2DLinearDampingIcall", reinterpret_cast<void*>(&ManagedSetRigidbody2DLinearDampingIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetRigidbody2DAngularDampingIcall", reinterpret_cast<void*>(&ManagedGetRigidbody2DAngularDampingIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetRigidbody2DAngularDampingIcall", reinterpret_cast<void*>(&ManagedSetRigidbody2DAngularDampingIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetRigidbody2DLinearVelocityIcall", reinterpret_cast<void*>(&ManagedGetRigidbody2DLinearVelocityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetRigidbody2DLinearVelocityIcall", reinterpret_cast<void*>(&ManagedSetRigidbody2DLinearVelocityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetRigidbody2DLinearVelocityXIcall", reinterpret_cast<void*>(&ManagedSetRigidbody2DLinearVelocityXIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetRigidbody2DLinearVelocityYIcall", reinterpret_cast<void*>(&ManagedSetRigidbody2DLinearVelocityYIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "AddRigidbody2DLinearVelocityIcall", reinterpret_cast<void*>(&ManagedAddRigidbody2DLinearVelocityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetRigidbody2DContactCountIcall", reinterpret_cast<void*>(&ManagedGetRigidbody2DContactCountIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasContactWithEntityIcall", reinterpret_cast<void*>(&ManagedHasContactWithEntityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetContactEntityCountIcall", reinterpret_cast<void*>(&ManagedGetContactEntityCountIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetContactEntityAtIcall", reinterpret_cast<void*>(&ManagedGetContactEntityAtIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "Raycast2DIcall", reinterpret_cast<void*>(&ManagedRaycast2DIcall));
            contractAssembly.UploadInternalCalls();
        }

        struct InvocationScope final
        {
            InvocationScope(Scene* scene, uint64_t runtimeInstanceId)
            {
                s_HostState.ActiveScene = scene;
                s_HostState.ActiveRuntimeInstanceId = runtimeInstanceId;
                s_HostState.LastManagedExceptionMessage.clear();
            }

            ~InvocationScope()
            {
                s_HostState.ActiveScene = nullptr;
                s_HostState.ActiveRuntimeInstanceId = 0;
            }
        };

        bool ConsumeInvocationStatus(std::string* errorMessage)
        {
            if (!s_HostState.LastManagedExceptionMessage.empty())
            {
                if (errorMessage != nullptr)
                    *errorMessage = s_HostState.LastManagedExceptionMessage;
                return false;
            }

            if (errorMessage != nullptr)
                errorMessage->clear();
            return true;
        }

        template<typename... TArgs>
        bool InvokeRuntimeMethod(uint64_t instanceId,
                                 Scene* scene,
                                 std::string_view methodName,
                                 const char* nonStandardMessage,
                                 std::string* errorMessage,
                                 TArgs... args)
        {
            RuntimeInstance* runtimeInstance = FindMutableRuntimeInstance(instanceId);
            if (runtimeInstance == nullptr)
            {
                if (errorMessage != nullptr)
                    *errorMessage = "managed runtime instance was not found";
                return false;
            }

            InvocationScope invocationScope(scene, instanceId);
            try
            {
                runtimeInstance->Object.InvokeMethod(methodName, args...);
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
                    *errorMessage = nonStandardMessage;
                return false;
            }

            return ConsumeInvocationStatus(errorMessage);
        }

        void ClearState()
        {
            for (auto& [instanceId, runtimeInstance] : s_HostState.RuntimeInstances)
            {
                (void)instanceId;
                if (runtimeInstance.Object.IsValid())
                    runtimeInstance.Object.Destroy();
            }
            s_HostState.RuntimeInstances.clear();
            s_HostState.DiscoveredTypes.clear();
            s_HostState.ContractAssembly = nullptr;
            s_HostState.PayloadManifest = {};
            s_HostState.NextRuntimeInstanceId = 1;
            s_HostState.ActiveScene = nullptr;
            s_HostState.ActiveRuntimeInstanceId = 0;
            s_HostState.LastManagedExceptionMessage.clear();

            if (s_HostState.Context && s_HostState.Host)
            {
                s_HostState.Host->UnloadAssemblyLoadContext(*s_HostState.Context);
                s_HostState.Context.reset();
            }

            if (s_HostState.Snapshot.HostInitialized && s_HostState.Host)
                s_HostState.Host->Shutdown();

            s_HostState.Host.reset();
            CleanupLoadedManagedPayloadDirectory();

            s_HostState.Snapshot = {};
        }

        bool DiscoverFromManagedDirectory(const std::filesystem::path& managedDirectory)
        {
            ManagedScriptPayload::PayloadManifest payloadManifest{};
            std::string payloadError;
            if (!ManagedScriptPayload::ValidatePayloadDirectory(managedDirectory, &payloadManifest, &payloadError))
            {
                LT_WARN("Managed scripting: invalid managed payload at '{}': {}",
                        managedDirectory.string(),
                        payloadError.empty() ? "unknown validation error" : payloadError.c_str());
                return false;
            }

            std::filesystem::path loadedManagedDirectory;
            if (!ShadowCopyManagedPayload(managedDirectory, loadedManagedDirectory, &payloadError))
            {
                LT_WARN("Managed scripting: failed to stage managed payload from '{}': {}",
                        managedDirectory.string(),
                        payloadError.empty() ? "unknown staging error" : payloadError.c_str());
                return false;
            }

            s_HostState.PayloadManifest = payloadManifest;
            s_HostState.Snapshot.ManagedDirectory = NormalizeManagedDirectoryPath(managedDirectory);
            s_HostState.Snapshot.LoadedManagedDirectory = NormalizeManagedDirectoryPath(loadedManagedDirectory);
            s_HostState.Snapshot.PayloadApiVersion = payloadManifest.ApiVersion;

            Coral::HostSettings hostSettings{};
            hostSettings.CoralDirectory = loadedManagedDirectory.string();
            hostSettings.MessageCallback = &LogCoralMessage;
            hostSettings.MessageFilter = Coral::MessageLevel::All;
            hostSettings.ExceptionCallback = &CaptureManagedException;

            s_HostState.Host = std::make_unique<Coral::HostInstance>();
            const Coral::CoralInitStatus initStatus = s_HostState.Host->Initialize(std::move(hostSettings));
            if (initStatus != Coral::CoralInitStatus::Success)
            {
                LT_WARN("Managed scripting: failed to initialize Coral host from '{}'. status={}",
                        loadedManagedDirectory.string(),
                        static_cast<int>(initStatus));
                return false;
            }

            s_HostState.Snapshot.HostInitialized = true;
            s_HostState.Context = std::make_unique<Coral::AssemblyLoadContext>(
                s_HostState.Host->CreateAssemblyLoadContext("Limitless.ManagedScripts"));

            const std::filesystem::path contractAssemblyPath = loadedManagedDirectory / payloadManifest.ContractAssembly;
            if (!std::filesystem::exists(contractAssemblyPath))
            {
                LT_WARN("Managed scripting: managed contract assembly not found at '{}'.", contractAssemblyPath.string());
                return false;
            }

            Coral::ManagedAssembly& contractAssembly = s_HostState.Context->LoadAssembly(contractAssemblyPath.string());
            s_HostState.ContractAssembly = &contractAssembly;
            if (contractAssembly.GetLoadStatus() != Coral::AssemblyLoadStatus::Success)
            {
                LT_WARN("Managed scripting: failed to load managed contract assembly '{}'. status={}",
                        contractAssemblyPath.string(),
                        static_cast<int>(contractAssembly.GetLoadStatus()));
                return false;
            }

            RegisterInternalCalls(contractAssembly);

            Coral::Type& baseType = contractAssembly.GetLocalType(kScriptBaseTypeName);
            if (!baseType)
            {
                LT_WARN("Managed scripting: base type '{}' was not found in '{}'.",
                        kScriptBaseTypeName,
                        contractAssemblyPath.string());
                return false;
            }

            std::unordered_set<std::string> discoveredNames;
            std::error_code errorCode;
            for (const auto& entry : std::filesystem::directory_iterator(loadedManagedDirectory, errorCode))
            {
                if (errorCode)
                    break;
                if (!entry.is_regular_file())
                    continue;
                if (entry.path().extension() != ".dll")
                    continue;

                const std::string fileNameLower = ToLower(entry.path().filename().string());
                if (fileNameLower == ToLower(kCoralManagedAssemblyFileName) ||
                    fileNameLower == ToLower(payloadManifest.ContractAssembly))
                {
                    continue;
                }

                Coral::ManagedAssembly& assembly = s_HostState.Context->LoadAssembly(entry.path().string());
                if (assembly.GetLoadStatus() != Coral::AssemblyLoadStatus::Success)
                {
                    LT_TRACE("Managed scripting: skipping assembly '{}' due to load status {}.",
                             entry.path().string(),
                             static_cast<int>(assembly.GetLoadStatus()));
                    continue;
                }

                for (const Coral::Type& discoveredType : assembly.GetLocalTypes())
                {
                    Coral::Type& type = const_cast<Coral::Type&>(discoveredType);
                    if (!type || !type.IsSubclassOf(baseType))
                        continue;

                    const std::string fullName = ToUtf8(type.GetFullName());
                    if (fullName.empty() || !discoveredNames.insert(fullName).second)
                        continue;

                    DiscoveredScriptClass discovered{};
                    discovered.FullName = fullName;
                    discovered.AssemblyName = std::string(assembly.GetName());
                    discovered.AssemblyPath = entry.path();
                    discovered.ReflectedFields = ReflectManagedFields(type);
                    s_HostState.Snapshot.Classes.push_back(std::move(discovered));
                    s_HostState.DiscoveredTypes.emplace(fullName, &type);
                }
            }

            std::sort(s_HostState.Snapshot.Classes.begin(),
                      s_HostState.Snapshot.Classes.end(),
                      [](const DiscoveredScriptClass& left, const DiscoveredScriptClass& right) {
                          return left.FullName < right.FullName;
                      });

            LT_INFO("Managed scripting: initialized Coral host from source '{}' via loaded shadow '{}' and discovered {} managed script class(es).",
                    s_HostState.Snapshot.ManagedDirectory.string(),
                    s_HostState.Snapshot.LoadedManagedDirectory.string(),
                    s_HostState.Snapshot.Classes.size());
            for (const auto& discoveredClass : s_HostState.Snapshot.Classes)
            {
                LT_INFO("Managed scripting: discovered '{}' from '{}'.",
                        discoveredClass.FullName,
                        discoveredClass.AssemblyPath.filename().string());
            }
            return true;
        }
    }

    bool Initialize(const std::filesystem::path& managedDirectory)
    {
        if (managedDirectory.empty())
            return false;

        if (IsInitialized())
        {
            const std::filesystem::path requestedDirectory = NormalizeManagedDirectoryPath(managedDirectory);
            const std::filesystem::path activeDirectory = NormalizeManagedDirectoryPath(s_HostState.Snapshot.ManagedDirectory);
            if (requestedDirectory == activeDirectory)
                return true;

            LT_WARN("Managed scripting: host already initialized from '{}' and cannot be reconfigured in-process to '{}'. Continuing to use the existing managed directory.",
                    activeDirectory.string(),
                    requestedDirectory.string());
            return true;
        }

        if (!DiscoverFromManagedDirectory(managedDirectory))
        {
            Shutdown();
            return false;
        }

        return true;
    }

    void Shutdown()
    {
        ClearState();
    }

    bool IsInitialized()
    {
        return s_HostState.Snapshot.HostInitialized;
    }

    std::string ResolveDiscoveredClassName(std::string_view className)
    {
        const DiscoveredScriptClass* discoveredClass = ResolveDiscoveredClassMetadata(className);
        return discoveredClass ? discoveredClass->FullName : std::string{};
    }

    bool HasDiscoveredClass(std::string_view className)
    {
        if (className.empty())
            return false;

        return ResolveDiscoveredClassMetadata(className) != nullptr;
    }

    uint64_t CreateScriptInstance(std::string_view className, uint32_t entityHandle, std::string* errorMessage)
    {
        if (!IsInitialized())
        {
            if (errorMessage != nullptr)
                *errorMessage = "managed host is not initialized";
            return 0;
        }

        const std::string resolvedClassName = ResolveDiscoveredClassName(className);
        const auto typeIterator = s_HostState.DiscoveredTypes.find(resolvedClassName.empty() ? std::string(className) : resolvedClassName);
        if (typeIterator == s_HostState.DiscoveredTypes.end() || typeIterator->second == nullptr || !(*typeIterator->second))
        {
            if (errorMessage != nullptr)
                *errorMessage = "managed class was not discovered";
            return 0;
        }

        try
        {
            Coral::ManagedObject instance = typeIterator->second->CreateInstance();
            if (!instance.IsValid())
            {
                if (errorMessage != nullptr)
                    *errorMessage = "managed instance creation returned an invalid object";
                return 0;
            }

            instance.SetFieldValue("EntityId", entityHandle);

            RuntimeInstance runtimeInstance{};
            runtimeInstance.Id = s_HostState.NextRuntimeInstanceId++;
            runtimeInstance.ClassName = resolvedClassName.empty() ? std::string(className) : resolvedClassName;
            runtimeInstance.EntityHandle = entityHandle;
            runtimeInstance.Object = std::move(instance);
            runtimeInstance.LastSynchronizedExposedPropertiesRevision = 0;

            const uint64_t instanceId = runtimeInstance.Id;
            s_HostState.RuntimeInstances.emplace(instanceId, std::move(runtimeInstance));
            if (errorMessage != nullptr)
                errorMessage->clear();
            return instanceId;
        }
        catch (const std::exception& exception)
        {
            if (errorMessage != nullptr)
                *errorMessage = exception.what();
        }
        catch (...)
        {
            if (errorMessage != nullptr)
                *errorMessage = "non-standard exception during managed instance creation";
        }

        return 0;
    }

    bool SynchronizeScriptExposedProperties(uint64_t instanceId,
                                            Scene* scene,
                                            const std::unordered_map<std::string, ScriptPropertyValue>& exposedProperties,
                                            uint64_t revision,
                                            std::string* errorMessage)
    {
        RuntimeInstance* runtimeInstance = FindMutableRuntimeInstance(instanceId);
        if (runtimeInstance == nullptr)
        {
            if (errorMessage != nullptr)
                *errorMessage = "managed runtime instance was not found";
            return false;
        }

        if (runtimeInstance->LastSynchronizedExposedPropertiesRevision == revision)
        {
            if (errorMessage != nullptr)
                errorMessage->clear();
            return true;
        }

        const DiscoveredScriptClass* discoveredClass = FindDiscoveredClassMetadata(runtimeInstance->ClassName);
        if (discoveredClass == nullptr)
        {
            if (errorMessage != nullptr)
                *errorMessage = "managed class metadata was not found";
            return false;
        }

        for (const ReflectedFieldDefinition& fieldDefinition : discoveredClass->ReflectedFields)
        {
            const auto propertyIterator = exposedProperties.find(fieldDefinition.Name);
            const ScriptPropertyValue& propertyValue = (propertyIterator != exposedProperties.end())
                ? propertyIterator->second
                : fieldDefinition.DefaultValue;

            if (!ApplyManagedFieldValue(*runtimeInstance, scene, fieldDefinition, propertyValue, errorMessage))
            {
                if (errorMessage != nullptr && errorMessage->empty())
                    *errorMessage = "failed synchronizing managed field '" + fieldDefinition.Name + "'";
                return false;
            }
        }

        runtimeInstance->LastSynchronizedExposedPropertiesRevision = revision;
        return ConsumeInvocationStatus(errorMessage);
    }

    bool ReadBackScriptExposedProperties(uint64_t instanceId,
                                         Scene* scene,
                                         std::unordered_map<std::string, ScriptPropertyValue>& exposedProperties,
                                         uint64_t* revision,
                                         std::string* errorMessage)
    {
        RuntimeInstance* runtimeInstance = FindMutableRuntimeInstance(instanceId);
        if (runtimeInstance == nullptr)
        {
            if (errorMessage != nullptr)
                *errorMessage = "managed runtime instance was not found";
            return false;
        }

        const DiscoveredScriptClass* discoveredClass = FindDiscoveredClassMetadata(runtimeInstance->ClassName);
        if (discoveredClass == nullptr)
        {
            if (errorMessage != nullptr)
                *errorMessage = "managed class metadata was not found";
            return false;
        }

        bool changed = false;
        for (const ReflectedFieldDefinition& fieldDefinition : discoveredClass->ReflectedFields)
        {
            ScriptPropertyValue propertyValue = fieldDefinition.DefaultValue;
            if (!TryReadRuntimeFieldValue(*runtimeInstance, scene, fieldDefinition, propertyValue, errorMessage))
            {
                if (errorMessage != nullptr && errorMessage->empty())
                    *errorMessage = "failed reading managed field '" + fieldDefinition.Name + "'";
                return false;
            }

            const auto propertyIterator = exposedProperties.find(fieldDefinition.Name);
            if (propertyIterator == exposedProperties.end())
            {
                exposedProperties.emplace(fieldDefinition.Name, std::move(propertyValue));
                changed = true;
                continue;
            }

            if (ScriptPropertyValuesEqual(propertyIterator->second, propertyValue))
                continue;

            propertyIterator->second = std::move(propertyValue);
            changed = true;
        }

        if (!ConsumeInvocationStatus(errorMessage))
            return false;

        if (changed)
        {
            if (revision != nullptr)
                ++(*revision);

            runtimeInstance->LastSynchronizedExposedPropertiesRevision = revision != nullptr
                ? *revision
                : runtimeInstance->LastSynchronizedExposedPropertiesRevision + 1;
        }

        if (errorMessage != nullptr)
            errorMessage->clear();
        return true;
    }

    bool InvokeScriptOnCreate(uint64_t instanceId, Scene* scene, std::string* errorMessage)
    {
        return InvokeRuntimeMethod(instanceId, scene, "DispatchCreateInternal", "non-standard exception during OnCreate", errorMessage);
    }

    bool InvokeScriptOnFixedUpdate(uint64_t instanceId, Scene* scene, float fixedDeltaTime, std::string* errorMessage)
    {
        return InvokeRuntimeMethod(instanceId, scene, "DispatchFixedUpdateInternal", "non-standard exception during OnFixedUpdate", errorMessage, fixedDeltaTime);
    }

    bool InvokeScriptOnUpdate(uint64_t instanceId, Scene* scene, float deltaTime, std::string* errorMessage)
    {
        return InvokeRuntimeMethod(instanceId, scene, "DispatchUpdateInternal", "non-standard exception during OnUpdate", errorMessage, deltaTime);
    }

    bool InvokeScriptOnCollisionEnter(uint64_t instanceId, Scene* scene, uint32_t otherEntityHandle, std::string* errorMessage)
    {
        return InvokeRuntimeMethod(instanceId, scene, "DispatchCollisionEnterInternal", "non-standard exception during OnCollisionEnter", errorMessage, otherEntityHandle);
    }

    bool InvokeScriptOnCollisionStay(uint64_t instanceId, Scene* scene, uint32_t otherEntityHandle, std::string* errorMessage)
    {
        return InvokeRuntimeMethod(instanceId, scene, "DispatchCollisionStayInternal", "non-standard exception during OnCollisionStay", errorMessage, otherEntityHandle);
    }

    bool InvokeScriptOnCollisionExit(uint64_t instanceId, Scene* scene, uint32_t otherEntityHandle, std::string* errorMessage)
    {
        return InvokeRuntimeMethod(instanceId, scene, "DispatchCollisionExitInternal", "non-standard exception during OnCollisionExit", errorMessage, otherEntityHandle);
    }

    bool InvokeScriptOnTriggerEnter(uint64_t instanceId, Scene* scene, uint32_t otherEntityHandle, std::string* errorMessage)
    {
        return InvokeRuntimeMethod(instanceId, scene, "DispatchTriggerEnterInternal", "non-standard exception during OnTriggerEnter", errorMessage, otherEntityHandle);
    }

    bool InvokeScriptOnTriggerStay(uint64_t instanceId, Scene* scene, uint32_t otherEntityHandle, std::string* errorMessage)
    {
        return InvokeRuntimeMethod(instanceId, scene, "DispatchTriggerStayInternal", "non-standard exception during OnTriggerStay", errorMessage, otherEntityHandle);
    }

    bool InvokeScriptOnTriggerExit(uint64_t instanceId, Scene* scene, uint32_t otherEntityHandle, std::string* errorMessage)
    {
        return InvokeRuntimeMethod(instanceId, scene, "DispatchTriggerExitInternal", "non-standard exception during OnTriggerExit", errorMessage, otherEntityHandle);
    }

    bool InvokeScriptOnDestroy(uint64_t instanceId, Scene* scene, std::string* errorMessage)
    {
        return InvokeRuntimeMethod(instanceId, scene, "DispatchDestroyInternal", "non-standard exception during OnDestroy", errorMessage);
    }

    void DestroyScriptInstance(uint64_t instanceId)
    {
        if (RuntimeInstance* runtimeInstance = FindMutableRuntimeInstance(instanceId))
        {
            if (runtimeInstance->Object.IsValid())
                runtimeInstance->Object.Destroy();
        }
        s_HostState.RuntimeInstances.erase(instanceId);
    }

    const DiscoverySnapshot& GetSnapshot()
    {
        return s_HostState.Snapshot;
    }
}
