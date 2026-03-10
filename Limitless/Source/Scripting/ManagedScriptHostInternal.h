#pragma once

#include "Scripting/ManagedScriptHost.h"
#include "Physics/Physics2DQueries.h"
#include "Scene/Components/AudioComponents.h"
#include "Scene/Components/CoreComponents.h"
#include "Scene/Components/PhysicsComponents.h"
#include "Scene/Components/RenderingComponents.h"
#include "Scene/Components/TilemapComponents.h"
#include "Scene/Scene.h"

#include <Coral/Assembly.hpp>
#include <Coral/HostInstance.hpp>
#include <Coral/MessageLevel.hpp>
#include <Coral/String.hpp>
#include <Coral/Type.hpp>

#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Limitless::ManagedScriptHost::Internal
{
    inline constexpr const char* kCoralManagedAssemblyFileName = "Coral.Managed.dll";
    inline constexpr const char* kContractAssemblyFileName = "Limitless.Managed.dll";
    inline constexpr const char* kScriptBaseTypeName = "Limitless.Managed.ScriptableEntity";
    inline constexpr const char* kScriptBridgeTypeName = "Limitless.Managed.ScriptBridge";
    inline constexpr const char* kManagedVector3TypeName = "Limitless.Managed.Vector3";
    inline constexpr const char* kManagedEntityTypeName = "Limitless.Managed.Entity";
    inline constexpr const char* kSystemSingleTypeName = "System.Single";
    inline constexpr const char* kSystemInt32TypeName = "System.Int32";
    inline constexpr const char* kSystemBooleanTypeName = "System.Boolean";
    inline constexpr const char* kSystemStringTypeName = "System.String";

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

    using InternalCallRegistrar = void (*)(Coral::ManagedAssembly& contractAssembly);

    struct InternalCallBinding final
    {
        const char* MethodName = nullptr;
        void* Function = nullptr;
    };

    extern HostState s_HostState;

    std::string ToLower(std::string value);
    std::string ToUtf8(Coral::String value);
    std::string ToUtf8Borrowed(Coral::String value);
    std::filesystem::path NormalizeManagedDirectoryPath(const std::filesystem::path& path);
    bool ShadowCopyManagedPayload(const std::filesystem::path& sourceDirectory,
                                  std::filesystem::path& outLoadedDirectory,
                                  std::string* errorMessage);
    void CleanupLoadedManagedPayloadDirectory();
    entt::registry* GetActiveRegistry();
    entt::entity ResolveManagedEntityHandle(uint32_t entityHandle);
    ManagedVector3 ToManagedVector3(const glm::vec3& value);
    glm::vec3 ToGlmVector3(const ManagedVector3& value);
    ManagedVector2 ToManagedVector2(const glm::vec2& value);
    glm::vec2 ToGlmVector2(const ManagedVector2& value);
    ManagedVector4 ToManagedVector4(const glm::vec4& value);
    glm::vec4 ToGlmVector4(const ManagedVector4& value);
    ManagedRaycastHit2D ToManagedRaycastHit2D(const Physics2DRaycastHit& value);
    Rigidbody2DComponent* TryGetManagedRigidbody2DComponent(uint32_t entityHandle);
    BoxCollider2DComponent* TryGetManagedBoxCollider2DComponent(uint32_t entityHandle);
    CircleCollider2DComponent* TryGetManagedCircleCollider2DComponent(uint32_t entityHandle);
    PolygonCollider2DComponent* TryGetManagedPolygonCollider2DComponent(uint32_t entityHandle);
    EdgeCollider2DComponent* TryGetManagedEdgeCollider2DComponent(uint32_t entityHandle);
    CapsuleCollider2DComponent* TryGetManagedCapsuleCollider2DComponent(uint32_t entityHandle);
    Joint2DComponent* TryGetManagedJoint2DComponent(uint32_t entityHandle);
    SpriteComponent* TryGetManagedSpriteComponent(uint32_t entityHandle);
    MaterialComponent* TryGetManagedMaterialComponent(uint32_t entityHandle);
    CanvasComponent* TryGetManagedCanvasComponent(uint32_t entityHandle);
    RectTransformComponent* TryGetManagedRectTransformComponent(uint32_t entityHandle);
    DirectionalLight2DComponent* TryGetManagedDirectionalLight2DComponent(uint32_t entityHandle);
    PointLight2DComponent* TryGetManagedPointLight2DComponent(uint32_t entityHandle);
    UIImageComponent* TryGetManagedUIImageComponent(uint32_t entityHandle);
    UIPanelComponent* TryGetManagedUIPanelComponent(uint32_t entityHandle);
    UITextComponent* TryGetManagedUITextComponent(uint32_t entityHandle);
    UIButtonComponent* TryGetManagedUIButtonComponent(uint32_t entityHandle);
    UISliderComponent* TryGetManagedUISliderComponent(uint32_t entityHandle);
    AudioListener2DComponent* TryGetManagedAudioListener2DComponent(uint32_t entityHandle);
    AudioListener3DComponent* TryGetManagedAudioListener3DComponent(uint32_t entityHandle);
    AudioSourceComponent* TryGetManagedAudioSourceComponent(uint32_t entityHandle);
    AnimatorComponent* TryGetManagedAnimatorComponent(uint32_t entityHandle);
    AnimationEventReceiverComponent* TryGetManagedAnimationEventReceiverComponent(uint32_t entityHandle);
    ParticleEmitterComponent* TryGetManagedParticleEmitterComponent(uint32_t entityHandle);
    Grid2DComponent* TryGetManagedGrid2DComponent(uint32_t entityHandle);
    TilemapLayerComponent* TryGetManagedTilemapLayerComponent(uint32_t entityHandle);
    bool ScriptPropertyValuesEqual(const ScriptPropertyValue& left, const ScriptPropertyValue& right);
    bool TryReadRuntimeFieldValue(const RuntimeInstance& runtimeInstance,
                                  Scene* scene,
                                  const ReflectedFieldDefinition& fieldDefinition,
                                  ScriptPropertyValue& outValue,
                                  std::string* errorMessage);
    std::vector<ReflectedFieldDefinition> ReflectManagedFields(Coral::Type& type);
    const DiscoveredScriptClass* ResolveDiscoveredClassMetadata(std::string_view className);
    const DiscoveredScriptClass* FindDiscoveredClassMetadata(std::string_view className);
    bool ApplyManagedFieldValue(RuntimeInstance& runtimeInstance,
                                Scene* scene,
                                const ReflectedFieldDefinition& fieldDefinition,
                                const ScriptPropertyValue& propertyValue,
                                std::string* errorMessage);
    RuntimeInstance* FindMutableRuntimeInstance(uint64_t instanceId);
    std::string BuildManagedLogPrefix();
    void LogCoralMessage(std::string_view message, Coral::MessageLevel level);
    void CaptureManagedException(std::string_view message);
    void RegisterInternalCallBatch(Coral::ManagedAssembly& contractAssembly,
                                   std::initializer_list<InternalCallBinding> bindings);
    void RegisterScenePhysicsInternalCalls(Coral::ManagedAssembly& contractAssembly);
    void RegisterRenderingUiInternalCalls(Coral::ManagedAssembly& contractAssembly);
    void RegisterAudioAnimationInternalCalls(Coral::ManagedAssembly& contractAssembly);
    void RegisterGridPhysicsInternalCalls(Coral::ManagedAssembly& contractAssembly);
    void RegisterInternalCalls(Coral::ManagedAssembly& contractAssembly);
}

#define LT_MANAGED_INTERNAL_CALL(methodName) ::Limitless::ManagedScriptHost::Internal::InternalCallBinding{ #methodName, reinterpret_cast<void*>(&Managed##methodName) }

#define LT_MANAGED_COMPONENT_HAS(methodName, accessor) \
    bool Managed##methodName(uint32_t entityHandle) \
    { \
        return accessor(entityHandle) != nullptr; \
    }

#define LT_MANAGED_COMPONENT_GET(methodName, returnType, accessor, valueExpression, ...) \
    returnType Managed##methodName(uint32_t entityHandle) \
    { \
        const auto* component = accessor(entityHandle); \
        return component ? (valueExpression) : __VA_ARGS__; \
    }

#define LT_MANAGED_COMPONENT_SET(methodName, valueType, accessor, ...) \
    void Managed##methodName(uint32_t entityHandle, valueType value) \
    { \
        if (auto* component = accessor(entityHandle)) \
        { \
            __VA_ARGS__ \
        } \
    }
