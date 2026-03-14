#include "EditorInspectorPanelEntityComponentsShared.h"

#include <algorithm>

namespace Limitless::EditorInspectorPanel::Internal
{
    void DrawGridComponentSections(StandardEntityInspectorContext& context)
    {
        Scene* scene = context.SceneContext;
        auto& registry = context.Registry;
        const entt::entity selectedEntity = context.SelectedEntity;
        PendingEntityComponentRemovals& pendingRemovals = context.PendingRemovals;
        EditorUndoService* undoService = context.UndoService;
        const std::string_view onlySectionKey = context.OnlySectionKey;
        const std::vector<std::string>* orderedSectionKeys = context.OrderedSectionKeys;

        if (ShouldDrawInspectorSection(onlySectionKey, "Grid2D") && (registry.try_get<Grid2DComponent>(selectedEntity) != nullptr))
        {
            auto* grid2D = registry.try_get<Grid2DComponent>(selectedEntity);
            const bool grid2DOpen = BeginInspectorSectionHeader("Grid 2D", "Grid2DComponentOptions", "...##Grid2DComponentOptionsButton");
            if (orderedSectionKeys)
                (void)HandleSectionDragDrop("Grid2D", *orderedSectionKeys, "Grid 2D");

            if (ImGui::BeginPopup("Grid2DComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveGrid2DComponent = true;
                ImGui::EndPopup();
            }

            if (grid2DOpen)
            {
                ImGui::TextUnformatted("Cell Size");
                EditorPanelStyle::AxisVectorDragState cellSizeInteractionState{};
                EditorPanelStyle::DragFloatNWithAxisLabels("##Grid2DCellSize", &grid2D->CellSize.x, 2, 0.05f, 0.001f, 100.0f, "%.3f", 0, &cellSizeInteractionState);
                TrackInteractiveVectorMemberMutation<Grid2DComponent>(
                    undoService, "Edit Grid2D Cell Size", cellSizeInteractionState, selectedEntity, &Grid2DComponent::CellSize, grid2D->CellSize);
                ImGui::TextUnformatted("Cell Gap");
                EditorPanelStyle::AxisVectorDragState cellGapInteractionState{};
                EditorPanelStyle::DragFloatNWithAxisLabels("##Grid2DCellGap", &grid2D->CellGap.x, 2, 0.01f, 0.0f, 10.0f, "%.3f", 0, &cellGapInteractionState);
                TrackInteractiveVectorMemberMutation<Grid2DComponent>(
                    undoService, "Edit Grid2D Cell Gap", cellGapInteractionState, selectedEntity, &Grid2DComponent::CellGap, grid2D->CellGap);

                ImGui::Separator();
                if (ImGui::Button("Add Layer"))
                {
                    if (scene && undoService)
                    {
                        (void)undoService->ExecuteSceneMutation("Add Tilemap Layer", [&](Scene& mutableScene) {
                            const auto children = mutableScene.GetChildren(selectedEntity);
                            const int32_t layerNumber = static_cast<int32_t>(children.size()) + 1;
                            const std::string layerName = "Layer " + std::to_string(layerNumber);
                            const int32_t renderOrder = static_cast<int32_t>(children.size()) * 10;
                            auto* mutableGrid2D = mutableScene.GetRegistry().try_get<Grid2DComponent>(selectedEntity);
                            if (!mutableGrid2D)
                                return false;

                            entt::entity layerEntity = mutableScene.CreateEntity(layerName);
                            mutableScene.SetParent(layerEntity, selectedEntity);

                            auto& layer = mutableScene.GetRegistry().emplace<TilemapLayerComponent>(layerEntity);
                            layer.RenderOrder = renderOrder;
                            EnsureTilemapLayerStorage(*mutableGrid2D, layer);
                            return true;
                        });
                    }
                    else if (scene)
                    {
                        const auto children = scene->GetChildren(selectedEntity);
                        const int32_t layerNumber = static_cast<int32_t>(children.size()) + 1;
                        const std::string layerName = "Layer " + std::to_string(layerNumber);
                        const int32_t renderOrder = static_cast<int32_t>(children.size()) * 10;

                        entt::entity layerEntity = scene->CreateEntity(layerName);
                        scene->SetParent(layerEntity, selectedEntity);

                        auto& layer = registry.emplace<TilemapLayerComponent>(layerEntity);
                        layer.RenderOrder = renderOrder;
                        EnsureTilemapLayerStorage(*grid2D, layer);
                    }
                }

                if (scene)
                {
                    const auto children = scene->GetChildren(selectedEntity);
                    if (!children.empty())
                    {
                        ImGui::TextDisabled("Layers: %d", static_cast<int>(children.size()));
                        for (entt::entity child : children)
                        {
                            if (!registry.all_of<TilemapLayerComponent>(child))
                                continue;
                            const auto* tag = registry.try_get<TagComponent>(child);
                            const auto* childLayer = registry.try_get<TilemapLayerComponent>(child);
                            if (tag && childLayer)
                                ImGui::BulletText("%s (order %d)", tag->Tag.c_str(), childLayer->RenderOrder);
                        }
                    }
                }

                ImGui::TreePop();
            }
        }

        if (ShouldDrawInspectorSection(onlySectionKey, "TilemapLayer") && (registry.try_get<TilemapLayerComponent>(selectedEntity) != nullptr))
        {
            auto* tilemapLayer = registry.try_get<TilemapLayerComponent>(selectedEntity);
            const entt::entity tilemapGridEntity = scene ? scene->GetParent(selectedEntity) : entt::null;
            auto* tilemapGrid = (scene && tilemapGridEntity != entt::null)
                ? registry.try_get<Grid2DComponent>(tilemapGridEntity)
                : nullptr;
            if (tilemapGrid)
                EnsureTilemapLayerStorage(*tilemapGrid, *tilemapLayer);
            const bool layerOpen = BeginInspectorSectionHeader("Tilemap Layer", "TilemapLayerComponentOptions", "...##TilemapLayerComponentOptionsButton");
            if (orderedSectionKeys)
                (void)HandleSectionDragDrop("TilemapLayer", *orderedSectionKeys, "Tilemap Layer");

            if (ImGui::BeginPopup("TilemapLayerComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveTilemapLayerComponent = true;
                ImGui::EndPopup();
            }

            if (layerOpen)
            {
                ImGui::TextUnformatted("Render Order");
                ImGui::DragInt("##TilemapRenderOrder", &tilemapLayer->RenderOrder);
                TrackInteractiveMemberMutation<TilemapLayerComponent>(
                    undoService, "Edit TilemapLayer Render Order", selectedEntity, &TilemapLayerComponent::RenderOrder, tilemapLayer->RenderOrder);

                ImGui::TextUnformatted("Collision Enabled");
                ImGui::Checkbox("##TilemapCollisionEnabled", &tilemapLayer->CollisionEnabled);
                TrackInteractiveMemberMutation<TilemapLayerComponent>(
                    undoService,
                    "Edit TilemapLayer Collision",
                    selectedEntity,
                    &TilemapLayerComponent::CollisionEnabled,
                    tilemapLayer->CollisionEnabled);

                ImGui::TextUnformatted("Cast Shadows");
                ImGui::Checkbox("##TilemapCastShadows", &tilemapLayer->CastShadows);
                TrackInteractiveMemberMutation<TilemapLayerComponent>(
                    undoService, "Edit TilemapLayer Cast Shadows", selectedEntity, &TilemapLayerComponent::CastShadows, tilemapLayer->CastShadows);

                const int32_t tileCount = tilemapGrid ? GetTilemapCellCount(*tilemapGrid) : static_cast<int32_t>(tilemapLayer->Tiles.size());
                if (tilemapLayer->PaintedCellCacheDirty)
                    RebuildPaintedCellCache(*tilemapLayer);
                const int32_t nonEmptyCount = static_cast<int32_t>(tilemapLayer->CachedPaintedCells.size());
                ImGui::TextDisabled("Cells: %d  |  Painted: %d  |  Tile types: %d",
                                    tileCount, nonEmptyCount,
                                    static_cast<int>(tilemapLayer->TileTable.size()));

                ImGui::TreePop();
            }
        }
    }
}
