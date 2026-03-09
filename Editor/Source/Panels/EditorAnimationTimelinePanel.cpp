#include "EditorAnimationTimelinePanel.h"

#include "EditorPanelStyle.h"
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
#include <cstring>
#include <limits>
#include <map>
#include <set>
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

        // Dopesheet visual constants
        constexpr float kToolbarHeight = 30.0f;
        constexpr float kMetadataRowHeight = 26.0f;
        constexpr float kTrackRowHeight = 24.0f;
        constexpr float kTimeRulerHeight = 22.0f;
        constexpr float kPropertyListWidth = 160.0f;
        constexpr float kPixelsPerSecond = 200.0f;
        constexpr float kDiamondHalfSize = 5.0f;
        constexpr float kMinTimelineZoom = 0.2f;
        constexpr float kMaxTimelineZoom = 5.0f;
        constexpr float kInspectorHeight = 180.0f;

        constexpr int kTrackCount = 6;

        struct TrackDef
        {
            const char* DisplayName;
            const char* JsonKey;
        };

        constexpr TrackDef kTracks[kTrackCount] = {
            {"Sprite Sub-Rect", "SpriteSubRectTrack"},
            {"Sprite Texture",  "SpriteTextureTrack"},
            {"Position",        "PositionTrack"},
            {"Scale",           "ScaleTrack"},
            {"Rotation",        "RotationTrack"},
            {"Events",          "EventTrack"},
        };

        struct KeyframeId
        {
            int Track = -1;
            int Index = -1;

            bool operator<(const KeyframeId& o) const
            {
                if (Track != o.Track) return Track < o.Track;
                return Index < o.Index;
            }
            bool operator==(const KeyframeId& o) const { return Track == o.Track && Index == o.Index; }
            bool operator!=(const KeyframeId& o) const { return !(*this == o); }
        };

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

            float TimelineZoom = 1.0f;
            float TimelineScrollX = 0.0f;

            std::set<KeyframeId> SelectedKeyframes;
            KeyframeId PrimaryKeyframe;

            bool IsDraggingPlayhead = false;
            bool IsDraggingKeyframes = false;
            float DragAnchorTime = 0.0f;
            std::vector<std::pair<KeyframeId, float>> DragStartTimes;

            int ContextMenuTrack = -1;
            float ContextMenuTime = 0.0f;

            struct ClipboardEntry
            {
                int Track = -1;
                float RelativeTime = 0.0f;
                json KeyframeData;
            };
            std::vector<ClipboardEntry> Clipboard;

            bool PasteOverridePopupPending = false;
            std::vector<ClipboardEntry> PendingPasteEntries;
            float PendingPasteBaseTime = 0.0f;

            bool IsSelected(int track, int index) const
            {
                return SelectedKeyframes.count({track, index}) > 0;
            }

            void SelectOnly(int track, int index)
            {
                SelectedKeyframes.clear();
                SelectedKeyframes.insert({track, index});
                PrimaryKeyframe = {track, index};
            }

            void ToggleSelect(int track, int index)
            {
                KeyframeId id{track, index};
                if (SelectedKeyframes.count(id))
                    SelectedKeyframes.erase(id);
                else
                    SelectedKeyframes.insert(id);
                PrimaryKeyframe = id;
            }

            void AddToSelection(int track, int index)
            {
                SelectedKeyframes.insert({track, index});
                PrimaryKeyframe = {track, index};
            }

            void ClearSelection()
            {
                SelectedKeyframes.clear();
                PrimaryKeyframe = {-1, -1};
            }
        };

        TimelineEditorState& GetTimelineEditorState()
        {
            static TimelineEditorState state;
            return state;
        }

        // ---- Asset utility functions ----

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
            ensureArray("EventTrack");

            if (root.contains("RotationZTrack") && root["RotationZTrack"].is_array() &&
                (!root.contains("RotationTrack") || !root["RotationTrack"].is_array() || root["RotationTrack"].empty()))
            {
                json migrated = json::array();
                for (const auto& kf : root["RotationZTrack"])
                {
                    if (!kf.is_object()) continue;
                    const float z = kf.value("Value", 0.0f);
                    migrated.push_back({
                        {"TimeSeconds", kf.value("TimeSeconds", 0.0f)},
                        {"Value", {0.0f, 0.0f, z}},
                        {"Interpolation", kf.value("Interpolation", "Linear")}
                    });
                }
                root["RotationTrack"] = std::move(migrated);
                root.erase("RotationZTrack");
            }
            ensureArray("RotationTrack");
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

        // ---- Dopesheet helpers ----

        float TimeToScreenX(float timeSeconds, float originX, float scrollX, float zoom)
        {
            return originX + (timeSeconds * kPixelsPerSecond - scrollX) * zoom;
        }

        float ScreenXToTime(float screenX, float originX, float scrollX, float zoom)
        {
            return (((screenX - originX) / zoom) + scrollX) / kPixelsPerSecond;
        }

        float ComputeTickInterval(float zoom)
        {
            const float pixelsPerTick = 80.0f;
            float secondsPerTick = pixelsPerTick / (kPixelsPerSecond * zoom);
            if (secondsPerTick <= 0.0001f) secondsPerTick = 0.001f;

            const float magnitude = std::pow(10.0f, std::floor(std::log10(secondsPerTick)));
            const float normalized = secondsPerTick / magnitude;

            if (normalized < 2.0f) return magnitude;
            if (normalized < 5.0f) return magnitude * 2.0f;
            return magnitude * 5.0f;
        }

        int ComputeSubdivisions(float zoom)
        {
            const float tickInterval = ComputeTickInterval(zoom);
            const float tickPixels = tickInterval * kPixelsPerSecond * zoom;
            if (tickPixels > 400.0f) return 10;
            if (tickPixels > 200.0f) return 5;
            if (tickPixels > 100.0f) return 4;
            return 2;
        }

        float ComputeSnapInterval(float zoom)
        {
            const float tickInterval = ComputeTickInterval(zoom);
            return tickInterval / static_cast<float>(ComputeSubdivisions(zoom));
        }

        float SnapTimeToGrid(float timeSeconds, float zoom)
        {
            const float snapInterval = ComputeSnapInterval(zoom);
            return std::round(timeSeconds / snapInterval) * snapInterval;
        }

        bool CommitUndoSnapshot(EditorUndoService* undoService, TimelineEditorState& state, const char* label)
        {
            if (state.WorkingJson.dump() == state.AppliedJson.dump())
                return true;
            return ApplyPendingClipChanges(undoService, state, label);
        }

        void DrawDiamond(ImDrawList* dl, const ImVec2& center, float halfSize, ImU32 fillCol, ImU32 borderCol)
        {
            const ImVec2 top(center.x, center.y - halfSize);
            const ImVec2 right(center.x + halfSize, center.y);
            const ImVec2 bottom(center.x, center.y + halfSize);
            const ImVec2 left(center.x - halfSize, center.y);
            dl->AddQuadFilled(top, right, bottom, left, fillCol);
            dl->AddQuad(top, right, bottom, left, borderCol, 1.0f);
        }

        void HandleSpriteTextureDrop(json& textureTrack, json& subRectTrack, float samplesPerSecond,
                                     const ImGuiPayload* payload, const char* payloadType)
        {
            std::vector<DroppedSpriteEntry> droppedEntries;
            if (std::strcmp(payloadType, kSubSpritePayloadId) == 0 ||
                std::strcmp(payloadType, "ASSET_TEXTURE") == 0)
            {
                const char* key = static_cast<const char*>(payload->Data);
                if (key && key[0])
                {
                    DroppedSpriteEntry entry;
                    if (TryResolveDroppedSpriteEntry(key, entry))
                        droppedEntries.push_back(std::move(entry));
                }
            }
            else if (std::strcmp(payloadType, "ASSET_MULTI_KEYS") == 0)
            {
                const auto droppedKeys = FilterTextureAssetKeys(ParseAssetKeyListPayload(payload));
                for (const auto& key : droppedKeys)
                {
                    DroppedSpriteEntry entry;
                    if (TryResolveDroppedSpriteEntry(key, entry))
                        droppedEntries.push_back(std::move(entry));
                }
            }

            if (droppedEntries.empty()) return;

            const float frameStep = 1.0f / std::max(1.0f, samplesPerSecond);
            const float startTime = textureTrack.empty()
                ? 0.0f
                : (textureTrack.back().value("TimeSeconds", 0.0f) + frameStep);
            bool subRectMutated = false;

            for (size_t i = 0; i < droppedEntries.size(); ++i)
            {
                const float keyTime = startTime + static_cast<float>(i) * frameStep;
                const auto& entry = droppedEntries[i];
                textureTrack.push_back({
                    {"TimeSeconds", keyTime},
                    {"Texture", BuildTextureKeyframeTextureObject(entry)}
                });
                if (entry.HasSubRect)
                {
                    UpsertSubRectKeyframe(subRectTrack, keyTime, entry.UvMin, entry.UvMax);
                    subRectMutated = true;
                }
            }
            if (subRectMutated)
                SortTrackByTime(subRectTrack);
        }

        json MakeDefaultKeyframe(int trackIndex, float timeSeconds)
        {
            switch (trackIndex)
            {
            case 0: return {{"TimeSeconds", timeSeconds}, {"UvMin", {0.0f, 0.0f}}, {"UvMax", {1.0f, 1.0f}}};
            case 1: return {{"TimeSeconds", timeSeconds}, {"Texture", {{"key", ""}}}};
            case 2: return {{"TimeSeconds", timeSeconds}, {"Value", {0.0f, 0.0f, 0.0f}}, {"Interpolation", "Linear"}};
            case 3: return {{"TimeSeconds", timeSeconds}, {"Value", {0.0f, 0.0f, 0.0f}}, {"Interpolation", "Linear"}};
            case 4: return {{"TimeSeconds", timeSeconds}, {"Value", {0.0f, 0.0f, 0.0f}}, {"Interpolation", "Linear"}};
            case 5: return {{"TimeSeconds", timeSeconds}, {"Name", "Event"}, {"StringPayload", ""}, {"FloatPayload", 0.0f}, {"IntegerPayload", 0}, {"BooleanPayload", false}};
            default: return {{"TimeSeconds", timeSeconds}};
            }
        }

        void DrawVec2Field(const char* label, json& valueArray, float speed = 0.01f)
        {
            if (!valueArray.is_array() || valueArray.size() != 2)
                valueArray = json::array({0.0f, 0.0f});
            float value[2] = {valueArray[0].get<float>(), valueArray[1].get<float>()};
            if (EditorPanelStyle::DragFloatNWithAxisLabels(label, value, 2, speed))
                valueArray = json::array({value[0], value[1]});
        }

        void DrawVec3Field(const char* label, json& valueArray, float speed = 0.01f)
        {
            if (!valueArray.is_array() || valueArray.size() != 3)
                valueArray = json::array({0.0f, 0.0f, 0.0f});
            float value[3] = {valueArray[0].get<float>(), valueArray[1].get<float>(), valueArray[2].get<float>()};
            if (EditorPanelStyle::DragFloatNWithAxisLabels(label, value, 3, speed))
                valueArray = json::array({value[0], value[1], value[2]});
        }

        void DrawKeyframeInspector(TimelineEditorState& state, EditorUndoService* undoService)
        {
            const auto& pk = state.PrimaryKeyframe;
            if (pk.Track < 0 || pk.Track >= kTrackCount || pk.Index < 0 || state.SelectedKeyframes.empty())
            {
                ImGui::TextDisabled("Select a keyframe to edit its properties.");
                return;
            }

            auto& trackArray = state.WorkingJson[kTracks[pk.Track].JsonKey];
            if (!trackArray.is_array() || pk.Index >= static_cast<int>(trackArray.size()))
            {
                state.ClearSelection();
                ImGui::TextDisabled("Select a keyframe to edit its properties.");
                return;
            }

            auto& kf = trackArray[pk.Index];
            const size_t selCount = state.SelectedKeyframes.size();
            if (selCount > 1)
                ImGui::Text("%s - Keyframe %d  (%d selected)", kTracks[pk.Track].DisplayName, pk.Index, static_cast<int>(selCount));
            else
                ImGui::Text("%s - Keyframe %d", kTracks[pk.Track].DisplayName, pk.Index);

            static json s_InspectorPreSnapshot;
            static bool s_InspectorWasActive = false;

            bool anyWidgetActive = false;

            float timeSeconds = kf.value("TimeSeconds", 0.0f);
            if (ImGui::DragFloat("Time (s)", &timeSeconds, 0.001f, 0.0f, 0.0f, "%.4f"))
            {
                kf["TimeSeconds"] = std::max(0.0f, timeSeconds);
                SortTrackByTime(trackArray);
                const float target = std::max(0.0f, timeSeconds);
                for (int i = 0; i < static_cast<int>(trackArray.size()); ++i)
                {
                    if (std::abs(trackArray[i].value("TimeSeconds", -1.0f) - target) < 0.00001f)
                    {
                        state.SelectedKeyframes.erase(pk);
                        state.PrimaryKeyframe = {pk.Track, i};
                        state.SelectedKeyframes.insert(state.PrimaryKeyframe);
                        break;
                    }
                }
            }
            anyWidgetActive |= ImGui::IsItemActive();

            switch (pk.Track)
            {
            case 0:
                DrawVec2Field("UV Min", kf["UvMin"], 0.001f);
                anyWidgetActive |= ImGui::IsItemActive();
                DrawVec2Field("UV Max", kf["UvMax"], 0.001f);
                anyWidgetActive |= ImGui::IsItemActive();
                break;
            case 1:
            {
                if (!kf.contains("Texture") || !kf["Texture"].is_object())
                    kf["Texture"] = json::object();
                std::string texKey = kf["Texture"].value("key", std::string{});
                const int32_t subIdx = kf["Texture"].value("subSpriteIndex", -1);
                const std::string texLabel = texKey.empty()
                    ? std::string("None")
                    : (EditorAssetNaming::GetAssetDisplayNameFromAssetKey(texKey) +
                       (subIdx >= 0 ? ("#" + std::to_string(subIdx)) : std::string{}));
                ImGui::Text("Texture: %s", texLabel.c_str());

                ImGui::Button((texLabel + "##InspTexSlot").c_str(),
                    ImVec2(std::max(60.0f, ImGui::GetContentRegionAvail().x - 80.0f), 0.0f));
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kSubSpritePayloadId))
                    {
                        const char* key = static_cast<const char*>(p->Data);
                        if (key && key[0])
                        {
                            DroppedSpriteEntry entry;
                            if (TryResolveDroppedSpriteEntry(key, entry))
                            {
                                kf["Texture"]["key"] = entry.TextureKey;
                                if (entry.SubSpriteIndex >= 0) kf["Texture"]["subSpriteIndex"] = entry.SubSpriteIndex;
                                else kf["Texture"].erase("subSpriteIndex");
                                if (entry.HasSubRect)
                                {
                                    UpsertSubRectKeyframe(state.WorkingJson["SpriteSubRectTrack"],
                                        kf.value("TimeSeconds", 0.0f), entry.UvMin, entry.UvMax);
                                    SortTrackByTime(state.WorkingJson["SpriteSubRectTrack"]);
                                }
                                CommitUndoSnapshot(undoService, state, "Change Keyframe Texture");
                            }
                        }
                    }
                    else if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_TEXTURE"))
                    {
                        const char* key = static_cast<const char*>(p->Data);
                        if (key && key[0])
                        {
                            DroppedSpriteEntry entry;
                            if (TryResolveDroppedSpriteEntry(key, entry))
                            {
                                kf["Texture"]["key"] = entry.TextureKey;
                                if (entry.SubSpriteIndex >= 0) kf["Texture"]["subSpriteIndex"] = entry.SubSpriteIndex;
                                else kf["Texture"].erase("subSpriteIndex");
                                if (entry.HasSubRect)
                                {
                                    UpsertSubRectKeyframe(state.WorkingJson["SpriteSubRectTrack"],
                                        kf.value("TimeSeconds", 0.0f), entry.UvMin, entry.UvMax);
                                    SortTrackByTime(state.WorkingJson["SpriteSubRectTrack"]);
                                }
                                CommitUndoSnapshot(undoService, state, "Change Keyframe Texture");
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                ImGui::SameLine();
                if (ImGui::Button("...##InspTexPicker"))
                    ImGui::OpenPopup("InspTexPickerPopup");
                if (ImGui::BeginPopup("InspTexPickerPopup"))
                {
                    if (ImGui::Selectable("None##InspTexNone"))
                    {
                        kf["Texture"]["key"] = "";
                        kf["Texture"].erase("subSpriteIndex");
                        CommitUndoSnapshot(undoService, state, "Clear Keyframe Texture");
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::Separator();
                    const auto texKeys = BuildTexturePickerKeys();
                    for (const auto& key : texKeys)
                    {
                        const std::string display = EditorAssetNaming::GetAssetDisplayNameFromAssetKey(key);
                        if (ImGui::Selectable((display + "##InspTex_" + key).c_str(), texKey == key))
                        {
                            kf["Texture"]["key"] = key;
                            kf["Texture"].erase("subSpriteIndex");
                            CommitUndoSnapshot(undoService, state, "Change Keyframe Texture");
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    ImGui::EndPopup();
                }
                break;
            }
            case 2:
            case 3:
            case 4:
            {
                DrawVec3Field("Value", kf["Value"], pk.Track == 4 ? 0.1f : 0.01f);
                anyWidgetActive |= ImGui::IsItemActive();
                int interpIdx = InterpolationIndexFromJson(kf);
                const char* interpNames[] = {"Step", "Linear"};
                if (ImGui::Combo("Interpolation", &interpIdx, interpNames, 2))
                {
                    kf["Interpolation"] = JsonInterpolationName(interpIdx);
                    CommitUndoSnapshot(undoService, state, "Change Interpolation");
                }
                break;
            }
            case 5:
            {
                std::array<char, 128> nameBuf{};
                std::snprintf(nameBuf.data(), nameBuf.size(), "%s", kf.value("Name", std::string{}).c_str());
                if (ImGui::InputText("Event Name", nameBuf.data(), nameBuf.size()))
                    kf["Name"] = std::string(nameBuf.data());
                anyWidgetActive |= ImGui::IsItemActive();

                std::array<char, 256> strBuf{};
                std::snprintf(strBuf.data(), strBuf.size(), "%s", kf.value("StringPayload", std::string{}).c_str());
                if (ImGui::InputText("String", strBuf.data(), strBuf.size()))
                    kf["StringPayload"] = std::string(strBuf.data());
                anyWidgetActive |= ImGui::IsItemActive();

                float fp = kf.value("FloatPayload", 0.0f);
                if (ImGui::DragFloat("Float", &fp, 0.01f))
                    kf["FloatPayload"] = fp;
                anyWidgetActive |= ImGui::IsItemActive();

                int ip = kf.value("IntegerPayload", 0);
                if (ImGui::DragInt("Integer", &ip))
                    kf["IntegerPayload"] = ip;
                anyWidgetActive |= ImGui::IsItemActive();

                bool bp = kf.value("BooleanPayload", false);
                if (ImGui::Checkbox("Boolean", &bp))
                {
                    kf["BooleanPayload"] = bp;
                    CommitUndoSnapshot(undoService, state, "Change Event Boolean");
                }
                break;
            }
            default: break;
            }

            if (!s_InspectorWasActive && anyWidgetActive)
                s_InspectorPreSnapshot = state.AppliedJson;
            if (s_InspectorWasActive && !anyWidgetActive)
            {
                json savedApplied = state.AppliedJson;
                state.AppliedJson = s_InspectorPreSnapshot;
                CommitUndoSnapshot(undoService, state, "Edit Keyframe Property");
                s_InspectorPreSnapshot = json::object();
            }
            s_InspectorWasActive = anyWidgetActive;

            const char* delLabel = selCount > 1 ? "Delete Selected Keyframes" : "Delete Keyframe";
            if (ImGui::Button(delLabel))
            {
                std::map<int, std::vector<int>> byTrack;
                for (const auto& sel : state.SelectedKeyframes)
                    byTrack[sel.Track].push_back(sel.Index);
                for (auto& [t, indices] : byTrack)
                {
                    std::sort(indices.rbegin(), indices.rend());
                    auto& arr = state.WorkingJson[kTracks[t].JsonKey];
                    if (!arr.is_array()) continue;
                    for (int idx : indices)
                    {
                        if (idx < static_cast<int>(arr.size()))
                            arr.erase(arr.begin() + idx);
                    }
                }
                state.ClearSelection();
                CommitUndoSnapshot(undoService, state, "Delete Keyframes");
            }
        }
    }

    void Draw(bool& isOpen, const std::string& animationClipAssetKey, EditorUndoService* undoService, bool requestFocus)
    {
        if (!isOpen)
            return;

        EditorPanelStyle::PushPanelVisualStyle();
        if (requestFocus)
            ImGui::SetNextWindowFocus();
        if (!ImGui::Begin("Animation Timeline", &isOpen))
        {
            ImGui::End();
            EditorPanelStyle::PopPanelVisualStyle();
            return;
        }

        auto& state = GetTimelineEditorState();
        if (animationClipAssetKey.empty())
        {
            ImGui::TextDisabled("Select an Animation Clip asset to edit.");
            ImGui::End();
            EditorPanelStyle::PopPanelVisualStyle();
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

        if (state.LoadFailed || !state.Loaded)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", state.StatusMessage.c_str());
            ImGui::End();
            EditorPanelStyle::PopPanelVisualStyle();
            return;
        }

        EnsureClipSchemaDefaults(state.WorkingJson);

        const float maxPreviewTime = std::max(0.0001f, state.WorkingJson.value("DurationSeconds", 1.0f));
        const bool clipLoops = state.WorkingJson.value("Loop", true);
        const float samplesPerSecond = state.WorkingJson.value("SamplesPerSecond", 30.0f);

        // Auto-advance playback
        if (state.IsPlaying && !state.IsPaused)
        {
            state.PreviewTimeSeconds += ImGui::GetIO().DeltaTime;
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

        // ---- Toolbar ----
        {
            if (ImGui::Button("|<##Rewind", ImVec2(28, 0)))
                state.PreviewTimeSeconds = 0.0f;

            ImGui::SameLine();
            const float frameStep = 1.0f / std::max(1.0f, samplesPerSecond);
            if (ImGui::Button("<##StepBack", ImVec2(28, 0)))
                state.PreviewTimeSeconds = std::max(0.0f, state.PreviewTimeSeconds - frameStep);

            ImGui::SameLine();
            if (!state.IsPlaying || state.IsPaused)
            {
                if (ImGui::Button("Play##Transport", ImVec2(50, 0)))
                {
                    state.IsPlaying = true;
                    state.IsPaused = false;
                    if (state.PreviewTimeSeconds >= maxPreviewTime - 0.001f)
                        state.PreviewTimeSeconds = 0.0f;
                }
            }
            else
            {
                if (ImGui::Button("Pause##Transport", ImVec2(50, 0)))
                    state.IsPaused = true;
            }

            ImGui::SameLine();
            if (ImGui::Button(">##StepFwd", ImVec2(28, 0)))
                state.PreviewTimeSeconds += frameStep;

            ImGui::SameLine();
            if (ImGui::Button(">|##GoEnd", ImVec2(28, 0)))
                state.PreviewTimeSeconds = maxPreviewTime;

            ImGui::SameLine();
            if (ImGui::Button("Stop##Transport", ImVec2(40, 0)))
            {
                state.IsPlaying = false;
                state.IsPaused = false;
                state.PreviewTimeSeconds = 0.0f;
            }

            ImGui::SameLine();
            ImGui::SetNextItemWidth(80.0f);
            if (ImGui::DragFloat("##Time", &state.PreviewTimeSeconds, 0.001f, 0.0f, 0.0f, "%.3f s"))
            {
                if (state.PreviewTimeSeconds < 0.0f) state.PreviewTimeSeconds = 0.0f;
                state.IsPlaying = false;
                state.IsPaused = false;
            }

            ImGui::SameLine();
            const int currentFrame = static_cast<int>(state.PreviewTimeSeconds * samplesPerSecond);
            ImGui::Text("F:%d", currentFrame);
        }

        // ---- Metadata row ----
        {
            std::array<char, 128> nameBuf{};
            std::snprintf(nameBuf.data(), nameBuf.size(), "%s", state.WorkingJson.value("Name", std::string{}).c_str());
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::InputText("##ClipName", nameBuf.data(), nameBuf.size()))
                state.WorkingJson["Name"] = std::string(nameBuf.data());
            if (ImGui::IsItemDeactivatedAfterEdit())
                CommitUndoSnapshot(undoService, state, "Rename Clip");

            ImGui::SameLine();
            float dur = state.WorkingJson.value("DurationSeconds", 1.0f);
            ImGui::SetNextItemWidth(70.0f);
            if (ImGui::DragFloat("Dur##Duration", &dur, 0.01f, 0.0001f, 0.0f, "%.3f"))
                state.WorkingJson["DurationSeconds"] = std::max(0.0001f, dur);
            if (ImGui::IsItemDeactivatedAfterEdit())
                CommitUndoSnapshot(undoService, state, "Change Clip Duration");

            ImGui::SameLine();
            float sps = state.WorkingJson.value("SamplesPerSecond", 30.0f);
            ImGui::SetNextItemWidth(60.0f);
            if (ImGui::DragFloat("SPS##Samples", &sps, 0.25f, 1.0f, 0.0f, "%.0f"))
                state.WorkingJson["SamplesPerSecond"] = std::max(1.0f, sps);
            if (ImGui::IsItemDeactivatedAfterEdit())
                CommitUndoSnapshot(undoService, state, "Change Samples Per Second");

            ImGui::SameLine();
            bool loop = state.WorkingJson.value("Loop", true);
            if (ImGui::Checkbox("Loop", &loop))
            {
                state.WorkingJson["Loop"] = loop;
                CommitUndoSnapshot(undoService, state, "Toggle Loop");
            }
        }

        ImGui::Separator();

        // Calculate layout
        const float footerHeight = 60.0f;
        const float inspectorReserve = kInspectorHeight + 8.0f;
        const float mainAreaHeight = std::max(80.0f,
            ImGui::GetContentRegionAvail().y - footerHeight - inspectorReserve);
        const float totalTracksHeight = kTrackCount * kTrackRowHeight;

        // ---- Main area: property list + timeline ----
        ImGui::BeginChild("##DopesheetMain", ImVec2(0.0f, mainAreaHeight), ImGuiChildFlags_None);
        {
            // Property list (left)
            ImGui::BeginChild("##PropList", ImVec2(kPropertyListWidth, 0.0f), ImGuiChildFlags_Border);
            {
                // Empty header row aligned with time ruler
                ImGui::Dummy(ImVec2(0.0f, kTimeRulerHeight));
                ImGui::Separator();

                for (int t = 0; t < kTrackCount; ++t)
                {
                    const auto& trackArr = state.WorkingJson[kTracks[t].JsonKey];
                    const size_t kfCount = trackArr.is_array() ? trackArr.size() : 0;

                    ImGui::PushID(t);

                    char label[128];
                    std::snprintf(label, sizeof(label), "%s (%zu)", kTracks[t].DisplayName, kfCount);

                    bool hasTrackSelected = false;
                    for (const auto& sel : state.SelectedKeyframes)
                        if (sel.Track == t) { hasTrackSelected = true; break; }
                    if (ImGui::Selectable(label, hasTrackSelected, 0, ImVec2(0.0f, kTrackRowHeight - 2.0f)))
                    {
                        state.ClearSelection();
                    }

                    // Sprite Texture track: drop target for textures
                    if (t == 1 && ImGui::BeginDragDropTarget())
                    {
                        const char* payloadTypes[] = {kSubSpritePayloadId, "ASSET_TEXTURE", "ASSET_MULTI_KEYS"};
                        for (const char* ptype : payloadTypes)
                        {
                            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(ptype))
                            {
                                HandleSpriteTextureDrop(
                                    state.WorkingJson["SpriteTextureTrack"],
                                    state.WorkingJson["SpriteSubRectTrack"],
                                    samplesPerSecond, p, ptype);
                                SortTrackByTime(state.WorkingJson["SpriteTextureTrack"]);
                                CommitUndoSnapshot(undoService, state, "Drop Sprite Texture");
                                break;
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

                    ImGui::PopID();
                }
            }
            ImGui::EndChild();

            ImGui::SameLine();

            // Timeline (right)
            ImGui::BeginChild("##Timeline", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            {
                const ImVec2 tlOrigin = ImGui::GetCursorScreenPos();
                const ImVec2 tlSize = ImGui::GetContentRegionAvail();

                if (tlSize.x > 1.0f && tlSize.y > 1.0f)
                {
                    ImGui::InvisibleButton("##tlCanvas", tlSize);
                    const bool tlHovered = ImGui::IsItemHovered();
                    const ImVec2 mouseScreen = ImGui::GetIO().MousePos;

                    // Drag-drop textures onto the timeline canvas to auto-create keyframes
                    if (ImGui::BeginDragDropTarget())
                    {
                        const float dropTime = std::max(0.0f,
                            SnapTimeToGrid(
                                ScreenXToTime(mouseScreen.x, tlOrigin.x, state.TimelineScrollX, state.TimelineZoom),
                                state.TimelineZoom));
                        const char* payloadTypes[] = {kSubSpritePayloadId, "ASSET_TEXTURE", "ASSET_MULTI_KEYS"};
                        for (const char* ptype : payloadTypes)
                        {
                            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(ptype))
                            {
                                auto& texTrack = state.WorkingJson["SpriteTextureTrack"];
                                auto& subRectTrack = state.WorkingJson["SpriteSubRectTrack"];

                                std::vector<DroppedSpriteEntry> droppedEntries;
                                if (std::strcmp(ptype, kSubSpritePayloadId) == 0 ||
                                    std::strcmp(ptype, "ASSET_TEXTURE") == 0)
                                {
                                    const char* key = static_cast<const char*>(p->Data);
                                    if (key && key[0])
                                    {
                                        DroppedSpriteEntry entry;
                                        if (TryResolveDroppedSpriteEntry(key, entry))
                                            droppedEntries.push_back(std::move(entry));
                                    }
                                }
                                else if (std::strcmp(ptype, "ASSET_MULTI_KEYS") == 0)
                                {
                                    const auto droppedKeys = FilterTextureAssetKeys(ParseAssetKeyListPayload(p));
                                    for (const auto& key : droppedKeys)
                                    {
                                        DroppedSpriteEntry entry;
                                        if (TryResolveDroppedSpriteEntry(key, entry))
                                            droppedEntries.push_back(std::move(entry));
                                    }
                                }

                                if (!droppedEntries.empty())
                                {
                                    const float frameStep = 1.0f / std::max(1.0f, samplesPerSecond);
                                    for (size_t i = 0; i < droppedEntries.size(); ++i)
                                    {
                                        const float keyTime = dropTime + static_cast<float>(i) * frameStep;
                                        const auto& entry = droppedEntries[i];
                                        texTrack.push_back({
                                            {"TimeSeconds", keyTime},
                                            {"Texture", BuildTextureKeyframeTextureObject(entry)}
                                        });
                                        if (entry.HasSubRect)
                                        {
                                            UpsertSubRectKeyframe(subRectTrack, keyTime, entry.UvMin, entry.UvMax);
                                        }
                                    }
                                    SortTrackByTime(texTrack);
                                    SortTrackByTime(subRectTrack);
                                    CommitUndoSnapshot(undoService, state, "Drop Textures On Timeline");
                                }
                                break;
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

                    // Zoom with scroll wheel
                    if (tlHovered && std::abs(ImGui::GetIO().MouseWheel) > 0.0f)
                    {
                        const float oldZoom = state.TimelineZoom;
                        state.TimelineZoom = std::clamp(
                            state.TimelineZoom * (1.0f + ImGui::GetIO().MouseWheel * 0.1f),
                            kMinTimelineZoom, kMaxTimelineZoom);
                        const float mouseRelX = mouseScreen.x - tlOrigin.x;
                        state.TimelineScrollX += mouseRelX * (1.0f / state.TimelineZoom - 1.0f / oldZoom);
                    }

                    // Pan with middle mouse
                    if (tlHovered && ImGui::IsMouseDown(ImGuiMouseButton_Middle))
                    {
                        state.TimelineScrollX -= ImGui::GetIO().MouseDelta.x / state.TimelineZoom;
                    }

                    const float rulerTop = tlOrigin.y;
                    const float tracksTop = rulerTop + kTimeRulerHeight;
                    const float zoom = state.TimelineZoom;

                    // Playhead dragging on ruler
                    const bool mouseInRuler = mouseScreen.y >= rulerTop &&
                        mouseScreen.y < tracksTop &&
                        mouseScreen.x >= tlOrigin.x &&
                        mouseScreen.x <= tlOrigin.x + tlSize.x;

                    if (mouseInRuler && tlHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    {
                        state.IsDraggingPlayhead = true;
                        state.PreviewTimeSeconds = std::max(
                            ScreenXToTime(mouseScreen.x, tlOrigin.x, state.TimelineScrollX, zoom),
                            0.0f);
                        state.IsPlaying = false;
                        state.IsPaused = false;
                    }

                    if (state.IsDraggingPlayhead)
                    {
                        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                        {
                            state.PreviewTimeSeconds = std::max(
                                ScreenXToTime(mouseScreen.x, tlOrigin.x, state.TimelineScrollX, zoom),
                                0.0f);
                        }
                        else
                        {
                            state.IsDraggingPlayhead = false;
                        }
                    }

                    // Keyframe interaction (click in tracks area)
                    const bool mouseInTracks = mouseScreen.y >= tracksTop &&
                        mouseScreen.y < tracksTop + totalTracksHeight &&
                        mouseScreen.x >= tlOrigin.x &&
                        mouseScreen.x <= tlOrigin.x + tlSize.x;

                    if (mouseInTracks && tlHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                        !state.IsDraggingPlayhead)
                    {
                        const int trackHit = static_cast<int>((mouseScreen.y - tracksTop) / kTrackRowHeight);
                        int kfHit = -1;

                        if (trackHit >= 0 && trackHit < kTrackCount)
                        {
                            const auto& trackArr = state.WorkingJson[kTracks[trackHit].JsonKey];
                            if (trackArr.is_array())
                            {
                                for (int k = 0; k < static_cast<int>(trackArr.size()); ++k)
                                {
                                    const float kfTime = trackArr[k].value("TimeSeconds", 0.0f);
                                    const float kfX = TimeToScreenX(kfTime, tlOrigin.x, state.TimelineScrollX, zoom);
                                    if (std::abs(mouseScreen.x - kfX) <= kDiamondHalfSize + 3.0f)
                                    {
                                        kfHit = k;
                                        break;
                                    }
                                }
                            }
                        }

                        if (kfHit >= 0)
                        {
                            const bool ctrl = ImGui::GetIO().KeyCtrl;
                            const bool shift = ImGui::GetIO().KeyShift;

                            if (ctrl)
                                state.ToggleSelect(trackHit, kfHit);
                            else if (shift)
                                state.AddToSelection(trackHit, kfHit);
                            else if (!state.IsSelected(trackHit, kfHit))
                                state.SelectOnly(trackHit, kfHit);
                            else
                                state.PrimaryKeyframe = {trackHit, kfHit};

                            state.IsDraggingKeyframes = true;
                            state.DragAnchorTime =
                                state.WorkingJson[kTracks[trackHit].JsonKey][kfHit].value("TimeSeconds", 0.0f);
                            state.DragStartTimes.clear();
                            for (const auto& sel : state.SelectedKeyframes)
                            {
                                const auto& arr = state.WorkingJson[kTracks[sel.Track].JsonKey];
                                if (arr.is_array() && sel.Index < static_cast<int>(arr.size()))
                                    state.DragStartTimes.push_back({sel, arr[sel.Index].value("TimeSeconds", 0.0f)});
                            }
                        }
                        else
                        {
                            state.ClearSelection();
                        }
                    }

                    // Multi-keyframe dragging with snap-to-grid
                    if (state.IsDraggingKeyframes && !state.SelectedKeyframes.empty())
                    {
                        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                            ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f))
                        {
                            const float cursorTime = ScreenXToTime(mouseScreen.x, tlOrigin.x, state.TimelineScrollX, zoom);
                            const float snappedCursor = SnapTimeToGrid(cursorTime, zoom);
                            const float snappedAnchor = SnapTimeToGrid(state.DragAnchorTime, zoom);
                            const float delta = snappedCursor - snappedAnchor;

                            for (const auto& [kfId, origTime] : state.DragStartTimes)
                            {
                                auto& trackArr = state.WorkingJson[kTracks[kfId.Track].JsonKey];
                                if (trackArr.is_array() && kfId.Index < static_cast<int>(trackArr.size()))
                                {
                                    const float rawTime = origTime + delta;
                                    const float newTime = std::max(SnapTimeToGrid(rawTime, zoom), 0.0f);
                                    trackArr[kfId.Index]["TimeSeconds"] = newTime;
                                }
                            }
                        }
                        else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
                        {
                            std::set<int> affectedTracks;
                            for (const auto& sel : state.SelectedKeyframes)
                                affectedTracks.insert(sel.Track);

                            for (int t : affectedTracks)
                            {
                                auto& trackArr = state.WorkingJson[kTracks[t].JsonKey];
                                SortTrackByTime(trackArr);
                            }

                            std::set<KeyframeId> newSelection;
                            KeyframeId newPrimary = {-1, -1};
                            for (const auto& [kfId, origTime] : state.DragStartTimes)
                            {
                                auto& trackArr = state.WorkingJson[kTracks[kfId.Track].JsonKey];
                                if (!trackArr.is_array()) continue;
                                const float cursorTime = ScreenXToTime(mouseScreen.x, tlOrigin.x, state.TimelineScrollX, zoom);
                                const float snappedCursor = SnapTimeToGrid(cursorTime, zoom);
                                const float snappedAnchor = SnapTimeToGrid(state.DragAnchorTime, zoom);
                                const float rawTime = origTime + (snappedCursor - snappedAnchor);
                                const float target = std::max(SnapTimeToGrid(rawTime, zoom), 0.0f);
                                for (int i = 0; i < static_cast<int>(trackArr.size()); ++i)
                                {
                                    if (std::abs(trackArr[i].value("TimeSeconds", -1.0f) - target) < 0.00001f)
                                    {
                                        newSelection.insert({kfId.Track, i});
                                        if (kfId == state.PrimaryKeyframe)
                                            newPrimary = {kfId.Track, i};
                                        break;
                                    }
                                }
                            }

                            state.SelectedKeyframes = newSelection;
                            if (newPrimary.Track >= 0)
                                state.PrimaryKeyframe = newPrimary;
                            else if (!newSelection.empty())
                                state.PrimaryKeyframe = *newSelection.begin();

                            state.IsDraggingKeyframes = false;
                            state.DragStartTimes.clear();

                            CommitUndoSnapshot(undoService, state, "Move Keyframes");
                        }
                    }

                    // Delete key removes selected keyframes
                    if (tlHovered && !state.SelectedKeyframes.empty() &&
                        ImGui::IsKeyPressed(ImGuiKey_Delete, false))
                    {
                        std::map<int, std::vector<int>> byTrack;
                        for (const auto& sel : state.SelectedKeyframes)
                            byTrack[sel.Track].push_back(sel.Index);
                        for (auto& [t, indices] : byTrack)
                        {
                            std::sort(indices.rbegin(), indices.rend());
                            auto& arr = state.WorkingJson[kTracks[t].JsonKey];
                            if (!arr.is_array()) continue;
                            for (int idx : indices)
                            {
                                if (idx < static_cast<int>(arr.size()))
                                    arr.erase(arr.begin() + idx);
                            }
                        }
                        state.ClearSelection();
                        CommitUndoSnapshot(undoService, state, "Delete Keyframes");
                    }

                    // Ctrl+C: copy selected keyframes
                    if (tlHovered && !state.SelectedKeyframes.empty() &&
                        ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false))
                    {
                        state.Clipboard.clear();
                        float earliestTime = std::numeric_limits<float>::max();
                        for (const auto& sel : state.SelectedKeyframes)
                        {
                            const auto& arr = state.WorkingJson[kTracks[sel.Track].JsonKey];
                            if (arr.is_array() && sel.Index < static_cast<int>(arr.size()))
                            {
                                const float t = arr[sel.Index].value("TimeSeconds", 0.0f);
                                if (t < earliestTime) earliestTime = t;
                            }
                        }
                        for (const auto& sel : state.SelectedKeyframes)
                        {
                            const auto& arr = state.WorkingJson[kTracks[sel.Track].JsonKey];
                            if (arr.is_array() && sel.Index < static_cast<int>(arr.size()))
                            {
                                TimelineEditorState::ClipboardEntry entry;
                                entry.Track = sel.Track;
                                entry.RelativeTime = arr[sel.Index].value("TimeSeconds", 0.0f) - earliestTime;
                                entry.KeyframeData = arr[sel.Index];
                                state.Clipboard.push_back(std::move(entry));
                            }
                        }
                        state.StatusMessage = "Copied " + std::to_string(state.Clipboard.size()) + " keyframe(s).";
                        state.StatusIsError = false;
                    }

                    // Ctrl+V: paste keyframes at playhead position
                    if (tlHovered && !state.Clipboard.empty() &&
                        ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false))
                    {
                        const float pasteBase = SnapTimeToGrid(state.PreviewTimeSeconds, zoom);

                        bool anyConflict = false;
                        for (const auto& clipEntry : state.Clipboard)
                        {
                            if (clipEntry.Track < 0 || clipEntry.Track >= kTrackCount) continue;
                            const auto& arr = state.WorkingJson[kTracks[clipEntry.Track].JsonKey];
                            if (!arr.is_array()) continue;
                            const float desiredTime = SnapTimeToGrid(pasteBase + clipEntry.RelativeTime, zoom);
                            for (const auto& existing : arr)
                            {
                                if (std::abs(existing.value("TimeSeconds", -1.0f) - desiredTime) < 0.00001f)
                                {
                                    anyConflict = true;
                                    break;
                                }
                            }
                            if (anyConflict) break;
                        }

                        if (anyConflict)
                        {
                            state.PendingPasteEntries = state.Clipboard;
                            state.PendingPasteBaseTime = pasteBase;
                            state.PasteOverridePopupPending = true;
                        }
                        else
                        {
                            state.ClearSelection();
                            for (const auto& clipEntry : state.Clipboard)
                            {
                                if (clipEntry.Track < 0 || clipEntry.Track >= kTrackCount) continue;
                                auto& arr = state.WorkingJson[kTracks[clipEntry.Track].JsonKey];
                                if (!arr.is_array()) arr = json::array();

                                json newKf = clipEntry.KeyframeData;
                                newKf["TimeSeconds"] = SnapTimeToGrid(pasteBase + clipEntry.RelativeTime, zoom);
                                arr.push_back(newKf);
                                SortTrackByTime(arr);

                                const float ft = newKf["TimeSeconds"].get<float>();
                                for (int i = 0; i < static_cast<int>(arr.size()); ++i)
                                {
                                    if (std::abs(arr[i].value("TimeSeconds", -1.0f) - ft) < 0.00001f)
                                    {
                                        state.AddToSelection(clipEntry.Track, i);
                                        break;
                                    }
                                }
                            }
                            CommitUndoSnapshot(undoService, state, "Paste Keyframes");
                            state.StatusMessage = "Pasted " + std::to_string(state.Clipboard.size()) + " keyframe(s).";
                            state.StatusIsError = false;
                        }
                    }

                    // Paste override confirmation popup
                    if (state.PasteOverridePopupPending)
                    {
                        ImGui::OpenPopup("##PasteOverride");
                        state.PasteOverridePopupPending = false;
                    }
                    if (ImGui::BeginPopupModal("##PasteOverride", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
                    {
                        ImGui::Text("Pasting here will overwrite existing keyframes.");
                        ImGui::Text("Do you want to replace them?");
                        ImGui::Separator();

                        if (ImGui::Button("Replace", ImVec2(100, 0)))
                        {
                            state.ClearSelection();
                            for (const auto& clipEntry : state.PendingPasteEntries)
                            {
                                if (clipEntry.Track < 0 || clipEntry.Track >= kTrackCount) continue;
                                auto& arr = state.WorkingJson[kTracks[clipEntry.Track].JsonKey];
                                if (!arr.is_array()) arr = json::array();

                                const float desiredTime = SnapTimeToGrid(state.PendingPasteBaseTime + clipEntry.RelativeTime, zoom);

                                for (int i = static_cast<int>(arr.size()) - 1; i >= 0; --i)
                                {
                                    if (std::abs(arr[i].value("TimeSeconds", -1.0f) - desiredTime) < 0.00001f)
                                    {
                                        arr.erase(arr.begin() + i);
                                        break;
                                    }
                                }

                                json newKf = clipEntry.KeyframeData;
                                newKf["TimeSeconds"] = desiredTime;
                                arr.push_back(newKf);
                                SortTrackByTime(arr);

                                for (int i = 0; i < static_cast<int>(arr.size()); ++i)
                                {
                                    if (std::abs(arr[i].value("TimeSeconds", -1.0f) - desiredTime) < 0.00001f)
                                    {
                                        state.AddToSelection(clipEntry.Track, i);
                                        break;
                                    }
                                }
                            }
                            state.PendingPasteEntries.clear();
                            CommitUndoSnapshot(undoService, state, "Paste Keyframes (Replace)");
                            state.StatusMessage = "Pasted and replaced keyframe(s).";
                            state.StatusIsError = false;
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Cancel", ImVec2(100, 0)))
                        {
                            state.PendingPasteEntries.clear();
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndPopup();
                    }

                    // Right-click: add/delete keyframe (capture position at click time)
                    if (mouseInTracks && tlHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                    {
                        state.ContextMenuTrack = static_cast<int>((mouseScreen.y - tracksTop) / kTrackRowHeight);
                        state.ContextMenuTime = std::max(0.0f,
                            SnapTimeToGrid(
                                ScreenXToTime(mouseScreen.x, tlOrigin.x, state.TimelineScrollX, zoom),
                                zoom));
                        ImGui::OpenPopup("##TrackCtx");
                    }

                    if (ImGui::BeginPopup("##TrackCtx"))
                    {
                        const int ctxTrack = state.ContextMenuTrack;
                        const float ctxTime = state.ContextMenuTime;

                        if (ctxTrack >= 0 && ctxTrack < kTrackCount)
                        {
                            ImGui::Text("%s", kTracks[ctxTrack].DisplayName);
                            ImGui::Separator();
                            if (ImGui::MenuItem("Add Keyframe"))
                            {
                                auto& trackArr = state.WorkingJson[kTracks[ctxTrack].JsonKey];
                                trackArr.push_back(MakeDefaultKeyframe(ctxTrack, ctxTime));
                                SortTrackByTime(trackArr);
                                for (int i = 0; i < static_cast<int>(trackArr.size()); ++i)
                                {
                                    if (std::abs(trackArr[i].value("TimeSeconds", -1.0f) - ctxTime) < 0.0001f)
                                    {
                                        state.SelectOnly(ctxTrack, i);
                                        break;
                                    }
                                }
                                CommitUndoSnapshot(undoService, state, "Add Keyframe");
                            }
                            if (!state.SelectedKeyframes.empty())
                            {
                                const size_t selCount = state.SelectedKeyframes.size();
                                const char* delLabel = selCount > 1 ? "Delete Selected Keyframes" : "Delete Selected Keyframe";
                                if (ImGui::MenuItem(delLabel))
                                {
                                    std::map<int, std::vector<int>> byTrack;
                                    for (const auto& sel : state.SelectedKeyframes)
                                        byTrack[sel.Track].push_back(sel.Index);
                                    for (auto& [t2, indices] : byTrack)
                                    {
                                        std::sort(indices.rbegin(), indices.rend());
                                        auto& arr = state.WorkingJson[kTracks[t2].JsonKey];
                                        if (!arr.is_array()) continue;
                                        for (int idx : indices)
                                        {
                                            if (idx < static_cast<int>(arr.size()))
                                                arr.erase(arr.begin() + idx);
                                        }
                                    }
                                    state.ClearSelection();
                                    CommitUndoSnapshot(undoService, state, "Delete Keyframes");
                                }
                            }
                        }
                        ImGui::EndPopup();
                    }

                    // ---- Render timeline ----
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    const ImVec2 tlMax(tlOrigin.x + tlSize.x, tlOrigin.y + tlSize.y);
                    dl->PushClipRect(tlOrigin, tlMax, true);

                    // Background
                    dl->AddRectFilled(tlOrigin, tlMax, IM_COL32(40, 40, 40, 255));

                    // Track row backgrounds (alternating)
                    for (int t = 0; t < kTrackCount; ++t)
                    {
                        const float rowY = tracksTop + t * kTrackRowHeight;
                        const ImU32 rowCol = (t % 2 == 0)
                            ? IM_COL32(45, 45, 45, 255)
                            : IM_COL32(50, 50, 50, 255);
                        dl->AddRectFilled(
                            ImVec2(tlOrigin.x, rowY),
                            ImVec2(tlMax.x, rowY + kTrackRowHeight),
                            rowCol);
                    }

                    // Time ruler background
                    dl->AddRectFilled(
                        ImVec2(tlOrigin.x, rulerTop),
                        ImVec2(tlMax.x, tracksTop),
                        IM_COL32(55, 55, 55, 255));

                    // Time ruler ticks and labels
                    {
                        const float tickInterval = ComputeTickInterval(zoom);
                        const int subdivs = ComputeSubdivisions(zoom);
                        const float subInterval = tickInterval / static_cast<float>(subdivs);
                        const float startTime = std::max(0.0f,
                            ScreenXToTime(tlOrigin.x, tlOrigin.x, state.TimelineScrollX, zoom));
                        const float endTime =
                            ScreenXToTime(tlMax.x, tlOrigin.x, state.TimelineScrollX, zoom);

                        const int startTickIndex = static_cast<int>(std::floor(startTime / subInterval));
                        const int endTickIndex = static_cast<int>(std::ceil(endTime / subInterval));
                        for (int tickIndex = startTickIndex; tickIndex <= endTickIndex; ++tickIndex)
                        {
                            const float t = static_cast<float>(tickIndex) * subInterval;
                            if (t < 0.0f) continue;
                            const float x = TimeToScreenX(t, tlOrigin.x, state.TimelineScrollX, zoom);
                            if (x < tlOrigin.x || x > tlMax.x) continue;

                            const bool isMajor = std::abs(std::fmod(t, tickInterval)) < subInterval * 0.1f;
                            const float halfInterval = tickInterval * 0.5f;
                            const bool isHalf = !isMajor &&
                                std::abs(std::fmod(t + halfInterval * 0.5f, halfInterval)) < subInterval * 0.1f;

                            if (isMajor)
                            {
                                dl->AddLine(
                                    ImVec2(x, rulerTop + 2.0f),
                                    ImVec2(x, tracksTop),
                                    IM_COL32(180, 180, 180, 255));

                                char tickLabel[32];
                                if (tickInterval >= 1.0f)
                                    std::snprintf(tickLabel, sizeof(tickLabel), "%.0fs", t);
                                else if (tickInterval >= 0.1f)
                                    std::snprintf(tickLabel, sizeof(tickLabel), "%.1fs", t);
                                else if (tickInterval >= 0.01f)
                                    std::snprintf(tickLabel, sizeof(tickLabel), "%.2fs", t);
                                else
                                    std::snprintf(tickLabel, sizeof(tickLabel), "%.3fs", t);
                                dl->AddText(ImVec2(x + 2.0f, rulerTop + 1.0f),
                                    IM_COL32(200, 200, 200, 255), tickLabel);

                                dl->AddLine(
                                    ImVec2(x, tracksTop),
                                    ImVec2(x, tracksTop + totalTracksHeight),
                                    IM_COL32(60, 60, 60, 255));
                            }
                            else if (isHalf)
                            {
                                dl->AddLine(
                                    ImVec2(x, rulerTop + kTimeRulerHeight * 0.45f),
                                    ImVec2(x, tracksTop),
                                    IM_COL32(120, 120, 120, 200));
                                dl->AddLine(
                                    ImVec2(x, tracksTop),
                                    ImVec2(x, tracksTop + totalTracksHeight),
                                    IM_COL32(48, 48, 48, 200));
                            }
                            else
                            {
                                dl->AddLine(
                                    ImVec2(x, rulerTop + kTimeRulerHeight * 0.65f),
                                    ImVec2(x, tracksTop),
                                    IM_COL32(90, 90, 90, 150));
                                dl->AddLine(
                                    ImVec2(x, tracksTop),
                                    ImVec2(x, tracksTop + totalTracksHeight),
                                    IM_COL32(44, 44, 44, 120));
                            }
                        }
                    }

                    // Duration end marker and grayed-out beyond-duration area
                    {
                        const float durX = TimeToScreenX(maxPreviewTime, tlOrigin.x, state.TimelineScrollX, zoom);
                        if (durX < tlMax.x)
                        {
                            const float grayLeft = std::max(durX, tlOrigin.x);
                            dl->AddRectFilled(
                                ImVec2(grayLeft, rulerTop),
                                ImVec2(tlMax.x, tracksTop),
                                IM_COL32(0, 0, 0, 50));
                            dl->AddRectFilled(
                                ImVec2(grayLeft, tracksTop),
                                ImVec2(tlMax.x, tracksTop + totalTracksHeight),
                                IM_COL32(0, 0, 0, 80));
                        }
                        if (durX >= tlOrigin.x && durX <= tlMax.x)
                        {
                            dl->AddLine(
                                ImVec2(durX, rulerTop),
                                ImVec2(durX, tracksTop + totalTracksHeight),
                                IM_COL32(200, 80, 80, 160), 2.0f);
                        }
                    }

                    // Keyframe diamonds (dimmed if beyond duration)
                    for (int t = 0; t < kTrackCount; ++t)
                    {
                        const float rowCenterY = tracksTop + t * kTrackRowHeight + kTrackRowHeight * 0.5f;
                        const auto& trackArr = state.WorkingJson[kTracks[t].JsonKey];
                        if (!trackArr.is_array()) continue;

                        for (int k = 0; k < static_cast<int>(trackArr.size()); ++k)
                        {
                            const float kfTime = trackArr[k].value("TimeSeconds", 0.0f);
                            const float kfX = TimeToScreenX(kfTime, tlOrigin.x, state.TimelineScrollX, zoom);
                            if (kfX < tlOrigin.x - kDiamondHalfSize || kfX > tlMax.x + kDiamondHalfSize)
                                continue;

                            const bool isSelected = state.IsSelected(t, k);
                            const bool beyondDuration = kfTime > maxPreviewTime;

                            ImU32 fillCol, borderCol;
                            if (isSelected)
                            {
                                fillCol = beyondDuration ? IM_COL32(180, 150, 40, 180) : IM_COL32(255, 210, 50, 255);
                                borderCol = beyondDuration ? IM_COL32(180, 180, 180, 180) : IM_COL32(255, 255, 255, 255);
                            }
                            else
                            {
                                fillCol = beyondDuration ? IM_COL32(90, 130, 180, 140) : IM_COL32(130, 190, 255, 255);
                                borderCol = beyondDuration ? IM_COL32(60, 80, 120, 140) : IM_COL32(80, 120, 180, 255);
                            }

                            DrawDiamond(dl, ImVec2(kfX, rowCenterY), kDiamondHalfSize, fillCol, borderCol);
                        }
                    }

                    // Playhead
                    {
                        const float phX = TimeToScreenX(state.PreviewTimeSeconds, tlOrigin.x, state.TimelineScrollX, zoom);
                        if (phX >= tlOrigin.x && phX <= tlMax.x)
                        {
                            dl->AddTriangleFilled(
                                ImVec2(phX, rulerTop + 2.0f),
                                ImVec2(phX - 6.0f, tracksTop),
                                ImVec2(phX + 6.0f, tracksTop),
                                IM_COL32(255, 40, 40, 255));

                            dl->AddLine(
                                ImVec2(phX, tracksTop),
                                ImVec2(phX, tracksTop + totalTracksHeight),
                                IM_COL32(255, 40, 40, 255), 1.5f);
                        }
                    }

                    // Separator between ruler and tracks
                    dl->AddLine(
                        ImVec2(tlOrigin.x, tracksTop),
                        ImVec2(tlMax.x, tracksTop),
                        IM_COL32(80, 80, 80, 255));

                    dl->PopClipRect();
                }
            }
            ImGui::EndChild();
        }
        ImGui::EndChild();

        // ---- Keyframe Inspector ----
        ImGui::Separator();
        ImGui::BeginChild("##KfInspector", ImVec2(0.0f, kInspectorHeight), ImGuiChildFlags_Border);
        {
            ImGui::SeparatorText("Keyframe Inspector");
            DrawKeyframeInspector(state, undoService);
        }
        ImGui::EndChild();

        // ---- Footer ----
        const bool hasUnsavedChanges = (state.WorkingJson.dump() != state.AppliedJson.dump());
        ImGui::Separator();
        ImGui::BeginDisabled(!hasUnsavedChanges);
        if (ImGui::Button("Apply Changes", ImVec2(180.0f, 0.0f)))
        {
            if (ApplyPendingClipChanges(undoService, state, "Edit Animation Clip"))
            {
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
        EditorPanelStyle::PopPanelVisualStyle();
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
