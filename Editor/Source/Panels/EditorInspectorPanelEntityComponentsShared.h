#pragma once

#include "EditorInspectorPanel.h"
#include "EditorInspectorPanelEntityComponents.h"
#include "EditorPanelStyle.h"
#include "Undo/EditorUndoService.h"

#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>

namespace Limitless::EditorInspectorPanel::Internal
{
    inline constexpr const char* kSubSpritePayloadId = "SUB_SPRITE_KEY";

    struct SpriteDropAssignment
    {
        std::string TextureKey;
        int32_t SubSpriteIndex = -1;
        glm::vec2 UvMin = glm::vec2(0.0f);
        glm::vec2 UvMax = glm::vec2(1.0f);
    };

    struct AnimatorParametersSnapshot
    {
        std::unordered_map<std::string, bool> BoolParameters;
        std::unordered_map<std::string, float> FloatParameters;
        std::unordered_map<std::string, int32_t> IntegerParameters;
        std::unordered_map<std::string, bool> TriggerParameters;

        bool operator==(const AnimatorParametersSnapshot& other) const = default;
    };

    struct StandardEntityInspectorContext
    {
        Scene* SceneContext;
        entt::registry& Registry;
        entt::entity SelectedEntity;
        const char* TexturePayloadId;
        const char* AudioPayloadId;
        const char* MaterialPayloadId;
        const char* FontPayloadId;
        std::string& SelectedAnimationClipAssetKey;
        std::string& SelectedAnimatorControllerAssetKey;
        PendingEntityComponentRemovals& PendingRemovals;
        EditorUndoService* UndoService;
        std::string_view OnlySectionKey;
        const std::vector<std::string>* OrderedSectionKeys;
    };

    bool ShouldDrawInspectorSection(std::string_view onlySectionKey, std::string_view sectionKey);
    bool BeginInspectorSectionHeader(const char* label, const char* popupId, const char* optionsButtonId);
    void ClearPrimaryFlagFromOtherCameras(entt::registry& registry, entt::entity currentEntity);
    bool ResolveSpriteDropAssignment(const std::string& droppedKey, SpriteDropAssignment& outAssignment);
    std::string ResolveTextureKeyFromDroppedKey(const std::string& droppedKey);
    std::vector<std::string> BuildMaterialPickerKeys();
    std::vector<std::string> BuildAudioClipPickerKeys();
    std::vector<std::string> BuildAnimationClipPickerKeys();
    std::vector<std::string> BuildAnimatorControllerPickerKeys();
    glm::vec2 NormalizeDirectionOrFallback(const glm::vec2& direction, const glm::vec2& fallback = glm::vec2(0.0f, -1.0f));
    entt::entity FindDirectChildByTag(const entt::registry& registry, entt::entity parentEntity, std::string_view childTag);
    bool SliderHasVisualChildren(const entt::registry& registry, entt::entity sliderEntity);
    void SyncSliderVisualChildrenInEditor(entt::registry& registry, entt::entity sliderEntity, const UISliderComponent& slider);

    void DrawSceneComponentSections(StandardEntityInspectorContext& context);
    void DrawAudioComponentSections(StandardEntityInspectorContext& context);
    void DrawGridComponentSections(StandardEntityInspectorContext& context);
    void DrawPhysics2DComponentSections(StandardEntityInspectorContext& context);
    void DrawLighting2DComponentSections(StandardEntityInspectorContext& context);
    void DrawUiComponentSections(StandardEntityInspectorContext& context);
    void DrawParticleComponentSections(StandardEntityInspectorContext& context);

    template<typename TValue, typename TApply>
    void TrackInteractiveValueMutation(EditorUndoService* undoService,
                                       const char* label,
                                       const TValue& currentValue,
                                       TApply&& applyValue)
    {
        if (!undoService || !label)
            return;

        const ImGuiID itemId = ImGui::GetItemID();
        if (itemId == 0)
            return;

        using DecayedValueType = std::decay_t<TValue>;
        static std::unordered_map<ImGuiID, DecayedValueType> beforeValues;
        if (ImGui::IsItemActivated())
            beforeValues[itemId] = currentValue;

        if (!ImGui::IsItemDeactivatedAfterEdit())
            return;

        const auto beforeIt = beforeValues.find(itemId);
        if (beforeIt == beforeValues.end())
            return;

        const DecayedValueType beforeValue = beforeIt->second;
        beforeValues.erase(beforeIt);

        const DecayedValueType afterValue = currentValue;
        std::function<bool(const DecayedValueType&)> applyCallback = std::forward<TApply>(applyValue);
        (void)undoService->ExecuteValueMutation(label, beforeValue, afterValue, std::move(applyCallback));
    }

    template<typename TValue, typename TApply>
    void TrackInteractiveVectorValueMutation(EditorUndoService* undoService,
                                             const char* label,
                                             const EditorPanelStyle::AxisVectorDragState& interactionState,
                                             const TValue& currentValue,
                                             TApply&& applyValue)
    {
        if (!undoService || !label || interactionState.InteractionId == 0)
            return;

        using DecayedValueType = std::decay_t<TValue>;
        static std::unordered_map<ImGuiID, DecayedValueType> beforeValues;
        if (interactionState.Activated)
            beforeValues[interactionState.InteractionId] = currentValue;

        if (!interactionState.DeactivatedAfterEdit)
            return;

        const auto beforeIt = beforeValues.find(interactionState.InteractionId);
        if (beforeIt == beforeValues.end())
            return;

        const DecayedValueType beforeValue = beforeIt->second;
        beforeValues.erase(beforeIt);

        const DecayedValueType afterValue = currentValue;
        std::function<bool(const DecayedValueType&)> applyCallback = std::forward<TApply>(applyValue);
        (void)undoService->ExecuteValueMutation(label, beforeValue, afterValue, std::move(applyCallback));
    }

    template<typename TComponent, typename TValue>
    void TrackInteractiveMemberMutation(EditorUndoService* undoService,
                                        const char* label,
                                        entt::entity entity,
                                        TValue TComponent::* member,
                                        const TValue& currentValue,
                                        const std::function<void(Scene&, TComponent&)>& onApply = {})
    {
        TrackInteractiveValueMutation(undoService, label, currentValue, [undoService, entity, member, onApply](const TValue& value) {
            if (!undoService)
                return false;
            Scene* activeScene = undoService->GetActiveScene();
            if (!activeScene || !activeScene->IsValid(entity))
                return false;

            auto* component = activeScene->GetRegistry().try_get<TComponent>(entity);
            if (!component)
                return false;

            component->*member = value;
            if (onApply)
                onApply(*activeScene, *component);
            return true;
        });
    }

    template<typename TComponent, typename TValue>
    void TrackInteractiveVectorMemberMutation(EditorUndoService* undoService,
                                              const char* label,
                                              const EditorPanelStyle::AxisVectorDragState& interactionState,
                                              entt::entity entity,
                                              TValue TComponent::* member,
                                              const TValue& currentValue,
                                              const std::function<void(Scene&, TComponent&)>& onApply = {})
    {
        TrackInteractiveVectorValueMutation(undoService, label, interactionState, currentValue, [undoService, entity, member, onApply](const TValue& value) {
            if (!undoService)
                return false;
            Scene* activeScene = undoService->GetActiveScene();
            if (!activeScene || !activeScene->IsValid(entity))
                return false;

            auto* component = activeScene->GetRegistry().try_get<TComponent>(entity);
            if (!component)
                return false;

            component->*member = value;
            if (onApply)
                onApply(*activeScene, *component);
            return true;
        });
    }

    template<typename TComponent, typename TValue>
    void ExecuteVectorMemberMutation(EditorUndoService* undoService,
                                     const char* label,
                                     entt::entity entity,
                                     std::vector<TValue> TComponent::* member,
                                     const std::vector<TValue>& beforeValue,
                                     const std::vector<TValue>& afterValue)
    {
        if (!undoService || !label)
            return;

        (void)undoService->ExecuteLambdaCommand(
            label,
            [undoService, entity, member, beforeValue]() {
                Scene* activeScene = undoService ? undoService->GetActiveScene() : nullptr;
                if (!activeScene || !activeScene->IsValid(entity))
                    return false;

                auto* component = activeScene->GetRegistry().try_get<TComponent>(entity);
                if (!component)
                    return false;

                component->*member = beforeValue;
                return true;
            },
            [undoService, entity, member, afterValue]() {
                Scene* activeScene = undoService ? undoService->GetActiveScene() : nullptr;
                if (!activeScene || !activeScene->IsValid(entity))
                    return false;

                auto* component = activeScene->GetRegistry().try_get<TComponent>(entity);
                if (!component)
                    return false;

                component->*member = afterValue;
                return true;
            });
    }
}
