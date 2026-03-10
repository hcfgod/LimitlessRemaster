#include "Scripting/ManagedScriptHostInternal.h"

#include <algorithm>

namespace Limitless::ManagedScriptHost
{
    using namespace Internal;

    namespace Internal
    {
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

        void RegisterGridPhysicsInternalCalls(Coral::ManagedAssembly& contractAssembly)
        {
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
        }
    }
}
