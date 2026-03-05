#include "EditorAnimationTimelinePanel.h"

#include "EditorAssetNaming.h"
#include "Assets/AnimationClipAsset.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetImportPipeline.h"
#include "Assets/AssetManager.h"
#include "Assets/AssetPaths.h"
#include "Assets/AssetTypes.h"
#include "Assets/SpriteImportSettings.h"
#include "Assets/TextureAsset.h"
#include "Core/Debug/Log.h"
#include "Undo/EditorTextAssetCommand.h"
#include "Undo/EditorUndoService.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

namespace Limitless::EditorAnimationTimelinePanel
{
    namespace
    {
        using json = nlohmann::json;
        constexpr const char* kSubSpritePayloadId = "SUB_SPRITE_KEY";

        struct TimelineEditorState
        {
            std::string LoadedAssetKey;
            std::filesystem::path ResolvedPath;
            json AppliedJson = json::object();
            json WorkingJson = json::object();
            bool Loaded = false;
            bool LoadFailed = false;
            float PreviewTimeSeconds = 0.0f;
            bool IsPlaying = false;
            bool IsPaused = false;
            std::string StatusMessage;
            bool StatusIsError = false;
        };

        TimelineEditorState& GetTimelineEditorState()
        {
            static TimelineEditorState state;
            return state;
        }

        std::string JsonInterpolationName(int interpolationIndex)
        {
            return interpolationIndex == 0 ? "Step" : "Linear";
        }

        int InterpolationIndexFromJson(const json& keyframeJson)
        {
            const std::string interpolation = keyframeJson.value("Interpolation", std::string("Linear"));
            return interpolation == "Step" ? 0 : 1;
        }

        bool ResolveClipPath(const std::string& assetKey, std::filesystem::path& outPath)
        {
            const auto resolvedResult = Assets::ResolveAssetKeyToPath(assetKey);
            if (resolvedResult.IsFailure())
                return false;
            outPath = resolvedResult.GetValue();
            return true;
        }

        std::vector<std::string> BuildTexturePickerKeys()
        {
            std::vector<std::string> keys;
            std::unordered_set<std::string> seen;

            const auto records = Assets::AssetDatabase::GetInstance().GetAllRecords();
            for (const auto& record : records)
            {
                if (record.Type != Assets::AssetType::Texture2D || record.Key.empty())
                    continue;
                if (seen.insert(record.Key).second)
                    keys.push_back(record.Key);
            }

            std::sort(keys.begin(), keys.end());
            return keys;
        }

        std::vector<std::string> ParseAssetKeyListPayload(const ImGuiPayload* payload)
        {
            std::vector<std::string> keys;
            if (!payload || !payload->Data || payload->DataSize <= 0)
                return keys;

            std::string payloadText(static_cast<const char*>(payload->Data), static_cast<size_t>(payload->DataSize));
            while (!payloadText.empty() && payloadText.back() == '\0')
                payloadText.pop_back();
            if (payloadText.empty())
                return keys;

            size_t lineStart = 0;
            while (lineStart < payloadText.size())
            {
                const size_t lineEnd = payloadText.find('\n', lineStart);
                const size_t count = (lineEnd == std::string::npos) ? (payloadText.size() - lineStart) : (lineEnd - lineStart);
                std::string key = payloadText.substr(lineStart, count);
                if (!key.empty())
                    keys.push_back(std::move(key));
                if (lineEnd == std::string::npos)
                    break;
                lineStart = lineEnd + 1;
            }
            return keys;
        }

        std::string ResolveTextureKeyFromDroppedKey(const std::string& droppedKey)
        {
            std::string textureKey;
            int32_t subSpriteIndex = -1;
            if (Assets::TryParseSubSpriteAssetKey(droppedKey, textureKey, subSpriteIndex))
                return textureKey;
            return droppedKey;
        }

        bool IsTextureAssetKey(const std::string& assetKey)
        {
            if (assetKey.empty())
                return false;

            const std::string resolvedKey = ResolveTextureKeyFromDroppedKey(assetKey);
            if (resolvedKey.empty())
                return false;

            const auto record = Assets::AssetDatabase::GetInstance().FindByKey(resolvedKey);
            if (record.IsSuccess())
                return record.GetValue().Type == Assets::AssetType::Texture2D;

            const std::filesystem::path keyPath(resolvedKey);
            std::string extension = keyPath.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            return extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
                   extension == ".ppm" || extension == ".pnm" || extension == ".bmp" ||
                   extension == ".tga" || extension == ".gif";
        }

        std::vector<std::string> FilterTextureAssetKeys(const std::vector<std::string>& keys)
        {
            std::vector<std::string> textureKeys;
            textureKeys.reserve(keys.size());
            for (const auto& key : keys)
            {
                if (IsTextureAssetKey(key))
                    textureKeys.push_back(key);
            }
            return textureKeys;
        }

        struct DroppedSpriteEntry
        {
            std::string TextureKey;
            int32_t SubSpriteIndex = -1;
            bool HasSubRect = false;
            glm::vec2 UvMin = glm::vec2(0.0f);
            glm::vec2 UvMax = glm::vec2(1.0f);
        };

        bool TryResolveDroppedSpriteEntry(const std::string& droppedKey, DroppedSpriteEntry& outEntry)
        {
            outEntry = DroppedSpriteEntry{};
            if (!IsTextureAssetKey(droppedKey))
                return false;

            std::string textureKey;
            int32_t subSpriteIndex = -1;
            if (!Assets::TryParseSubSpriteAssetKey(droppedKey, textureKey, subSpriteIndex))
            {
                outEntry.TextureKey = droppedKey;
                return true;
            }

            outEntry.TextureKey = textureKey;
            if (textureKey.empty())
                return false;

            const auto spriteSettings = Assets::LoadSpriteImportSettings(textureKey);
            if (subSpriteIndex < 0 || subSpriteIndex >= static_cast<int32_t>(spriteSettings.SubSprites.size()))
                return true;
            outEntry.SubSpriteIndex = subSpriteIndex;

            Assets::TextureAsset::Ptr textureAsset =
                std::dynamic_pointer_cast<Assets::TextureAsset>(Assets::AssetManager::GetCachedByKey(textureKey));
            if (!textureAsset)
                textureAsset = Assets::TextureAsset::LoadBlocking(textureKey);
            if (!textureAsset || !textureAsset->GetTexture())
                return true;

            const auto uvs = Assets::ComputeSubSpriteUvs(
                spriteSettings.SubSprites[static_cast<size_t>(subSpriteIndex)].RectPixels,
                textureAsset->GetTexture()->GetWidth(),
                textureAsset->GetTexture()->GetHeight());
            outEntry.HasSubRect = true;
            outEntry.UvMin = glm::vec2(uvs.x, uvs.y);
            outEntry.UvMax = glm::vec2(uvs.z, uvs.w);
            return true;
        }

        json BuildTextureKeyframeTextureObject(const DroppedSpriteEntry& entry)
        {
            json textureObject = json::object();
            textureObject["key"] = entry.TextureKey;
            if (entry.SubSpriteIndex >= 0)
                textureObject["subSpriteIndex"] = entry.SubSpriteIndex;
            return textureObject;
        }

        void UpsertSubRectKeyframe(json& subRectTrackArray,
                                   float timeSeconds,
                                   const glm::vec2& uvMin,
                                   const glm::vec2& uvMax)
        {
            for (auto& keyframe : subRectTrackArray)
            {
                if (!keyframe.is_object())
                    continue;
                const float existingTime = keyframe.value("TimeSeconds", -1.0f);
                if (std::abs(existingTime - timeSeconds) <= 0.0001f)
                {
                    keyframe["UvMin"] = { uvMin.x, uvMin.y };
                    keyframe["UvMax"] = { uvMax.x, uvMax.y };
                    return;
                }
            }

            subRectTrackArray.push_back({
                {"TimeSeconds", timeSeconds},
                {"UvMin", {uvMin.x, uvMin.y}},
                {"UvMax", {uvMax.x, uvMax.y}}
            });
        }

        bool LoadJsonFromPath(const std::filesystem::path& path, json& outJson)
        {
            std::ifstream input(path, std::ios::in | std::ios::binary);
            if (!input.is_open())
                return false;
            try
            {
                input >> outJson;
            }
            catch (...)
            {
                return false;
            }
            if (!outJson.is_object())
                outJson = json::object();
            return true;
        }

        void EnsureClipSchemaDefaults(json& root)
        {
            if (!root.is_object())
                root = json::object();

            if (!root.contains("Version") || !root["Version"].is_number_integer())
                root["Version"] = 1;
            if (!root.contains("Name") || !root["Name"].is_string())
                root["Name"] = std::string("New Clip");
            if (!root.contains("Loop") || !root["Loop"].is_boolean())
                root["Loop"] = true;
            if (!root.contains("DurationSeconds") || !root["DurationSeconds"].is_number())
                root["DurationSeconds"] = 1.0f;
            if (!root.contains("SamplesPerSecond") || !root["SamplesPerSecond"].is_number())
                root["SamplesPerSecond"] = 30.0f;

            root["DurationSeconds"] = std::max(0.0001f, root.value("DurationSeconds", 1.0f));
            root["SamplesPerSecond"] = std::max(1.0f, root.value("SamplesPerSecond", 30.0f));

            const auto ensureArray = [&](const char* key) {
                if (!root.contains(key) || !root[key].is_array())
                    root[key] = json::array();
            };

            ensureArray("SpriteSubRectTrack");
            ensureArray("SpriteTextureTrack");
            ensureArray("PositionTrack");
            ensureArray("ScaleTrack");
            ensureArray("RotationZTrack");
            ensureArray("EventTrack");
        }

        bool ApplyClipTextToDisk(const std::filesystem::path& path,
                                 const std::string& assetKey,
                                 const std::string& jsonText)
        {
            std::ofstream output(path, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!output.is_open())
                return false;
            output << jsonText;
            output.flush();
            if (!output.good())
                return false;

            (void)Assets::AssetDatabase::GetInstance().ImportOrUpdate(assetKey, Assets::AssetType::AnimationClip);
            (void)Assets::AssetImportPipeline::ReimportChanged(true);
            if (const auto cached = Assets::AssetManager::GetCachedByKey(assetKey))
                (void)cached->Reload();
            return true;
        }

        bool CommitClipChanges(EditorUndoService* undoService,
                               const std::string& label,
                               const std::string& assetKey,
                               const std::filesystem::path& path,
                               const json& beforeJson,
                               const json& afterJson)
        {
            const std::string beforeText = beforeJson.dump(2);
            const std::string afterText = afterJson.dump(2);
            if (beforeText == afterText)
                return true;

            auto applyCallback = [path, assetKey](const std::string& text) {
                return ApplyClipTextToDisk(path, assetKey, text);
            };

            if (!undoService)
                return applyCallback(afterText);

            // Apply first so persisted asset state changes immediately; command stores undo/redo texts.
            if (!applyCallback(afterText))
                return false;

            auto command = std::make_unique<EditorTextAssetCommand>(
                label,
                beforeText,
                afterText,
                std::move(applyCallback));
            return undoService->ExecuteCommand(std::move(command));
        }

        bool ApplyPendingClipChanges(EditorUndoService* undoService, TimelineEditorState& state, const char* label)
        {
            if (!state.Loaded || state.LoadFailed)
                return true;

            if (state.LoadedAssetKey.empty() || state.ResolvedPath.empty())
                return false;

            if (state.WorkingJson.dump() == state.AppliedJson.dump())
                return true;

            if (!CommitClipChanges(
                    undoService,
                    label ? std::string(label) : std::string("Edit Animation Clip"),
                    state.LoadedAssetKey,
                    state.ResolvedPath,
                    state.AppliedJson,
                    state.WorkingJson))
            {
                return false;
            }

            state.AppliedJson = state.WorkingJson;
            state.StatusMessage = "Animation clip changes applied.";
            state.StatusIsError = false;
            return true;
        }

        const json* SampleStepJsonKeyframe(const json& trackArray, float timeSeconds)
        {
            if (!trackArray.is_array() || trackArray.empty())
                return nullptr;

            const json* sampled = &trackArray.front();
            for (const auto& keyframe : trackArray)
            {
                if (!keyframe.is_object())
                    continue;
                if (keyframe.value("TimeSeconds", 0.0f) <= timeSeconds)
                {
                    sampled = &keyframe;
                    continue;
                }
                break;
            }
            return sampled;
        }

        void SortTrackByTime(json& trackArray)
        {
            if (!trackArray.is_array())
                return;
            std::sort(trackArray.begin(), trackArray.end(), [](const json& left, const json& right) {
                return left.value("TimeSeconds", 0.0f) < right.value("TimeSeconds", 0.0f);
            });
        }

        void DrawFloatField(const char* label, float& value, float speed = 0.01f)
        {
            ImGui::TextUnformatted(label);
            std::string widgetId = "##";
            widgetId += label;
            ImGui::DragFloat(widgetId.c_str(), &value, speed);
        }

        void DrawVec2Field(const char* label, json& valueArray, float speed = 0.01f)
        {
            if (!valueArray.is_array() || valueArray.size() != 2)
                valueArray = json::array({0.0f, 0.0f});
            float value[2] = {
                valueArray[0].get<float>(),
                valueArray[1].get<float>()
            };
            ImGui::TextUnformatted(label);
            std::string widgetId = "##";
            widgetId += label;
            if (ImGui::DragFloat2(widgetId.c_str(), value, speed))
                valueArray = json::array({value[0], value[1]});
        }

        void DrawVec3Field(const char* label, json& valueArray, float speed = 0.01f)
        {
            if (!valueArray.is_array() || valueArray.size() != 3)
                valueArray = json::array({0.0f, 0.0f, 0.0f});
            float value[3] = {
                valueArray[0].get<float>(),
                valueArray[1].get<float>(),
                valueArray[2].get<float>()
            };
            ImGui::TextUnformatted(label);
            std::string widgetId = "##";
            widgetId += label;
            if (ImGui::DragFloat3(widgetId.c_str(), value, speed))
                valueArray = json::array({value[0], value[1], value[2]});
        }

        void DrawSpriteSubRectTrack(json& trackArray)
        {
            ImGui::SeparatorText("Sprite Sub-Rect Track");
            if (ImGui::Button("Add Sub-Rect Keyframe"))
            {
                trackArray.push_back({
                    {"TimeSeconds", 0.0f},
                    {"UvMin", {0.0f, 0.0f}},
                    {"UvMax", {1.0f, 1.0f}}
                });
            }

            int32_t removeIndex = -1;
            for (size_t index = 0; index < trackArray.size(); ++index)
            {
                auto& keyframe = trackArray[index];
                ImGui::PushID(static_cast<int>(index));
                const std::string header = "Sub-Rect Keyframe " + std::to_string(index);
                if (ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                {
                    float timeSeconds = keyframe.value("TimeSeconds", 0.0f);
                    ImGui::TextUnformatted("Time (Seconds)");
                    if (ImGui::DragFloat("##TimeSeconds", &timeSeconds, 0.01f))
                        keyframe["TimeSeconds"] = std::max(0.0f, timeSeconds);

                    DrawVec2Field("UV Min", keyframe["UvMin"], 0.001f);
                    DrawVec2Field("UV Max", keyframe["UvMax"], 0.001f);
                    if (ImGui::Button("Remove Keyframe"))
                        removeIndex = static_cast<int32_t>(index);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }

            if (removeIndex >= 0)
                trackArray.erase(trackArray.begin() + removeIndex);
            SortTrackByTime(trackArray);
        }

        void DrawSpriteTextureTrack(json& trackArray, json& subRectTrackArray, float samplesPerSecond)
        {
            ImGui::SeparatorText("Sprite Texture Track");
            if (ImGui::Button("Add Texture Keyframe"))
            {
                trackArray.push_back({
                    {"TimeSeconds", 0.0f},
                    {"Texture", {{"key", ""}}}
                });
            }
            ImGui::SameLine();
            ImGui::Button("Drop Textures Here##SpriteTextureTrackDropTarget", ImVec2(240.0f, 0.0f));
            if (ImGui::BeginDragDropTarget())
            {
                std::vector<DroppedSpriteEntry> droppedEntries;
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSubSpritePayloadId))
                {
                    const char* key = static_cast<const char*>(payload->Data);
                    if (key && key[0])
                    {
                        DroppedSpriteEntry entry;
                        if (TryResolveDroppedSpriteEntry(key, entry))
                            droppedEntries.push_back(std::move(entry));
                    }
                }
                else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_TEXTURE"))
                {
                    const char* key = static_cast<const char*>(payload->Data);
                    if (key && key[0])
                    {
                        DroppedSpriteEntry entry;
                        if (TryResolveDroppedSpriteEntry(key, entry))
                            droppedEntries.push_back(std::move(entry));
                    }
                }
                else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_MULTI_KEYS"))
                {
                    const std::vector<std::string> droppedKeys = FilterTextureAssetKeys(ParseAssetKeyListPayload(payload));
                    droppedEntries.reserve(droppedKeys.size());
                    for (const auto& key : droppedKeys)
                    {
                        DroppedSpriteEntry entry;
                        if (TryResolveDroppedSpriteEntry(key, entry))
                            droppedEntries.push_back(std::move(entry));
                    }
                }

                if (!droppedEntries.empty())
                {
                    const float frameStepSeconds = 1.0f / std::max(1.0f, samplesPerSecond);
                    const float startTime = trackArray.empty()
                        ? 0.0f
                        : (trackArray.back().value("TimeSeconds", 0.0f) + frameStepSeconds);
                    bool subRectTrackMutated = false;

                    for (size_t keyIndex = 0; keyIndex < droppedEntries.size(); ++keyIndex)
                    {
                        const float keyframeTime = startTime + static_cast<float>(keyIndex) * frameStepSeconds;
                        const auto& entry = droppedEntries[keyIndex];
                        trackArray.push_back({
                            {"TimeSeconds", keyframeTime},
                            {"Texture", BuildTextureKeyframeTextureObject(entry)}
                        });
                        if (entry.HasSubRect)
                        {
                            UpsertSubRectKeyframe(subRectTrackArray, keyframeTime, entry.UvMin, entry.UvMax);
                            subRectTrackMutated = true;
                        }
                    }

                    if (subRectTrackMutated)
                        SortTrackByTime(subRectTrackArray);
                }
                ImGui::EndDragDropTarget();
            }

            int32_t removeIndex = -1;
            for (size_t index = 0; index < trackArray.size(); ++index)
            {
                auto& keyframe = trackArray[index];
                ImGui::PushID(static_cast<int>(index));
                const std::string header = "Texture Keyframe " + std::to_string(index);
                if (ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                {
                    float timeSeconds = keyframe.value("TimeSeconds", 0.0f);
                    ImGui::TextUnformatted("Time (Seconds)");
                    if (ImGui::DragFloat("##TimeSeconds", &timeSeconds, 0.01f))
                        keyframe["TimeSeconds"] = std::max(0.0f, timeSeconds);

                    if (!keyframe.contains("Texture") || !keyframe["Texture"].is_object())
                        keyframe["Texture"] = json::object();
                    std::string textureKey = keyframe["Texture"].value("key", std::string{});
                    const int32_t textureSubSpriteIndex = keyframe["Texture"].value("subSpriteIndex", -1);
                    const std::string textureLabel = textureKey.empty()
                        ? std::string("None")
                        : (EditorAssetNaming::GetAssetDisplayNameFromAssetKey(textureKey) +
                           (textureSubSpriteIndex >= 0 ? ("#" + std::to_string(textureSubSpriteIndex)) : std::string{}));

                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("Texture");
                    ImGui::Button((textureLabel + "##TextureKeyframeSlot").c_str(),
                                  ImVec2(std::max(60.0f, ImGui::GetContentRegionAvail().x - 120.0f), 0.0f));
                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSubSpritePayloadId))
                        {
                            const char* key = static_cast<const char*>(payload->Data);
                            if (key && key[0])
                            {
                                DroppedSpriteEntry entry;
                                if (TryResolveDroppedSpriteEntry(key, entry))
                                {
                                    if (textureKey != entry.TextureKey)
                                    {
                                        textureKey = entry.TextureKey;
                                        keyframe["Texture"]["key"] = textureKey;
                                    }
                                    if (entry.SubSpriteIndex >= 0)
                                        keyframe["Texture"]["subSpriteIndex"] = entry.SubSpriteIndex;
                                    else
                                        keyframe["Texture"].erase("subSpriteIndex");
                                    if (entry.HasSubRect)
                                    {
                                        UpsertSubRectKeyframe(
                                            subRectTrackArray,
                                            keyframe.value("TimeSeconds", 0.0f),
                                            entry.UvMin,
                                            entry.UvMax);
                                        SortTrackByTime(subRectTrackArray);
                                    }
                                }
                            }
                        }
                        else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_TEXTURE"))
                        {
                            const char* key = static_cast<const char*>(payload->Data);
                            if (key && key[0])
                            {
                                DroppedSpriteEntry entry;
                                if (TryResolveDroppedSpriteEntry(key, entry))
                                {
                                    if (textureKey != entry.TextureKey)
                                    {
                                        textureKey = entry.TextureKey;
                                        keyframe["Texture"]["key"] = textureKey;
                                    }
                                    if (entry.SubSpriteIndex >= 0)
                                        keyframe["Texture"]["subSpriteIndex"] = entry.SubSpriteIndex;
                                    else
                                        keyframe["Texture"].erase("subSpriteIndex");
                                    if (entry.HasSubRect)
                                    {
                                        UpsertSubRectKeyframe(
                                            subRectTrackArray,
                                            keyframe.value("TimeSeconds", 0.0f),
                                            entry.UvMin,
                                            entry.UvMax);
                                        SortTrackByTime(subRectTrackArray);
                                    }
                                }
                            }
                        }
                        else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_MULTI_KEYS"))
                        {
                            const std::vector<std::string> droppedKeys = FilterTextureAssetKeys(ParseAssetKeyListPayload(payload));
                            std::vector<DroppedSpriteEntry> droppedEntries;
                            droppedEntries.reserve(droppedKeys.size());
                            for (const auto& key : droppedKeys)
                            {
                                DroppedSpriteEntry entry;
                                if (TryResolveDroppedSpriteEntry(key, entry))
                                    droppedEntries.push_back(std::move(entry));
                            }

                            if (!droppedEntries.empty())
                            {
                                const float frameStepSeconds = 1.0f / std::max(1.0f, samplesPerSecond);
                                const float startTime = keyframe.value("TimeSeconds", 0.0f);
                                bool subRectTrackMutated = false;

                                keyframe["Texture"]["key"] = droppedEntries.front().TextureKey;
                                if (droppedEntries.front().SubSpriteIndex >= 0)
                                    keyframe["Texture"]["subSpriteIndex"] = droppedEntries.front().SubSpriteIndex;
                                else
                                    keyframe["Texture"].erase("subSpriteIndex");
                                if (droppedEntries.front().HasSubRect)
                                {
                                    UpsertSubRectKeyframe(subRectTrackArray, startTime, droppedEntries.front().UvMin, droppedEntries.front().UvMax);
                                    subRectTrackMutated = true;
                                }

                                for (size_t keyIndex = 1; keyIndex < droppedEntries.size(); ++keyIndex)
                                {
                                    const float keyframeTime = startTime + static_cast<float>(keyIndex) * frameStepSeconds;
                                    const auto& entry = droppedEntries[keyIndex];
                                    trackArray.push_back({
                                        {"TimeSeconds", keyframeTime},
                                        {"Texture", BuildTextureKeyframeTextureObject(entry)}
                                    });
                                    if (entry.HasSubRect)
                                    {
                                        UpsertSubRectKeyframe(subRectTrackArray, keyframeTime, entry.UvMin, entry.UvMax);
                                        subRectTrackMutated = true;
                                    }
                                }

                                if (subRectTrackMutated)
                                    SortTrackByTime(subRectTrackArray);
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("...##TextureKeyframePicker"))
                        ImGui::OpenPopup("TextureKeyframePickerPopup");
                    if (ImGui::BeginPopup("TextureKeyframePickerPopup"))
                    {
                        if (ImGui::Selectable("None##TextureKeyframePickerNone"))
                        {
                            textureKey.clear();
                            keyframe["Texture"]["key"] = "";
                            keyframe["Texture"].erase("subSpriteIndex");
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::Separator();

                        const std::vector<std::string> textureKeys = BuildTexturePickerKeys();
                        for (const auto& key : textureKeys)
                        {
                            const bool isSelected = (textureKey == key);
                            const std::string display = EditorAssetNaming::GetAssetDisplayNameFromAssetKey(key);
                            if (ImGui::Selectable((display + "##TextureKeyframePicker_" + key).c_str(), isSelected))
                            {
                                textureKey = key;
                                keyframe["Texture"]["key"] = textureKey;
                                keyframe["Texture"].erase("subSpriteIndex");
                                ImGui::CloseCurrentPopup();
                            }
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                                ImGui::SetTooltip("%s", key.c_str());
                        }
                        ImGui::EndPopup();
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("X##ClearTextureKeyframe"))
                    {
                        textureKey.clear();
                        keyframe["Texture"]["key"] = "";
                        keyframe["Texture"].erase("subSpriteIndex");
                    }

                    if (ImGui::Button("Remove Keyframe"))
                        removeIndex = static_cast<int32_t>(index);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }

            if (removeIndex >= 0)
                trackArray.erase(trackArray.begin() + removeIndex);
            SortTrackByTime(trackArray);
        }

        void DrawVector3Track(const char* title, json& trackArray)
        {
            ImGui::SeparatorText(title);
            if (ImGui::Button((std::string("Add ") + title + " Keyframe").c_str()))
            {
                trackArray.push_back({
                    {"TimeSeconds", 0.0f},
                    {"Value", {0.0f, 0.0f, 0.0f}},
                    {"Interpolation", "Linear"}
                });
            }

            int32_t removeIndex = -1;
            for (size_t index = 0; index < trackArray.size(); ++index)
            {
                auto& keyframe = trackArray[index];
                ImGui::PushID(static_cast<int>(index));
                const std::string header = std::string(title) + " Keyframe " + std::to_string(index);
                if (ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                {
                    float timeSeconds = keyframe.value("TimeSeconds", 0.0f);
                    ImGui::TextUnformatted("Time (Seconds)");
                    if (ImGui::DragFloat("##TimeSeconds", &timeSeconds, 0.01f))
                        keyframe["TimeSeconds"] = std::max(0.0f, timeSeconds);

                    DrawVec3Field("Value", keyframe["Value"], 0.01f);

                    int interpolationIndex = InterpolationIndexFromJson(keyframe);
                    const char* interpolationNames[] = {"Step", "Linear"};
                    ImGui::TextUnformatted("Interpolation");
                    if (ImGui::Combo("##Interpolation", &interpolationIndex, interpolationNames, 2))
                        keyframe["Interpolation"] = JsonInterpolationName(interpolationIndex);

                    if (ImGui::Button("Remove Keyframe"))
                        removeIndex = static_cast<int32_t>(index);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }

            if (removeIndex >= 0)
                trackArray.erase(trackArray.begin() + removeIndex);
            SortTrackByTime(trackArray);
        }

        void DrawFloatTrack(json& trackArray)
        {
            ImGui::SeparatorText("Rotation Z Track");
            if (ImGui::Button("Add Rotation Keyframe"))
            {
                trackArray.push_back({
                    {"TimeSeconds", 0.0f},
                    {"Value", 0.0f},
                    {"Interpolation", "Linear"}
                });
            }

            int32_t removeIndex = -1;
            for (size_t index = 0; index < trackArray.size(); ++index)
            {
                auto& keyframe = trackArray[index];
                ImGui::PushID(static_cast<int>(index));
                const std::string header = "Rotation Keyframe " + std::to_string(index);
                if (ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                {
                    float timeSeconds = keyframe.value("TimeSeconds", 0.0f);
                    ImGui::TextUnformatted("Time (Seconds)");
                    if (ImGui::DragFloat("##TimeSeconds", &timeSeconds, 0.01f))
                        keyframe["TimeSeconds"] = std::max(0.0f, timeSeconds);

                    float value = keyframe.value("Value", 0.0f);
                    ImGui::TextUnformatted("Value");
                    if (ImGui::DragFloat("##Value", &value, 0.1f))
                        keyframe["Value"] = value;

                    int interpolationIndex = InterpolationIndexFromJson(keyframe);
                    const char* interpolationNames[] = {"Step", "Linear"};
                    ImGui::TextUnformatted("Interpolation");
                    if (ImGui::Combo("##Interpolation", &interpolationIndex, interpolationNames, 2))
                        keyframe["Interpolation"] = JsonInterpolationName(interpolationIndex);

                    if (ImGui::Button("Remove Keyframe"))
                        removeIndex = static_cast<int32_t>(index);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }

            if (removeIndex >= 0)
                trackArray.erase(trackArray.begin() + removeIndex);
            SortTrackByTime(trackArray);
        }

        void DrawEventTrack(json& trackArray)
        {
            ImGui::SeparatorText("Event Track");
            if (ImGui::Button("Add Event Keyframe"))
            {
                trackArray.push_back({
                    {"TimeSeconds", 0.0f},
                    {"Name", "Event"},
                    {"StringPayload", ""},
                    {"FloatPayload", 0.0f},
                    {"IntegerPayload", 0},
                    {"BooleanPayload", false}
                });
            }

            int32_t removeIndex = -1;
            for (size_t index = 0; index < trackArray.size(); ++index)
            {
                auto& keyframe = trackArray[index];
                ImGui::PushID(static_cast<int>(index));
                const std::string header = "Event Keyframe " + std::to_string(index);
                if (ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                {
                    float timeSeconds = keyframe.value("TimeSeconds", 0.0f);
                    ImGui::TextUnformatted("Time (Seconds)");
                    if (ImGui::DragFloat("##TimeSeconds", &timeSeconds, 0.01f))
                        keyframe["TimeSeconds"] = std::max(0.0f, timeSeconds);

                    std::array<char, 128> nameBuffer{};
                    std::snprintf(nameBuffer.data(), nameBuffer.size(), "%s", keyframe.value("Name", std::string{}).c_str());
                    ImGui::TextUnformatted("Event Name");
                    if (ImGui::InputText("##EventName", nameBuffer.data(), nameBuffer.size()))
                        keyframe["Name"] = std::string(nameBuffer.data());

                    std::array<char, 256> stringPayloadBuffer{};
                    std::snprintf(stringPayloadBuffer.data(),
                                  stringPayloadBuffer.size(),
                                  "%s",
                                  keyframe.value("StringPayload", std::string{}).c_str());
                    ImGui::TextUnformatted("String Payload");
                    if (ImGui::InputText("##StringPayload", stringPayloadBuffer.data(), stringPayloadBuffer.size()))
                        keyframe["StringPayload"] = std::string(stringPayloadBuffer.data());

                    float floatPayload = keyframe.value("FloatPayload", 0.0f);
                    ImGui::TextUnformatted("Float Payload");
                    if (ImGui::DragFloat("##FloatPayload", &floatPayload, 0.01f))
                        keyframe["FloatPayload"] = floatPayload;

                    int integerPayload = keyframe.value("IntegerPayload", 0);
                    ImGui::TextUnformatted("Integer Payload");
                    if (ImGui::DragInt("##IntegerPayload", &integerPayload))
                        keyframe["IntegerPayload"] = integerPayload;

                    bool booleanPayload = keyframe.value("BooleanPayload", false);
                    ImGui::TextUnformatted("Boolean Payload");
                    if (ImGui::Checkbox("##BooleanPayload", &booleanPayload))
                        keyframe["BooleanPayload"] = booleanPayload;

                    if (ImGui::Button("Remove Keyframe"))
                        removeIndex = static_cast<int32_t>(index);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }

            if (removeIndex >= 0)
                trackArray.erase(trackArray.begin() + removeIndex);
            SortTrackByTime(trackArray);
        }
    }

    void Draw(bool& isOpen, const std::string& animationClipAssetKey, EditorUndoService* undoService)
    {
        if (!isOpen)
            return;

        if (!ImGui::Begin("Animation Timeline", &isOpen))
        {
            ImGui::End();
            return;
        }

        auto& state = GetTimelineEditorState();
        if (animationClipAssetKey.empty())
        {
            ImGui::TextDisabled("Select an Animation Clip asset to edit.");
            ImGui::End();
            return;
        }

        if (state.LoadedAssetKey != animationClipAssetKey)
        {
            state = TimelineEditorState{};
            state.LoadedAssetKey = animationClipAssetKey;
            if (!ResolveClipPath(animationClipAssetKey, state.ResolvedPath))
            {
                state.LoadFailed = true;
                state.StatusMessage = "Failed to resolve animation clip path.";
                state.StatusIsError = true;
            }
            else if (!LoadJsonFromPath(state.ResolvedPath, state.WorkingJson))
            {
                state.LoadFailed = true;
                state.StatusMessage = "Failed to load clip JSON.";
                state.StatusIsError = true;
            }
            else
            {
                EnsureClipSchemaDefaults(state.WorkingJson);
                state.AppliedJson = state.WorkingJson;
                state.Loaded = true;
                state.PreviewTimeSeconds = 0.0f;
            }
        }

        ImGui::Text("Clip: %s", EditorAssetNaming::GetAssetDisplayNameFromAssetKey(animationClipAssetKey).c_str());
        ImGui::TextDisabled("Asset Key: %s", animationClipAssetKey.c_str());
        if (!state.ResolvedPath.empty())
            ImGui::TextDisabled("Path: %s", state.ResolvedPath.string().c_str());
        ImGui::Separator();

        if (state.LoadFailed || !state.Loaded)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", state.StatusMessage.c_str());
            ImGui::End();
            return;
        }

        EnsureClipSchemaDefaults(state.WorkingJson);

        std::array<char, 256> clipNameBuffer{};
        std::snprintf(clipNameBuffer.data(), clipNameBuffer.size(), "%s", state.WorkingJson.value("Name", std::string{}).c_str());
        ImGui::TextUnformatted("Name");
        if (ImGui::InputText("##TimelineClipName", clipNameBuffer.data(), clipNameBuffer.size()))
            state.WorkingJson["Name"] = std::string(clipNameBuffer.data());

        bool loop = state.WorkingJson.value("Loop", true);
        ImGui::TextUnformatted("Loop");
        if (ImGui::Checkbox("##TimelineLoop", &loop))
            state.WorkingJson["Loop"] = loop;

        float durationSeconds = state.WorkingJson.value("DurationSeconds", 1.0f);
        DrawFloatField("Duration (Seconds)", durationSeconds, 0.01f);
        state.WorkingJson["DurationSeconds"] = std::max(0.0001f, durationSeconds);

        float samplesPerSecond = state.WorkingJson.value("SamplesPerSecond", 30.0f);
        DrawFloatField("Samples Per Second", samplesPerSecond, 0.25f);
        state.WorkingJson["SamplesPerSecond"] = std::max(1.0f, samplesPerSecond);

        const float maxPreviewTime = std::max(0.0001f, state.WorkingJson.value("DurationSeconds", 1.0f));
        const bool clipLoops = state.WorkingJson.value("Loop", true);
        const float previewSpeedMultiplier = 1.0f;

        if (state.IsPlaying && !state.IsPaused)
        {
            const float frameDelta = ImGui::GetIO().DeltaTime * previewSpeedMultiplier;
            state.PreviewTimeSeconds += frameDelta;
            if (state.PreviewTimeSeconds >= maxPreviewTime)
            {
                if (clipLoops)
                    state.PreviewTimeSeconds = std::fmod(state.PreviewTimeSeconds, maxPreviewTime);
                else
                {
                    state.PreviewTimeSeconds = maxPreviewTime;
                    state.IsPlaying = false;
                    state.IsPaused = false;
                }
            }
        }

        ImGui::SeparatorText("Preview");

        if (!state.IsPlaying)
        {
            if (ImGui::Button("Play##TimelinePlay", ImVec2(60.0f, 0.0f)))
            {
                state.IsPlaying = true;
                state.IsPaused = false;
                if (state.PreviewTimeSeconds >= maxPreviewTime - 0.001f)
                    state.PreviewTimeSeconds = 0.0f;
            }
        }
        else if (state.IsPaused)
        {
            if (ImGui::Button("Resume##TimelineResume", ImVec2(60.0f, 0.0f)))
                state.IsPaused = false;
        }
        else
        {
            if (ImGui::Button("Pause##TimelinePause", ImVec2(60.0f, 0.0f)))
                state.IsPaused = true;
        }

        ImGui::SameLine();
        if (ImGui::Button("Stop##TimelineStop", ImVec2(60.0f, 0.0f)))
        {
            state.IsPlaying = false;
            state.IsPaused = false;
            state.PreviewTimeSeconds = 0.0f;
        }

        ImGui::SameLine();
        if (ImGui::Button("|<##TimelineRewind", ImVec2(30.0f, 0.0f)))
        {
            state.PreviewTimeSeconds = 0.0f;
        }

        ImGui::SameLine();
        {
            const float frameStep = 1.0f / std::max(1.0f, state.WorkingJson.value("SamplesPerSecond", 30.0f));
            if (ImGui::Button(">|##TimelineStepFwd", ImVec2(30.0f, 0.0f)))
            {
                state.PreviewTimeSeconds = std::min(state.PreviewTimeSeconds + frameStep, maxPreviewTime);
            }
        }

        ImGui::SliderFloat("##PreviewTime", &state.PreviewTimeSeconds, 0.0f, maxPreviewTime, "%.3f s");
        if (ImGui::IsItemActive())
        {
            state.IsPlaying = false;
            state.IsPaused = false;
        }

        if (const json* sampledTexture = SampleStepJsonKeyframe(state.WorkingJson["SpriteTextureTrack"], state.PreviewTimeSeconds))
        {
            const std::string previewTextureKey = sampledTexture->contains("Texture") && (*sampledTexture)["Texture"].is_object()
                ? (*sampledTexture)["Texture"].value("key", std::string{})
                : sampledTexture->value("TextureKey", std::string{});
            const int32_t previewSubSpriteIndex = sampledTexture->contains("Texture") && (*sampledTexture)["Texture"].is_object()
                ? (*sampledTexture)["Texture"].value("subSpriteIndex", -1)
                : -1;
            const std::string previewTextureLabel = previewTextureKey.empty()
                ? std::string("None")
                : (EditorAssetNaming::GetAssetDisplayNameFromAssetKey(previewTextureKey) +
                   (previewSubSpriteIndex >= 0 ? ("#" + std::to_string(previewSubSpriteIndex)) : std::string{}));
            ImGui::Text("Preview Texture: %s", previewTextureLabel.c_str());
        }
        else
        {
            ImGui::TextDisabled("Preview Texture: none");
        }

        DrawSpriteSubRectTrack(state.WorkingJson["SpriteSubRectTrack"]);
        DrawSpriteTextureTrack(
            state.WorkingJson["SpriteTextureTrack"],
            state.WorkingJson["SpriteSubRectTrack"],
            state.WorkingJson.value("SamplesPerSecond", 30.0f));
        DrawVector3Track("Position Track", state.WorkingJson["PositionTrack"]);
        DrawVector3Track("Scale Track", state.WorkingJson["ScaleTrack"]);
        DrawFloatTrack(state.WorkingJson["RotationZTrack"]);
        DrawEventTrack(state.WorkingJson["EventTrack"]);

        const bool hasUnsavedChanges = (state.WorkingJson.dump() != state.AppliedJson.dump());
        ImGui::Separator();
        ImGui::BeginDisabled(!hasUnsavedChanges);
        if (ImGui::Button("Apply Changes", ImVec2(180.0f, 0.0f)))
        {
            if (ApplyPendingClipChanges(undoService, state, "Edit Animation Clip"))
            {
                // State updated by ApplyPendingClipChanges.
            }
            else
            {
                state.StatusMessage = "Failed to apply animation clip changes.";
                state.StatusIsError = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Revert Changes", ImVec2(180.0f, 0.0f)))
        {
            state.WorkingJson = state.AppliedJson;
            state.StatusMessage = "Reverted local animation clip edits.";
            state.StatusIsError = false;
        }
        ImGui::EndDisabled();

        if (!hasUnsavedChanges)
            ImGui::TextDisabled("No pending edits.");
        else
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "Pending edits not yet applied.");

        if (!state.StatusMessage.empty())
        {
            const ImVec4 messageColor = state.StatusIsError
                ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f)
                : ImVec4(0.6f, 1.0f, 0.6f, 1.0f);
            ImGui::TextColored(messageColor, "%s", state.StatusMessage.c_str());
        }

        ImGui::End();
    }

    bool ApplyPendingChanges(EditorUndoService* undoService)
    {
        auto& state = GetTimelineEditorState();
        return ApplyPendingClipChanges(undoService, state, "Auto Save Animation Clip");
    }

    bool TryGetActivePreview(ActivePreview& outPreview)
    {
        auto& state = GetTimelineEditorState();
        if (!state.Loaded || state.LoadFailed || state.LoadedAssetKey.empty())
            return false;

        outPreview.ClipAssetKey = state.LoadedAssetKey;
        outPreview.PreviewTimeSeconds = std::max(0.0f, state.PreviewTimeSeconds);
        outPreview.ClipDurationSeconds = std::max(0.0001f, state.WorkingJson.value("DurationSeconds", 1.0f));
        outPreview.IsPlaying = state.IsPlaying && !state.IsPaused;
        return true;
    }
}
