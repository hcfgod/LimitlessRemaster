#include "Scripting/ManagedScriptHost.h"

#include "Core/Debug/Log.h"
#include "Scene/Components/CoreComponents.h"
#include "Scene/Components/PhysicsComponents.h"
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

        bool ManagedEntityExistsIcall(uint32_t entityHandle)
        {
            if (s_HostState.ActiveScene == nullptr)
                return false;

            return s_HostState.ActiveScene->IsValid(static_cast<entt::entity>(entityHandle));
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
            return Coral::String::New(tagComponent ? tagComponent->Tag : std::string_view());
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
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "EntityExistsIcall", reinterpret_cast<void*>(&ManagedEntityExistsIcall));
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

    bool InvokeScriptOnCreate(uint64_t instanceId, Scene* scene, std::string* errorMessage)
    {
        return InvokeRuntimeMethod(instanceId, scene, "OnCreate", "non-standard exception during OnCreate", errorMessage);
    }

    bool InvokeScriptOnFixedUpdate(uint64_t instanceId, Scene* scene, float fixedDeltaTime, std::string* errorMessage)
    {
        return InvokeRuntimeMethod(instanceId, scene, "OnFixedUpdate", "non-standard exception during OnFixedUpdate", errorMessage, fixedDeltaTime);
    }

    bool InvokeScriptOnUpdate(uint64_t instanceId, Scene* scene, float deltaTime, std::string* errorMessage)
    {
        return InvokeRuntimeMethod(instanceId, scene, "OnUpdate", "non-standard exception during OnUpdate", errorMessage, deltaTime);
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
        return InvokeRuntimeMethod(instanceId, scene, "OnDestroy", "non-standard exception during OnDestroy", errorMessage);
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
