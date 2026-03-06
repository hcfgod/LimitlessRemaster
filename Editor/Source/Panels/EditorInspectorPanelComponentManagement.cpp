#include "EditorInspectorPanelComponentManagement.h"

#include "EditorComponentRegistry.h"
#include "Undo/EditorUndoService.h"
#include "Scene/Scene.h"
#include "imgui/imgui.h"

#include <algorithm>

namespace Limitless::EditorInspectorPanel
{
    namespace
    {
        template<typename ComponentType>
        bool HasComponent(const entt::registry& registry, entt::entity entity)
        {
            return registry.all_of<ComponentType>(entity);
        }

        void DrawAddComponentMenuItem(const ComponentRegistryEntry& entry,
                                      Scene* scene,
                                      entt::registry& registry,
                                      entt::entity selectedEntity,
                                      EditorUndoService* undoService)
        {
            const bool wasDisabled = entry.HasComponent(registry, selectedEntity);
            if (wasDisabled)
                ImGui::BeginDisabled();

            if (ImGui::MenuItem(entry.MenuItemLabel))
            {
                if (undoService)
                {
                    (void)undoService->ExecuteSceneMutation(entry.AddMutationLabel, [&](Scene& mutableScene) {
                        entry.AddComponent(mutableScene.GetRegistry(), selectedEntity);
                        return true;
                    });
                }
                else
                {
                    (void)scene;
                    entry.AddComponent(registry, selectedEntity);
                }
            }

            if (wasDisabled)
                ImGui::EndDisabled();
        }

        void ApplyPendingRemoval(const ComponentRegistryEntry& entry,
                                 bool shouldRemove,
                                 entt::registry& registry,
                                 entt::entity selectedEntity,
                                 EditorUndoService* undoService)
        {
            if (!shouldRemove)
                return;

            if (undoService)
            {
                (void)undoService->ExecuteSceneMutation(entry.RemoveMutationLabel, [&](Scene& mutableScene) {
                    entry.RemoveComponent(mutableScene.GetRegistry(), selectedEntity);
                    return true;
                });
            }
            else
            {
                entry.RemoveComponent(registry, selectedEntity);
            }
        }
    }

    void DrawAddComponentPopup(Scene* scene,
                               entt::registry& registry,
                               entt::entity selectedEntity,
                               EditorUndoService* undoService)
    {
        if (ImGui::Button("Add Component", ImVec2(-1.0f, 0.0f)))
            ImGui::OpenPopup("AddComponentPopup");

        if (!ImGui::BeginPopup("AddComponentPopup"))
            return;

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::Canvas))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::RectTransform))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::UIImage))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::UIPanel))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::UIText))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::UIButton))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        const bool wasSliderDisabled = HasComponent<UISliderComponent>(registry, selectedEntity);
        if (wasSliderDisabled)
            ImGui::BeginDisabled();

        if (ImGui::MenuItem("UI Slider"))
        {
            if (undoService)
                (void)undoService->ExecuteSceneMutation("Add UISlider Component", [&](Scene& mutableScene) {
                    auto& mutableRegistry = mutableScene.GetRegistry();
                    auto& slider = mutableRegistry.emplace<UISliderComponent>(selectedEntity);
                    slider.Value = std::clamp(0.5f, slider.MinValue, std::max(slider.MinValue, slider.MaxValue));
                    if (!mutableRegistry.all_of<UIImageComponent>(selectedEntity))
                        mutableRegistry.emplace<UIImageComponent>(selectedEntity);
                    if (!mutableRegistry.all_of<RectTransformComponent>(selectedEntity))
                        mutableRegistry.emplace<RectTransformComponent>(selectedEntity);

                    const float sliderRange = std::max(0.0001f, slider.MaxValue - slider.MinValue);
                    const float sliderNormalized = std::clamp((slider.Value - slider.MinValue) / sliderRange, 0.0f, 1.0f);
                    auto ensureSliderVisualChild = [&](const char* childName,
                                                       const glm::vec4& defaultColor,
                                                       int32_t siblingOrder,
                                                       auto&& initializeRectTransform) {
                        entt::entity childEntity = entt::null;
                        auto childView = mutableRegistry.view<TagComponent, HierarchyComponent>();
                        for (entt::entity candidate : childView)
                        {
                            const auto& hierarchy = childView.get<HierarchyComponent>(candidate);
                            if (hierarchy.Parent != selectedEntity)
                                continue;
                            const auto& tag = childView.get<TagComponent>(candidate);
                            if (tag.Tag == childName)
                            {
                                childEntity = candidate;
                                break;
                            }
                        }

                        bool created = false;
                        if (childEntity == entt::null)
                        {
                            childEntity = mutableScene.CreateEntity(childName);
                            mutableScene.SetParent(childEntity, selectedEntity);
                            created = true;
                        }

                        if (auto* hierarchy = mutableRegistry.try_get<HierarchyComponent>(childEntity))
                            hierarchy->SiblingOrder = siblingOrder;
                        if (!mutableRegistry.all_of<RectTransformComponent>(childEntity))
                            mutableRegistry.emplace<RectTransformComponent>(childEntity);
                        if (!mutableRegistry.all_of<UIImageComponent>(childEntity))
                            mutableRegistry.emplace<UIImageComponent>(childEntity);
                        if (!mutableRegistry.all_of<SpriteComponent>(childEntity))
                        {
                            auto& childSprite = mutableRegistry.emplace<SpriteComponent>(childEntity);
                            childSprite.Color = defaultColor;
                        }

                        if (created)
                        {
                            auto& rect = mutableRegistry.get<RectTransformComponent>(childEntity);
                            initializeRectTransform(rect);
                        }
                    };

                    ensureSliderVisualChild("Slider Background", slider.BackgroundColor, 0, [](RectTransformComponent& rect) {
                        rect.AnchorMin = glm::vec2(0.0f, 0.0f);
                        rect.AnchorMax = glm::vec2(1.0f, 1.0f);
                        rect.Pivot = glm::vec2(0.5f, 0.5f);
                        rect.SizeDelta = glm::vec2(0.0f, 0.0f);
                        rect.AnchoredPosition = glm::vec2(0.0f, 0.0f);
                    });
                    ensureSliderVisualChild("Slider Fill", slider.FillColor, 10, [sliderNormalized](RectTransformComponent& rect) {
                        rect.AnchorMin = glm::vec2(0.0f, 0.0f);
                        rect.AnchorMax = glm::vec2(sliderNormalized, 1.0f);
                        rect.Pivot = glm::vec2(0.5f, 0.5f);
                        rect.SizeDelta = glm::vec2(0.0f, 0.0f);
                        rect.AnchoredPosition = glm::vec2(0.0f, 0.0f);
                    });
                    ensureSliderVisualChild("Slider Handle", slider.HandleColor, 20, [sliderNormalized](RectTransformComponent& rect) {
                        rect.AnchorMin = glm::vec2(sliderNormalized, 0.5f);
                        rect.AnchorMax = glm::vec2(sliderNormalized, 0.5f);
                        rect.Pivot = glm::vec2(0.5f, 0.5f);
                        rect.SizeDelta = glm::vec2(16.0f, 48.0f);
                        rect.AnchoredPosition = glm::vec2(0.0f, 0.0f);
                    });
                    return true;
                });
            else {
                auto& slider = registry.emplace<UISliderComponent>(selectedEntity);
                slider.Value = std::clamp(0.5f, slider.MinValue, std::max(slider.MinValue, slider.MaxValue));
                if (!registry.all_of<UIImageComponent>(selectedEntity))
                    registry.emplace<UIImageComponent>(selectedEntity);
                if (!registry.all_of<RectTransformComponent>(selectedEntity))
                    registry.emplace<RectTransformComponent>(selectedEntity);

                const float sliderRange = std::max(0.0001f, slider.MaxValue - slider.MinValue);
                const float sliderNormalized = std::clamp((slider.Value - slider.MinValue) / sliderRange, 0.0f, 1.0f);
                auto ensureSliderVisualChild = [&](const char* childName,
                                                   const glm::vec4& defaultColor,
                                                   int32_t siblingOrder,
                                                   auto&& initializeRectTransform) {
                    entt::entity childEntity = entt::null;
                    auto childView = registry.view<TagComponent, HierarchyComponent>();
                    for (entt::entity candidate : childView)
                    {
                        const auto& hierarchy = childView.get<HierarchyComponent>(candidate);
                        if (hierarchy.Parent != selectedEntity)
                            continue;
                        const auto& tag = childView.get<TagComponent>(candidate);
                        if (tag.Tag == childName)
                        {
                            childEntity = candidate;
                            break;
                        }
                    }

                    bool created = false;
                    if (childEntity == entt::null)
                    {
                        childEntity = scene->CreateEntity(childName);
                        scene->SetParent(childEntity, selectedEntity);
                        created = true;
                    }

                    if (auto* hierarchy = registry.try_get<HierarchyComponent>(childEntity))
                        hierarchy->SiblingOrder = siblingOrder;
                    if (!registry.all_of<RectTransformComponent>(childEntity))
                        registry.emplace<RectTransformComponent>(childEntity);
                    if (!registry.all_of<UIImageComponent>(childEntity))
                        registry.emplace<UIImageComponent>(childEntity);
                    if (!registry.all_of<SpriteComponent>(childEntity))
                    {
                        auto& childSprite = registry.emplace<SpriteComponent>(childEntity);
                        childSprite.Color = defaultColor;
                    }

                    if (created)
                    {
                        auto& rect = registry.get<RectTransformComponent>(childEntity);
                        initializeRectTransform(rect);
                    }
                };

                ensureSliderVisualChild("Slider Background", slider.BackgroundColor, 0, [](RectTransformComponent& rect) {
                    rect.AnchorMin = glm::vec2(0.0f, 0.0f);
                    rect.AnchorMax = glm::vec2(1.0f, 1.0f);
                    rect.Pivot = glm::vec2(0.5f, 0.5f);
                    rect.SizeDelta = glm::vec2(0.0f, 0.0f);
                    rect.AnchoredPosition = glm::vec2(0.0f, 0.0f);
                });
                ensureSliderVisualChild("Slider Fill", slider.FillColor, 10, [sliderNormalized](RectTransformComponent& rect) {
                    rect.AnchorMin = glm::vec2(0.0f, 0.0f);
                    rect.AnchorMax = glm::vec2(sliderNormalized, 1.0f);
                    rect.Pivot = glm::vec2(0.5f, 0.5f);
                    rect.SizeDelta = glm::vec2(0.0f, 0.0f);
                    rect.AnchoredPosition = glm::vec2(0.0f, 0.0f);
                });
                ensureSliderVisualChild("Slider Handle", slider.HandleColor, 20, [sliderNormalized](RectTransformComponent& rect) {
                    rect.AnchorMin = glm::vec2(sliderNormalized, 0.5f);
                    rect.AnchorMax = glm::vec2(sliderNormalized, 0.5f);
                    rect.Pivot = glm::vec2(0.5f, 0.5f);
                    rect.SizeDelta = glm::vec2(16.0f, 48.0f);
                    rect.AnchoredPosition = glm::vec2(0.0f, 0.0f);
                });
            }
        }

        if (wasSliderDisabled)
            ImGui::EndDisabled();

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::Sprite))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::Camera))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::AudioListener2D))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::AudioListener3D))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::AudioSource))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::NativeScript))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::Animator))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::AnimationEventReceiver))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::Rigidbody2D))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::BoxCollider2D))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::CircleCollider2D))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::PolygonCollider2D))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::EdgeCollider2D))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::CapsuleCollider2D))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::Joint2D))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::DirectionalLight2D))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::PointLight2D))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::ShadowOccluder2D))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::Grid2D))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::TilemapLayer))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::ParticleEmitter))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        ImGui::EndPopup();
    }

    void ApplyPendingEntityComponentRemovals(Scene* scene,
                                             entt::registry& registry,
                                             entt::entity selectedEntity,
                                             PendingEntityComponentRemovals& pendingRemovals,
                                             bool removeNativeScriptComponent,
                                             EditorUndoService* undoService)
    {
        (void)scene;

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::Canvas))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveCanvasComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::RectTransform))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveRectTransformComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::UIImage))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveUIImageComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::UIPanel))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveUIPanelComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::UIText))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveUITextComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::UIButton))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveUIButtonComponent, registry, selectedEntity, undoService);

        if (pendingRemovals.RemoveUISliderComponent)
        {
            if (undoService)
            {
                (void)undoService->ExecuteSceneMutation("Remove UISlider Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().remove<UISliderComponent>(selectedEntity);
                    return true;
                });
            }
            else
            {
                registry.remove<UISliderComponent>(selectedEntity);
            }
        }

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::Sprite))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveSpriteComponent, registry, selectedEntity, undoService);

        if (pendingRemovals.RemoveMaterialComponent)
        {
            if (undoService)
            {
                (void)undoService->ExecuteSceneMutation("Remove Material Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().remove<MaterialComponent>(selectedEntity);
                    return true;
                });
            }
            else
            {
                registry.remove<MaterialComponent>(selectedEntity);
            }
        }

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::Camera))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveCameraComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::AudioListener2D))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveAudioListener2DComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::AudioListener3D))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveAudioListener3DComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::AudioSource))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveAudioSourceComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::NativeScript))
            ApplyPendingRemoval(*entry, removeNativeScriptComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::Animator))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveAnimatorComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::AnimationEventReceiver))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveAnimationEventReceiverComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::Rigidbody2D))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveRigidbody2DComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::BoxCollider2D))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveBoxCollider2DComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::CircleCollider2D))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveCircleCollider2DComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::PolygonCollider2D))
            ApplyPendingRemoval(*entry, pendingRemovals.RemovePolygonCollider2DComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::EdgeCollider2D))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveEdgeCollider2DComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::CapsuleCollider2D))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveCapsuleCollider2DComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::Joint2D))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveJoint2DComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::DirectionalLight2D))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveDirectionalLight2DComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::PointLight2D))
            ApplyPendingRemoval(*entry, pendingRemovals.RemovePointLight2DComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::ShadowOccluder2D))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveShadowOccluder2DComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::ParticleEmitter))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveParticleEmitterComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::Grid2D))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveGrid2DComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::TilemapLayer))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveTilemapLayerComponent, registry, selectedEntity, undoService);
    }
}
