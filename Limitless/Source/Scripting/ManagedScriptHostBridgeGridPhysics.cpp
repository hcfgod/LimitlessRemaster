#include "Scripting/ManagedScriptHostInternal.h"

#include <algorithm>

namespace Limitless::ManagedScriptHost
{
    using namespace Internal;

    namespace Internal
    {
        LT_MANAGED_COMPONENT_HAS(HasGrid2DComponentIcall, TryGetManagedGrid2DComponent);
        LT_MANAGED_COMPONENT_GET(GetGrid2DCellSizeIcall, ManagedVector2, TryGetManagedGrid2DComponent, ToManagedVector2(component->CellSize), ManagedVector2{ 1.0f, 1.0f });

        void ManagedSetGrid2DCellSizeIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* grid2D = TryGetManagedGrid2DComponent(entityHandle))
            {
                const glm::vec2 cellSize = ToGlmVector2(value);
                grid2D->CellSize = glm::vec2(std::max(0.001f, cellSize.x), std::max(0.001f, cellSize.y));
            }
        }

        LT_MANAGED_COMPONENT_GET(GetGrid2DCellGapIcall, ManagedVector2, TryGetManagedGrid2DComponent, ToManagedVector2(component->CellGap), ManagedVector2{});
        LT_MANAGED_COMPONENT_SET(SetGrid2DCellGapIcall, ManagedVector2, TryGetManagedGrid2DComponent, component->CellGap = ToGlmVector2(value););

        LT_MANAGED_COMPONENT_HAS(HasTilemapLayerComponentIcall, TryGetManagedTilemapLayerComponent);

        Grid2DComponent* TryGetManagedTilemapLayerGridComponent(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            if (registry == nullptr || s_HostState.ActiveScene == nullptr)
                return nullptr;

            const entt::entity layerEntity = ResolveManagedEntityHandle(entityHandle);
            if (layerEntity == entt::null)
                return nullptr;

            const auto* hierarchy = registry->try_get<HierarchyComponent>(layerEntity);
            if (!hierarchy || hierarchy->Parent == entt::null || !registry->valid(hierarchy->Parent))
                return nullptr;

            return registry->try_get<Grid2DComponent>(hierarchy->Parent);
        }

        int ManagedGetTilemapLayerGridWidthIcall(uint32_t entityHandle)
        {
            if (!TryGetManagedTilemapLayerComponent(entityHandle))
                return 0;

            const auto* grid = TryGetManagedTilemapLayerGridComponent(entityHandle);
            return grid ? std::max(1, grid->GridSize.x) : 0;
        }

        int ManagedGetTilemapLayerGridHeightIcall(uint32_t entityHandle)
        {
            if (!TryGetManagedTilemapLayerComponent(entityHandle))
                return 0;

            const auto* grid = TryGetManagedTilemapLayerGridComponent(entityHandle);
            return grid ? std::max(1, grid->GridSize.y) : 0;
        }

        void ManagedResizeTilemapLayerGridIcall(uint32_t entityHandle, int width, int height)
        {
            entt::registry* registry = GetActiveRegistry();
            auto* layer = TryGetManagedTilemapLayerComponent(entityHandle);
            auto* grid = TryGetManagedTilemapLayerGridComponent(entityHandle);
            if (layer == nullptr || registry == nullptr || s_HostState.ActiveScene == nullptr || grid == nullptr)
                return;

            const entt::entity layerEntity = ResolveManagedEntityHandle(entityHandle);
            const auto* hierarchy = registry->try_get<HierarchyComponent>(layerEntity);
            if (!hierarchy || hierarchy->Parent == entt::null)
                return;

            const glm::ivec2 previousGridSize = grid->GridSize;
            const glm::ivec2 requestedGridSize = GetClampedGrid2DLayoutSize(glm::ivec2(std::max(1, width), std::max(1, height)));
            const auto children = s_HostState.ActiveScene->GetChildren(hierarchy->Parent);
            for (entt::entity child : children)
            {
                auto* childLayer = registry->try_get<TilemapLayerComponent>(child);
                if (!childLayer)
                    continue;
                ResizeTilemapLayerStorage(*childLayer, previousGridSize, requestedGridSize, glm::ivec2(0));
            }
            grid->GridSize = requestedGridSize;
            for (entt::entity child : children)
            {
                auto* childLayer = registry->try_get<TilemapLayerComponent>(child);
                if (!childLayer)
                    continue;
                EnsureTilemapLayerStorage(*grid, *childLayer);
            }
        }

        LT_MANAGED_COMPONENT_GET(GetTilemapLayerRenderOrderIcall, int, TryGetManagedTilemapLayerComponent, component->RenderOrder, 0);
        LT_MANAGED_COMPONENT_SET(SetTilemapLayerRenderOrderIcall, int, TryGetManagedTilemapLayerComponent, component->RenderOrder = value;);
        LT_MANAGED_COMPONENT_GET(GetTilemapLayerCollisionEnabledIcall, bool, TryGetManagedTilemapLayerComponent, component->CollisionEnabled, false);
        LT_MANAGED_COMPONENT_SET(SetTilemapLayerCollisionEnabledIcall, bool, TryGetManagedTilemapLayerComponent, component->CollisionEnabled = value;);
        LT_MANAGED_COMPONENT_GET(GetTilemapLayerCastShadowsIcall, bool, TryGetManagedTilemapLayerComponent, component->CastShadows, false);
        LT_MANAGED_COMPONENT_SET(SetTilemapLayerCastShadowsIcall, bool, TryGetManagedTilemapLayerComponent, component->CastShadows = value;);

        int ManagedGetTilemapLayerCellCountIcall(uint32_t entityHandle)
        {
            if (!TryGetManagedTilemapLayerComponent(entityHandle))
                return 0;

            const auto* grid = TryGetManagedTilemapLayerGridComponent(entityHandle);
            return grid ? GetTilemapCellCount(*grid) : 0;
        }

        bool ManagedIsTilemapLayerCellInBoundsIcall(uint32_t entityHandle, int cellX, int cellY)
        {
            if (!TryGetManagedTilemapLayerComponent(entityHandle))
                return false;

            const auto* grid = TryGetManagedTilemapLayerGridComponent(entityHandle);
            return grid ? IsGrid2DCellInBounds(*grid, cellX, cellY) : false;
        }

        int ManagedGetTilemapLayerTileIdIcall(uint32_t entityHandle, int cellX, int cellY)
        {
            auto* layer = TryGetManagedTilemapLayerComponent(entityHandle);
            auto* grid = TryGetManagedTilemapLayerGridComponent(entityHandle);
            if (layer == nullptr || grid == nullptr || !IsGrid2DCellInBounds(*grid, cellX, cellY))
                return 0;

            EnsureTilemapLayerStorage(*grid, *layer);
            const size_t index = Grid2DCellToIndex(*grid, cellX, cellY);
            return index < layer->Tiles.size() ? static_cast<int>(layer->Tiles[index]) : 0;
        }

        void ManagedSetTilemapLayerTileIdIcall(uint32_t entityHandle, int cellX, int cellY, int tileId)
        {
            auto* layer = TryGetManagedTilemapLayerComponent(entityHandle);
            auto* grid = TryGetManagedTilemapLayerGridComponent(entityHandle);
            if (layer == nullptr || grid == nullptr || !IsGrid2DCellInBounds(*grid, cellX, cellY))
                return;

            EnsureTilemapLayerStorage(*grid, *layer);
            const size_t index = Grid2DCellToIndex(*grid, cellX, cellY);
            const uint32_t resolvedTileId = (tileId > 0 && static_cast<size_t>(tileId) < layer->TileTable.size())
                ? static_cast<uint32_t>(tileId)
                : 0u;
            if (index < layer->Tiles.size())
                layer->Tiles[index] = resolvedTileId;
        }

        Coral::String ManagedGetTilemapLayerTileAssetKeyIcall(uint32_t entityHandle, int cellX, int cellY)
        {
            auto* layer = TryGetManagedTilemapLayerComponent(entityHandle);
            auto* grid = TryGetManagedTilemapLayerGridComponent(entityHandle);
            if (layer == nullptr || grid == nullptr || !IsGrid2DCellInBounds(*grid, cellX, cellY))
                return Coral::String::New("");

            EnsureTilemapLayerStorage(*grid, *layer);
            const size_t index = Grid2DCellToIndex(*grid, cellX, cellY);
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
            auto* grid = TryGetManagedTilemapLayerGridComponent(entityHandle);
            if (layer == nullptr || grid == nullptr || !IsGrid2DCellInBounds(*grid, cellX, cellY))
                return;

            EnsureTilemapLayerStorage(*grid, *layer);
            const size_t index = Grid2DCellToIndex(*grid, cellX, cellY);
            const std::string tileAssetKey = ToUtf8Borrowed(value);
            const uint32_t tileId = layer->GetOrAddTileTableEntry(tileAssetKey);
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

            if (auto* grid = TryGetManagedTilemapLayerGridComponent(entityHandle))
                EnsureTilemapLayerStorage(*grid, *layer);

            const std::string tileAssetKey = ToUtf8Borrowed(value);
            if (tileAssetKey.empty())
            {
                layer->TileTable[static_cast<size_t>(index)].clear();
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

        LT_MANAGED_COMPONENT_HAS(HasRigidbody2DComponentIcall, TryGetManagedRigidbody2DComponent);
        LT_MANAGED_COMPONENT_GET(GetRigidbody2DBodyTypeIcall, int, TryGetManagedRigidbody2DComponent, static_cast<int>(component->Type), static_cast<int>(Rigidbody2DComponent::BodyType::Static));

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

        LT_MANAGED_COMPONENT_GET(GetRigidbody2DFreezePositionXIcall, bool, TryGetManagedRigidbody2DComponent, component->FreezePositionX, false);
        LT_MANAGED_COMPONENT_SET(SetRigidbody2DFreezePositionXIcall, bool, TryGetManagedRigidbody2DComponent, component->FreezePositionX = value;);
        LT_MANAGED_COMPONENT_GET(GetRigidbody2DFreezePositionYIcall, bool, TryGetManagedRigidbody2DComponent, component->FreezePositionY, false);
        LT_MANAGED_COMPONENT_SET(SetRigidbody2DFreezePositionYIcall, bool, TryGetManagedRigidbody2DComponent, component->FreezePositionY = value;);
        LT_MANAGED_COMPONENT_GET(GetRigidbody2DFixedRotationIcall, bool, TryGetManagedRigidbody2DComponent, component->FixedRotation, false);
        LT_MANAGED_COMPONENT_SET(SetRigidbody2DFixedRotationIcall, bool, TryGetManagedRigidbody2DComponent, component->FixedRotation = value;);
        LT_MANAGED_COMPONENT_GET(GetRigidbody2DUseCCDIcall, bool, TryGetManagedRigidbody2DComponent, component->UseCCD, false);
        LT_MANAGED_COMPONENT_SET(SetRigidbody2DUseCCDIcall, bool, TryGetManagedRigidbody2DComponent, component->UseCCD = value;);
        LT_MANAGED_COMPONENT_GET(GetRigidbody2DEnableSleepIcall, bool, TryGetManagedRigidbody2DComponent, component->EnableSleep, false);
        LT_MANAGED_COMPONENT_SET(SetRigidbody2DEnableSleepIcall, bool, TryGetManagedRigidbody2DComponent, component->EnableSleep = value;);
        LT_MANAGED_COMPONENT_GET(GetRigidbody2DStartAwakeIcall, bool, TryGetManagedRigidbody2DComponent, component->StartAwake, false);
        LT_MANAGED_COMPONENT_SET(SetRigidbody2DStartAwakeIcall, bool, TryGetManagedRigidbody2DComponent, component->StartAwake = value;);
        LT_MANAGED_COMPONENT_GET(GetRigidbody2DInterpolateIcall, bool, TryGetManagedRigidbody2DComponent, component->Interpolate, false);
        LT_MANAGED_COMPONENT_SET(SetRigidbody2DInterpolateIcall, bool, TryGetManagedRigidbody2DComponent, component->Interpolate = value;);
        LT_MANAGED_COMPONENT_GET(GetRigidbody2DHighContactQualityIcall, bool, TryGetManagedRigidbody2DComponent, component->HighContactQuality, false);
        LT_MANAGED_COMPONENT_SET(SetRigidbody2DHighContactQualityIcall, bool, TryGetManagedRigidbody2DComponent, component->HighContactQuality = value;);
        LT_MANAGED_COMPONENT_GET(GetRigidbody2DExtraSolverSubStepsIcall, int, TryGetManagedRigidbody2DComponent, component->ExtraSolverSubSteps, 0);
        LT_MANAGED_COMPONENT_SET(SetRigidbody2DExtraSolverSubStepsIcall, int, TryGetManagedRigidbody2DComponent, component->ExtraSolverSubSteps = std::max(0, value););

        float ManagedGetRigidbody2DGravityScaleIcall(uint32_t entityHandle)
        {
            const auto* rigidbody = TryGetManagedRigidbody2DComponent(entityHandle);
            return rigidbody ? rigidbody->GravityScale : 1.0f;
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
            RegisterInternalCallBatch(contractAssembly, {
                LT_MANAGED_INTERNAL_CALL(HasGrid2DComponentIcall),
                LT_MANAGED_INTERNAL_CALL(GetGrid2DCellSizeIcall),
                LT_MANAGED_INTERNAL_CALL(SetGrid2DCellSizeIcall),
                LT_MANAGED_INTERNAL_CALL(GetGrid2DCellGapIcall),
                LT_MANAGED_INTERNAL_CALL(SetGrid2DCellGapIcall),
                LT_MANAGED_INTERNAL_CALL(HasTilemapLayerComponentIcall),
                LT_MANAGED_INTERNAL_CALL(GetTilemapLayerGridWidthIcall),
                LT_MANAGED_INTERNAL_CALL(GetTilemapLayerGridHeightIcall),
                LT_MANAGED_INTERNAL_CALL(ResizeTilemapLayerGridIcall),
                LT_MANAGED_INTERNAL_CALL(GetTilemapLayerRenderOrderIcall),
                LT_MANAGED_INTERNAL_CALL(SetTilemapLayerRenderOrderIcall),
                LT_MANAGED_INTERNAL_CALL(GetTilemapLayerCollisionEnabledIcall),
                LT_MANAGED_INTERNAL_CALL(SetTilemapLayerCollisionEnabledIcall),
                LT_MANAGED_INTERNAL_CALL(GetTilemapLayerCastShadowsIcall),
                LT_MANAGED_INTERNAL_CALL(SetTilemapLayerCastShadowsIcall),
                LT_MANAGED_INTERNAL_CALL(GetTilemapLayerCellCountIcall),
                LT_MANAGED_INTERNAL_CALL(IsTilemapLayerCellInBoundsIcall),
                LT_MANAGED_INTERNAL_CALL(GetTilemapLayerTileIdIcall),
                LT_MANAGED_INTERNAL_CALL(SetTilemapLayerTileIdIcall),
                LT_MANAGED_INTERNAL_CALL(GetTilemapLayerTileAssetKeyIcall),
                LT_MANAGED_INTERNAL_CALL(SetTilemapLayerTileAssetKeyIcall),
                LT_MANAGED_INTERNAL_CALL(GetTilemapLayerTileTableEntryCountIcall),
                LT_MANAGED_INTERNAL_CALL(GetTilemapLayerTileTableEntryIcall),
                LT_MANAGED_INTERNAL_CALL(SetTilemapLayerTileTableEntryIcall),
                LT_MANAGED_INTERNAL_CALL(GetOrAddTilemapLayerTileTableEntryIcall),
                LT_MANAGED_INTERNAL_CALL(HasRigidbody2DComponentIcall),
                LT_MANAGED_INTERNAL_CALL(GetRigidbody2DBodyTypeIcall),
                LT_MANAGED_INTERNAL_CALL(SetRigidbody2DBodyTypeIcall),
                LT_MANAGED_INTERNAL_CALL(GetRigidbody2DFreezePositionXIcall),
                LT_MANAGED_INTERNAL_CALL(SetRigidbody2DFreezePositionXIcall),
                LT_MANAGED_INTERNAL_CALL(GetRigidbody2DFreezePositionYIcall),
                LT_MANAGED_INTERNAL_CALL(SetRigidbody2DFreezePositionYIcall),
                LT_MANAGED_INTERNAL_CALL(GetRigidbody2DFixedRotationIcall),
                LT_MANAGED_INTERNAL_CALL(SetRigidbody2DFixedRotationIcall),
                LT_MANAGED_INTERNAL_CALL(GetRigidbody2DUseCCDIcall),
                LT_MANAGED_INTERNAL_CALL(SetRigidbody2DUseCCDIcall),
                LT_MANAGED_INTERNAL_CALL(GetRigidbody2DEnableSleepIcall),
                LT_MANAGED_INTERNAL_CALL(SetRigidbody2DEnableSleepIcall),
                LT_MANAGED_INTERNAL_CALL(GetRigidbody2DStartAwakeIcall),
                LT_MANAGED_INTERNAL_CALL(SetRigidbody2DStartAwakeIcall),
                LT_MANAGED_INTERNAL_CALL(GetRigidbody2DInterpolateIcall),
                LT_MANAGED_INTERNAL_CALL(SetRigidbody2DInterpolateIcall),
                LT_MANAGED_INTERNAL_CALL(GetRigidbody2DHighContactQualityIcall),
                LT_MANAGED_INTERNAL_CALL(SetRigidbody2DHighContactQualityIcall),
                LT_MANAGED_INTERNAL_CALL(GetRigidbody2DExtraSolverSubStepsIcall),
                LT_MANAGED_INTERNAL_CALL(SetRigidbody2DExtraSolverSubStepsIcall),
                LT_MANAGED_INTERNAL_CALL(GetRigidbody2DGravityScaleIcall),
                LT_MANAGED_INTERNAL_CALL(SetRigidbody2DGravityScaleIcall),
                LT_MANAGED_INTERNAL_CALL(GetRigidbody2DLinearDampingIcall),
                LT_MANAGED_INTERNAL_CALL(SetRigidbody2DLinearDampingIcall),
                LT_MANAGED_INTERNAL_CALL(GetRigidbody2DAngularDampingIcall),
                LT_MANAGED_INTERNAL_CALL(SetRigidbody2DAngularDampingIcall),
                LT_MANAGED_INTERNAL_CALL(GetRigidbody2DLinearVelocityIcall),
                LT_MANAGED_INTERNAL_CALL(SetRigidbody2DLinearVelocityIcall),
                LT_MANAGED_INTERNAL_CALL(SetRigidbody2DLinearVelocityXIcall),
                LT_MANAGED_INTERNAL_CALL(SetRigidbody2DLinearVelocityYIcall),
                LT_MANAGED_INTERNAL_CALL(AddRigidbody2DLinearVelocityIcall),
                LT_MANAGED_INTERNAL_CALL(GetRigidbody2DContactCountIcall),
                LT_MANAGED_INTERNAL_CALL(HasContactWithEntityIcall),
                LT_MANAGED_INTERNAL_CALL(GetContactEntityCountIcall),
                LT_MANAGED_INTERNAL_CALL(GetContactEntityAtIcall),
                LT_MANAGED_INTERNAL_CALL(Raycast2DIcall)
            });
        }
    }
}
