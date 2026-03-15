#include "EditorProjectPanelInternal.h"
#include "EditorProjectPanelShared.h"

#include "ProjectAssetOperations.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace Limitless::EditorProjectPanel::Internal
{
    void DrawCreateMenuItems(const std::filesystem::path& parentRelativePath,
                             EditorProjectPanelState& state,
                             const ProjectPanelCallbacks& callbacks)
    {
        state.FolderPopupParent = parentRelativePath;
        if (ImGui::MenuItem("Create Folder"))
        {
            state.FolderPopupPending = EditorProjectFolderPopup::Create;
            CopyTextToBuffer(state.FolderPopupBuffer, "New Folder");
        }
        if (ImGui::MenuItem("Create Scene") && callbacks.OnCreateSceneRequested)
            callbacks.OnCreateSceneRequested(parentRelativePath);
        if (ImGui::MenuItem("Create Material"))
        {
            state.CreateMaterialParentRelativePath = parentRelativePath;
            CopyTextToBuffer(state.CreateMaterialNameBuffer, "New Material");
            state.CreateMaterialPopupPending = true;
        }
        if (ImGui::MenuItem("Create Tileset"))
        {
            state.CreateTilesetParentRelativePath = parentRelativePath;
            CopyTextToBuffer(state.CreateTilesetNameBuffer, "New Tileset");
            state.CreateTilesetPopupPending = true;
        }
        if (ImGui::MenuItem("Create Tile Palette"))
        {
            state.CreateTilePaletteParentRelativePath = parentRelativePath;
            CopyTextToBuffer(state.CreateTilePaletteNameBuffer, "New Tile Palette");
            state.CreateTilePalettePopupPending = true;
        }
        if (ImGui::MenuItem("Create Audio Mixer"))
        {
            state.CreateAudioMixerParentRelativePath = parentRelativePath;
            CopyTextToBuffer(state.CreateAudioMixerNameBuffer, "New Audio Mixer");
            state.CreateAudioMixerPopupPending = true;
        }
        if (ImGui::MenuItem("Create Input Actions"))
        {
            state.CreateInputActionsParentRelativePath = parentRelativePath;
            CopyTextToBuffer(state.CreateInputActionsNameBuffer, "New Input Actions");
            state.CreateInputActionsPopupPending = true;
        }
        if (ImGui::MenuItem("Create Animation Clip"))
        {
            state.CreateAnimationClipParentRelativePath = parentRelativePath;
            CopyTextToBuffer(state.CreateAnimationClipNameBuffer, "New Animation Clip");
            state.CreateAnimationClipPopupPending = true;
        }
        if (ImGui::MenuItem("Create Animator Controller"))
        {
            state.CreateAnimatorControllerParentRelativePath = parentRelativePath;
            CopyTextToBuffer(state.CreateAnimatorControllerNameBuffer, "New Animator Controller");
            state.CreateAnimatorControllerPopupPending = true;
        }
        if (ImGui::MenuItem("Create Native Script"))
        {
            state.CreateNativeScriptParentRelativePath = parentRelativePath;
            CopyTextToBuffer(state.CreateNativeScriptClassNameBuffer, "NewNativeScript");
            state.CreateNativeScriptPopupPending = true;
        }
        if (ImGui::MenuItem("Create C# Script"))
        {
            state.CreateManagedScriptParentRelativePath = parentRelativePath;
            CopyTextToBuffer(state.CreateManagedScriptClassNameBuffer, "NewManagedScript");
            state.CreateManagedScriptPopupPending = true;
        }
    }

    void SetActiveFolder(EditorProjectPanelState& state, std::filesystem::path relativePath)
    {
        if (state.IsLocked)
            return;

        relativePath = relativePath.lexically_normal();
        if (relativePath == ".")
            relativePath.clear();
        if (state.ActiveFolderRelativePath != relativePath)
        {
            state.ActiveFolderRelativePath = std::move(relativePath);
            state.AssetsRootExpanded = true;
            std::filesystem::path ancestorPath;
            for (const std::filesystem::path& segment : state.ActiveFolderRelativePath)
            {
                ancestorPath /= segment;
                state.ExpandedFolderState[ancestorPath.generic_string()] = true;
            }
            state.BrowseLocationChanged = true;
        }
    }

    void DrawProjectBrowserRegion(const std::filesystem::path& assetsDirectory,
                                  EditorProjectPanelState& state,
                                  EditorAssetPreview::MaterialPreviewCache& materialPreviewCache,
                                  ProjectPanelSelectionRefs& selection,
                                  const ProjectPanelCallbacks& callbacks)
    {
        const bool searchActive = HasProjectSearchFilter(state);

        const auto beginFolderContextMenu = [&](const std::filesystem::path& folderRelativePath, const std::string& folderName, const bool isRoot) {
            if (!ImGui::BeginPopupContextItem())
                return;

            DrawCreateMenuItems(folderRelativePath, state, callbacks);

            if (!isRoot)
            {
                ImGui::Separator();
                if (ImGui::MenuItem("Rename"))
                {
                    state.FolderPopupPending = EditorProjectFolderPopup::Rename;
                    CopyTextToBuffer(state.FolderPopupBuffer, folderName.c_str());
                }
                if (ImGui::MenuItem("Delete"))
                {
                    if (ProjectAssetOperations::DeleteFolderInAssets(assetsDirectory, folderRelativePath))
                    {
                        InvalidateProjectDirectoryCache(state);
                        if (state.ActiveFolderRelativePath == folderRelativePath ||
                            (!state.ActiveFolderRelativePath.empty() && state.ActiveFolderRelativePath.generic_string().rfind(folderRelativePath.generic_string() + "/", 0) == 0))
                        {
                            SetActiveFolder(state, folderRelativePath.parent_path());
                        }
                    }
                }
            }

            ImGui::EndPopup();
        };

        const auto acceptFolderDropTarget = [&](const std::filesystem::path& folderRelativePath) {
            if (!ImGui::BeginDragDropTarget())
                return;

            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetMultiSelectionPayload))
            {
                const std::vector<std::string> keys = ParseAssetKeyListPayload(payload->Data, payload->DataSize);
                MoveAssetListToTargetFolder(state, keys, folderRelativePath);
            }
            else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(callbacks.TexturePayloadId))
            {
                MoveAssetOrFolderToTargetFolder(state, static_cast<const char*>(payload->Data), folderRelativePath);
            }
            else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(callbacks.AssetMovePayloadId))
            {
                MoveAssetOrFolderToTargetFolder(state, static_cast<const char*>(payload->Data), folderRelativePath);
            }
            else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(callbacks.AudioPayloadId))
            {
                MoveAssetOrFolderToTargetFolder(state, static_cast<const char*>(payload->Data), folderRelativePath);
            }
            else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(callbacks.FontPayloadId))
            {
                MoveAssetOrFolderToTargetFolder(state, static_cast<const char*>(payload->Data), folderRelativePath);
            }
            else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSceneEntityPayload))
            {
                const auto* entity = static_cast<const entt::entity*>(payload->Data);
                if (entity && callbacks.OnCreatePrefabFromSceneEntityRequested)
                    callbacks.OnCreatePrefabFromSceneEntityRequested(*entity, folderRelativePath);
            }

            ImGui::EndDragDropTarget();
        };

        std::function<void(const std::filesystem::path&, bool)> drawFolderNode = [&](const std::filesystem::path& relativePath, const bool isRoot) {
            const std::string displayName = isRoot ? std::string("Assets") : relativePath.filename().string();
            const std::string folderStateKey = relativePath.generic_string();
            const bool selected = relativePath == state.ActiveFolderRelativePath;
            bool folderExpanded = isRoot ? state.AssetsRootExpanded : true;
            if (!isRoot)
            {
                if (const auto expandedStateIt = state.ExpandedFolderState.find(folderStateKey); expandedStateIt != state.ExpandedFolderState.end())
                    folderExpanded = expandedStateIt->second;
            }

            const bool previousFolderExpanded = folderExpanded;
            ImGui::SetNextItemOpen(folderExpanded, ImGuiCond_Always);
            const float folderIndentX = ImGui::GetCursorScreenPos().x;
            const std::string label = isRoot
                ? (BadgePadLabel(displayName) + "###ProjectFolderRoot")
                : (BadgePadLabel(displayName) + "###ProjectFolder_" + folderStateKey);
            const bool nodeOpen = ImGui::TreeNodeEx(label.c_str(),
                ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_FramePadding |
                (selected ? ImGuiTreeNodeFlags_Selected : ImGuiTreeNodeFlags_None));
            DrawAssetTypeBadge(kBadgeFolder, folderIndentX);

            if (isRoot)
            {
                state.AssetsRootExpanded = nodeOpen;
                if (state.AssetsRootExpanded != previousFolderExpanded)
                    state.TreeExpansionStateChanged = true;
            }
            else
            {
                state.ExpandedFolderState[folderStateKey] = nodeOpen;
                if (nodeOpen != previousFolderExpanded)
                    state.TreeExpansionStateChanged = true;
            }

            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                SetActiveFolder(state, relativePath);

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly))
                state.HoveredFolderRelativePathForExternalDrop = relativePath;

            beginFolderContextMenu(relativePath, displayName, isRoot);
            acceptFolderDropTarget(relativePath);

            if (!isRoot && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
            {
                const std::string assetKey = "Assets/" + relativePath.generic_string();
                ImGui::SetDragDropPayload(callbacks.AssetMovePayloadId, assetKey.c_str(), static_cast<uint32_t>(assetKey.size() + 1), ImGuiCond_Once);
                ImGui::Text("%s", displayName.c_str());
                ImGui::EndDragDropSource();
            }

            if (nodeOpen)
            {
                const std::vector<ProjectAssetTreeEntry>& childEntries = GetCachedProjectDirectoryEntries(state, assetsDirectory, relativePath);
                for (const ProjectAssetTreeEntry& childEntry : childEntries)
                {
                    if (childEntry.IsDirectory)
                        drawFolderNode(childEntry.RelativePath, false);
                }
                ImGui::TreePop();
            }
        };

        std::function<void(const std::filesystem::path&, std::vector<ProjectGridEntry>&)> appendGridEntries =
            [&](const std::filesystem::path& relativePath, std::vector<ProjectGridEntry>& output) {
                const std::vector<ProjectAssetTreeEntry>& entries = GetCachedProjectDirectoryEntries(state, assetsDirectory, relativePath);
                std::unordered_map<std::string, uint8_t> scriptPairPresenceByBasePath;
                scriptPairPresenceByBasePath.reserve(entries.size());
                for (const ProjectAssetTreeEntry& entry : entries)
                {
                    if (entry.IsDirectory || !IsNativeScriptExtensionLower(entry.LowerExtension))
                        continue;
                    const std::filesystem::path scriptBaseRelativePath = entry.RelativePath.parent_path() / std::filesystem::path(entry.FileName).stem();
                    const std::string scriptBaseKey = scriptBaseRelativePath.generic_string();
                    uint8_t& presenceBits = scriptPairPresenceByBasePath[scriptBaseKey];
                    if (entry.LowerExtension == ".h")
                        presenceBits |= kScriptPairHeaderBit;
                    else if (entry.LowerExtension == ".cpp")
                        presenceBits |= kScriptPairSourceBit;
                }

                std::unordered_set<std::string> renderedScriptBasePaths;
                for (const ProjectAssetTreeEntry& entry : entries)
                {
                    if (entry.IsDirectory)
                    {
                        const bool directoryVisible = !searchActive || MatchesProjectSearchFilter(state, entry.FileName) || DirectoryContainsProjectSearchMatch(state, assetsDirectory, entry.RelativePath);
                        if (!directoryVisible)
                            continue;

                        ProjectGridEntry folderEntry;
                        folderEntry.Entry = entry;
                        folderEntry.DisplayName = entry.FileName;
                        folderEntry.PrimaryAssetKey = entry.AssetKey;
                        folderEntry.Badge = &kBadgeFolder;
                        folderEntry.IsDirectory = true;
                        output.push_back(std::move(folderEntry));

                        if (searchActive)
                            appendGridEntries(entry.RelativePath, output);
                        continue;
                    }

                    if (IsNativeScriptExtensionLower(entry.LowerExtension))
                    {
                        const std::filesystem::path scriptBaseRelativePath = entry.RelativePath.parent_path() / entry.RelativePath.stem();
                        const std::string scriptBaseKey = scriptBaseRelativePath.generic_string();
                        if (renderedScriptBasePaths.find(scriptBaseKey) != renderedScriptBasePaths.end())
                            continue;

                        const auto scriptPairPresenceIt = scriptPairPresenceByBasePath.find(scriptBaseKey);
                        const bool hasScriptPair =
                            scriptPairPresenceIt != scriptPairPresenceByBasePath.end() &&
                            (scriptPairPresenceIt->second & (kScriptPairHeaderBit | kScriptPairSourceBit)) ==
                                (kScriptPairHeaderBit | kScriptPairSourceBit);
                        if (hasScriptPair)
                        {
                            const std::filesystem::path headerRelativePath = scriptBaseRelativePath.string() + ".h";
                            const std::filesystem::path sourceRelativePath = scriptBaseRelativePath.string() + ".cpp";
                            const std::string sourceAssetKey = "Assets/" + sourceRelativePath.generic_string();
                            const std::string headerAssetKey = "Assets/" + headerRelativePath.generic_string();
                            const std::string scriptBaseName = scriptBaseRelativePath.stem().string();
                            const bool matchesSearch = !searchActive ||
                                MatchesProjectSearchFilter(state, scriptBaseName) ||
                                MatchesProjectSearchFilter(state, sourceAssetKey) ||
                                MatchesProjectSearchFilter(state, headerAssetKey);
                            renderedScriptBasePaths.insert(scriptBaseKey);
                            if (!matchesSearch)
                                continue;

                            ProjectGridEntry scriptEntry;
                            scriptEntry.Entry = entry;
                            scriptEntry.DisplayName = scriptBaseName + " [Native Script]";
                            scriptEntry.PrimaryAssetKey = sourceAssetKey;
                            scriptEntry.SecondaryAssetKey = headerAssetKey;
                            scriptEntry.Badge = &kBadgeScript;
                            scriptEntry.IsNativeScriptFile = true;
                            scriptEntry.IsScriptPair = true;
                            scriptEntry.HasPairedScriptFile = true;
                            output.push_back(std::move(scriptEntry));
                            continue;
                        }
                    }

                    const bool isTexture = IsTextureExtensionLower(entry.LowerExtension);
                    const bool isScene = IsSceneFileNameLower(entry.LowerFileName);
                    const bool isMaterial = IsMaterialFileNameLower(entry.LowerFileName);
                    const bool isTileset = IsTilesetFileNameLower(entry.LowerFileName);
                    const bool isTilePalette = IsTilePaletteFileNameLower(entry.LowerFileName);
                    const bool isAudioMixer = IsAudioMixerFileNameLower(entry.LowerFileName);
                    const bool isInputActions = IsInputActionsFileNameLower(entry.LowerFileName);
                    const bool isAnimationClip = IsAnimationClipFileNameLower(entry.LowerFileName);
                    const bool isAnimatorController = IsAnimatorControllerFileNameLower(entry.LowerFileName);
                    const bool isPrefab = IsPrefabFileNameLower(entry.LowerFileName);
                    const bool isShader = IsShaderExtensionLower(entry.LowerExtension);
                    const bool isAudio = IsAudioExtensionLower(entry.LowerExtension);
                    const bool isFont = IsFontExtensionLower(entry.LowerExtension);
                    const bool isNativeScriptFile = IsNativeScriptExtensionLower(entry.LowerExtension);
                    const bool isManagedScriptFile = IsManagedScriptExtensionLower(entry.LowerExtension);
                    const bool isScriptAssetFile = isNativeScriptFile || isManagedScriptFile;
                    const bool hasPairedScriptFile = isNativeScriptFile && ([&]() {
                        const std::filesystem::path scriptBaseRelativePath = entry.RelativePath.parent_path() / entry.RelativePath.stem();
                        const auto scriptPairPresenceIt = scriptPairPresenceByBasePath.find(scriptBaseRelativePath.generic_string());
                        return scriptPairPresenceIt != scriptPairPresenceByBasePath.end() &&
                               (scriptPairPresenceIt->second & (kScriptPairHeaderBit | kScriptPairSourceBit)) ==
                                   (kScriptPairHeaderBit | kScriptPairSourceBit);
                    })();
                    const std::string displayName = isScriptAssetFile
                        ? BuildProjectScriptAssetDisplayName(entry.AbsolutePath)
                        : GetAssetDisplayName(entry.AbsolutePath);
                    const bool matchesSearch = !searchActive || EntryMatchesProjectSearchFilter(state, entry, displayName);
                    if (!isTexture && !matchesSearch)
                        continue;

                    ProjectGridEntry fileEntry;
                    fileEntry.Entry = entry;
                    fileEntry.DisplayName = displayName;
                    fileEntry.PrimaryAssetKey = entry.AssetKey;
                    fileEntry.IsTexture = isTexture;
                    fileEntry.IsScene = isScene;
                    fileEntry.IsMaterial = isMaterial;
                    fileEntry.IsTileset = isTileset;
                    fileEntry.IsTilePalette = isTilePalette;
                    fileEntry.IsAudioMixer = isAudioMixer;
                    fileEntry.IsInputActions = isInputActions;
                    fileEntry.IsAnimationClip = isAnimationClip;
                    fileEntry.IsAnimatorController = isAnimatorController;
                    fileEntry.IsPrefab = isPrefab;
                    fileEntry.IsShader = isShader;
                    fileEntry.IsAudio = isAudio;
                    fileEntry.IsFont = isFont;
                    fileEntry.IsNativeScriptFile = isNativeScriptFile;
                    fileEntry.IsManagedScriptFile = isManagedScriptFile;
                    fileEntry.HasPairedScriptFile = hasPairedScriptFile;
                    fileEntry.Badge = &ResolveAssetBadge(isTexture,
                                                         isScene,
                                                         isMaterial,
                                                         isAudioMixer,
                                                         isInputActions,
                                                         isAnimationClip,
                                                         isAnimatorController,
                                                         isPrefab,
                                                         isShader,
                                                         isAudio,
                                                         isFont,
                                                         isNativeScriptFile,
                                                         isManagedScriptFile);
                    if (isTexture)
                    {
                        // Only look up sprite settings when the user has explicitly
                        // expanded this texture or during a search.  This avoids
                        // loading .meta files for every texture during grid rebuild.
                        const bool isExplicitlyExpanded = state.ExpandedSubSpriteTextureKeys.count(entry.AssetKey) > 0;
                        const bool needSpriteSettings = isExplicitlyExpanded || searchActive;
                        const Assets::SpriteImportSettings* spriteSettings = nullptr;
                        bool hasSubSprites = false;

                        if (needSpriteSettings)
                        {
                            spriteSettings = &GetCachedSpriteImportSettings(state, entry.AssetKey);
                            hasSubSprites = spriteSettings->Mode == Assets::SpriteImportSettings::SpriteMode::Multiple && !spriteSettings->SubSprites.empty();
                        }

                        // Mark as expandable only when we know; when sprite settings
                        // haven't been loaded yet the expand arrow will appear once
                        // the user expands the first time (or during search).
                        fileEntry.HasExpandableSubSprites = hasSubSprites;

                        if (!matchesSearch)
                            continue;

                        output.push_back(std::move(fileEntry));

                        const bool isExpanded = hasSubSprites &&
                            (searchActive || isExplicitlyExpanded);
                        if (isExpanded && spriteSettings)
                        {
                            const std::string textureBaseName = entry.AbsolutePath.stem().string();
                            for (size_t subSpriteIndex = 0; subSpriteIndex < spriteSettings->SubSprites.size(); ++subSpriteIndex)
                            {
                                const Assets::SpriteSubRect& subSprite = spriteSettings->SubSprites[subSpriteIndex];
                                const std::string subSpriteAssetKey = entry.AssetKey + "#" + std::to_string(subSpriteIndex);
                                const std::string subSpriteDisplayName = subSprite.Name.empty()
                                    ? (textureBaseName + "_" + std::to_string(subSpriteIndex))
                                    : subSprite.Name;
                                if (searchActive && !matchesSearch &&
                                    !MatchesProjectSearchFilter(state, subSpriteDisplayName) &&
                                    !MatchesProjectSearchFilter(state, subSpriteAssetKey))
                                {
                                    continue;
                                }

                                ProjectGridEntry subSpriteEntry;
                                subSpriteEntry.Entry = entry;
                                subSpriteEntry.DisplayName = subSpriteDisplayName;
                                subSpriteEntry.PrimaryAssetKey = subSpriteAssetKey;
                                subSpriteEntry.Badge = &kBadgeSubSprite;
                                subSpriteEntry.IsTexture = true;
                                subSpriteEntry.IsSubSprite = true;
                                subSpriteEntry.HasThumbnailSubRect = true;
                                subSpriteEntry.ThumbnailRectPixels = subSprite.RectPixels;
                                output.push_back(std::move(subSpriteEntry));
                            }
                        }
                        continue;
                    }

                    output.push_back(std::move(fileEntry));
                }
            };

        const bool useCompactListView = state.GridScale <= kCompactListScaleThreshold;
        ImGui::BeginChild("##ProjectBrowserRegion", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);

        if (ImGui::BeginChild("##ProjectFolderPane", ImVec2(260.0f, 0.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar))
        {
            ImGui::TextUnformatted("Folders");
            ImGui::Separator();
            drawFolderNode("", true);
        }
        ImGui::EndChild();

        ImGui::SameLine();

        if (ImGui::BeginChild("##ProjectGridPane", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar))
        {
            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
                state.HoveredFolderRelativePathForExternalDrop = state.ActiveFolderRelativePath;

            if (!state.ActiveFolderRelativePath.empty())
            {
                if (ImGui::Button("Up"))
                    SetActiveFolder(state, state.ActiveFolderRelativePath.parent_path());
                ImGui::SameLine();
            }

            if (ImGui::SmallButton("Assets"))
                SetActiveFolder(state, "");

            const std::filesystem::path activeFolderPathForBreadcrumbs = state.ActiveFolderRelativePath;
            std::optional<std::filesystem::path> pendingBreadcrumbFolder;
            std::filesystem::path breadcrumbPath;
            for (const std::filesystem::path& segment : activeFolderPathForBreadcrumbs)
            {
                breadcrumbPath /= segment;
                ImGui::SameLine();
                ImGui::TextUnformatted("/");
                ImGui::SameLine();
                ImGui::PushID(breadcrumbPath.generic_string().c_str());
                if (ImGui::SmallButton(segment.string().c_str()))
                    pendingBreadcrumbFolder = breadcrumbPath;
                ImGui::PopID();
            }
            if (pendingBreadcrumbFolder.has_value())
                SetActiveFolder(state, *pendingBreadcrumbFolder);

            ImGui::Separator();

            if (ImGui::BeginPopupContextWindow("##ProjectGridContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
            {
                DrawCreateMenuItems(state.ActiveFolderRelativePath, state, callbacks);
                ImGui::EndPopup();
            }

            // Only rebuild grid entries when something actually changed.
            ProjectPanelCacheState& cacheState = GetProjectPanelCacheState(state);
            const uint64_t dirGen = cacheState.DirectoryCacheGeneration;
            const std::string& currentSearchFilter = GetProjectSearchFilterLower(state);
            const bool gridDirty = state.GridEntryDirty
                || state.GridEntryDirCacheGeneration != dirGen
                || state.GridEntryCachedFolder != state.ActiveFolderRelativePath
                || state.GridEntryCachedSearchFilter != currentSearchFilter
                || state.GridEntryCachedExpansions != state.ExpandedSubSpriteTextureKeys;

            if (gridDirty)
            {
                cacheState.CachedGridEntries.clear();
                appendGridEntries(state.ActiveFolderRelativePath, cacheState.CachedGridEntries);

                cacheState.CachedVisibleAssetKeys.clear();
                cacheState.CachedVisibleAssetKeys.reserve(cacheState.CachedGridEntries.size());
                for (const ProjectGridEntry& entry : cacheState.CachedGridEntries)
                {
                    if (!entry.IsDirectory && !entry.PrimaryAssetKey.empty())
                        cacheState.CachedVisibleAssetKeys.push_back(entry.PrimaryAssetKey);
                }

                state.GridEntryDirCacheGeneration = dirGen;
                state.GridEntryCachedFolder = state.ActiveFolderRelativePath;
                state.GridEntryCachedSearchFilter = currentSearchFilter;
                state.GridEntryCachedExpansions = state.ExpandedSubSpriteTextureKeys;
                state.GridEntryDirty = false;
            }

            std::vector<ProjectGridEntry>& gridEntries = cacheState.CachedGridEntries;
            std::vector<std::string>& visibleAssetKeys = cacheState.CachedVisibleAssetKeys;

            if (gridEntries.empty())
            {
                ImGui::Dummy(ImVec2(0.0f, 8.0f));
                ImGui::TextColored(ImVec4(0.78f, 0.82f, 0.90f, 1.0f),
                    searchActive ? "No assets match the current filter in this location." : "This folder is empty.");
            }
            else if (useCompactListView)
            {
                const auto fitRowTextToWidth = [](const std::string& text, const float maxWidth) {
                    if (text.empty() || maxWidth <= 0.0f)
                        return std::string{};
                    if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth)
                        return text;
                    constexpr const char* ellipsis = "...";
                    const float ellipsisWidth = ImGui::CalcTextSize(ellipsis).x;
                    if (ellipsisWidth >= maxWidth)
                        return std::string(ellipsis);
                    std::string fitted = text;
                    while (!fitted.empty())
                    {
                        fitted.pop_back();
                        const std::string candidate = fitted + ellipsis;
                        if (ImGui::CalcTextSize(candidate.c_str()).x <= maxWidth)
                            return candidate;
                    }
                    return std::string(ellipsis);
                };

                const float rowHeight = 30.0f;
                const float rowSpacing = 4.0f;
                const float rowPaddingX = 10.0f;
                const float badgePadX = 5.0f;
                const float badgePadY = 2.0f;
                const float textGap = 8.0f;
                const float rightPadding = 10.0f;
                const float minNameWidth = 120.0f;

                const float expandArrowSize = 10.0f;
                const float expandArrowPad = 4.0f;

                ImGuiListClipper listClipper;
                listClipper.Begin(static_cast<int>(gridEntries.size()), rowHeight + rowSpacing);
                while (listClipper.Step())
                {
                    for (int index = listClipper.DisplayStart; index < listClipper.DisplayEnd; ++index)
                    {
                        ProjectGridEntry& entry = gridEntries[index];
                        if (entry.IsSubSprite)
                            ImGui::Indent(18.0f);
                        ImGui::PushID(index);
                        const float rowWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
                        const ImVec2 rowMin = ImGui::GetCursorScreenPos();
                        ImGui::InvisibleButton("##AssetRow", ImVec2(rowWidth, rowHeight));
                        const bool hovered = ImGui::IsItemHovered();
                        const bool releasedOnItemWithoutDrag = hovered && ImGui::IsMouseReleased(0) && (ImGui::GetDragDropPayload() == nullptr);
                        const bool isMultiSelected = !entry.IsDirectory &&
                            std::find(state.MultiSelectedAssetKeys.begin(), state.MultiSelectedAssetKeys.end(), entry.PrimaryAssetKey) != state.MultiSelectedAssetKeys.end();
                        const bool isSelected = !entry.IsDirectory && (IsGridEntryPrimarySelected(entry, selection) || isMultiSelected);

                        const ImVec2 rowMax(rowMin.x + rowWidth, rowMin.y + rowHeight);
                        ImDrawList* drawList = ImGui::GetWindowDrawList();
                        const ImU32 fillColor = isSelected ? IM_COL32(44, 79, 138, 240) : (hovered ? IM_COL32(29, 39, 60, 240) : IM_COL32(19, 26, 40, 225));
                        const ImU32 borderColor = isSelected ? IM_COL32(92, 145, 230, 255) : IM_COL32(56, 72, 104, 210);
                        drawList->AddRectFilled(rowMin, rowMax, fillColor, 6.0f);
                        drawList->AddRect(rowMin, rowMax, borderColor, 6.0f, 0, isSelected ? 2.0f : 1.0f);

                        float contentStartX = rowMin.x + rowPaddingX;

                        if (entry.HasExpandableSubSprites)
                        {
                            const std::string& parentKey = entry.Entry.AssetKey.empty() ? entry.PrimaryAssetKey : entry.Entry.AssetKey;
                            const bool isExpanded = state.ExpandedSubSpriteTextureKeys.count(parentKey) > 0;
                            const float arrowCenterX = contentStartX + expandArrowSize * 0.5f;
                            const float arrowCenterY = rowMin.y + rowHeight * 0.5f;
                            const float halfSize = expandArrowSize * 0.4f;
                            if (isExpanded)
                            {
                                const ImVec2 p1(arrowCenterX - halfSize, arrowCenterY - halfSize * 0.5f);
                                const ImVec2 p2(arrowCenterX + halfSize, arrowCenterY - halfSize * 0.5f);
                                const ImVec2 p3(arrowCenterX, arrowCenterY + halfSize * 0.5f);
                                drawList->AddTriangleFilled(p1, p2, p3, IM_COL32(200, 210, 225, 255));
                            }
                            else
                            {
                                const ImVec2 p1(arrowCenterX - halfSize * 0.5f, arrowCenterY - halfSize);
                                const ImVec2 p2(arrowCenterX + halfSize * 0.5f, arrowCenterY);
                                const ImVec2 p3(arrowCenterX - halfSize * 0.5f, arrowCenterY + halfSize);
                                drawList->AddTriangleFilled(p1, p2, p3, IM_COL32(200, 210, 225, 255));
                            }
                            const ImVec2 arrowHitMin(rowMin.x, rowMin.y);
                            const ImVec2 arrowHitMax(contentStartX + expandArrowSize + expandArrowPad, rowMax.y);
                            if (hovered && ImGui::IsMouseClicked(0))
                            {
                                const ImVec2 mousePos = ImGui::GetMousePos();
                                if (mousePos.x >= arrowHitMin.x && mousePos.x <= arrowHitMax.x &&
                                    mousePos.y >= arrowHitMin.y && mousePos.y <= arrowHitMax.y)
                                {
                                    if (isExpanded)
                                        state.ExpandedSubSpriteTextureKeys.erase(parentKey);
                                    else
                                        state.ExpandedSubSpriteTextureKeys.insert(parentKey);
                                }
                            }
                            contentStartX += expandArrowSize + expandArrowPad;
                        }

                        const AssetTypeBadgeInfo& badge = *entry.Badge;
                        const ImVec2 badgeTextSize = ImGui::CalcTextSize(badge.Label);
                        const float badgeHeight = badgeTextSize.y + badgePadY * 2.0f;
                        const ImVec2 badgeMin(contentStartX, rowMin.y + (rowHeight - badgeHeight) * 0.5f);
                        const ImVec2 badgeMax(badgeMin.x + badgeTextSize.x + badgePadX * 2.0f, badgeMin.y + badgeHeight);
                        drawList->AddRectFilled(badgeMin, badgeMax, badge.FillColor, 4.0f);
                        drawList->AddRect(badgeMin, badgeMax, badge.BorderColor, 4.0f, 0, 1.0f);
                        drawList->AddText(ImVec2(badgeMin.x + badgePadX, badgeMin.y + badgePadY), badge.TextColor, badge.Label);

                        const float nameStartX = badgeMax.x + textGap;
                        const float textY = rowMin.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f;
                        const std::string secondaryText = searchActive
                            ? (entry.IsDirectory
                                ? (entry.Entry.RelativePath.empty() ? std::string("Assets") : std::string("Assets/") + entry.Entry.RelativePath.generic_string())
                                : (entry.Entry.RelativePath.parent_path().empty() ? std::string("Assets") : std::string("Assets/") + entry.Entry.RelativePath.parent_path().generic_string()))
                            : std::string{};
                        std::string fittedSecondaryText;
                        float secondaryTextWidth = 0.0f;
                        bool drawSecondaryText = false;
                        if (!secondaryText.empty())
                        {
                            fittedSecondaryText = fitRowTextToWidth(secondaryText, std::max(120.0f, rowWidth * 0.35f));
                            secondaryTextWidth = ImGui::CalcTextSize(fittedSecondaryText.c_str()).x;
                            drawSecondaryText = !fittedSecondaryText.empty() &&
                                (rowMax.x - nameStartX - rightPadding) > (secondaryTextWidth + textGap + minNameWidth);
                        }

                        const float nameMaxX = rowMax.x - rightPadding - (drawSecondaryText ? (secondaryTextWidth + textGap) : 0.0f);
                        const float nameWidth = std::max(0.0f, nameMaxX - nameStartX);
                        const std::string fittedDisplayName = fitRowTextToWidth(entry.DisplayName, nameWidth);
                        drawList->PushClipRect(ImVec2(nameStartX, rowMin.y), ImVec2(nameMaxX, rowMax.y), true);
                        drawList->AddText(ImVec2(nameStartX, textY), IM_COL32(230, 236, 245, 255), fittedDisplayName.c_str());
                        drawList->PopClipRect();
                        if (drawSecondaryText)
                        {
                            drawList->AddText(ImVec2(rowMax.x - rightPadding - secondaryTextWidth, textY), IM_COL32(145, 156, 176, 255), fittedSecondaryText.c_str());
                        }

                        if (entry.IsDirectory)
                        {
                            if (hovered)
                                state.HoveredFolderRelativePathForExternalDrop = entry.Entry.RelativePath;
                            if (hovered && ImGui::IsMouseDoubleClicked(0))
                                SetActiveFolder(state, entry.Entry.RelativePath);
                            beginFolderContextMenu(entry.Entry.RelativePath, entry.Entry.FileName, false);
                            acceptFolderDropTarget(entry.Entry.RelativePath);
                            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
                            {
                                ImGui::SetDragDropPayload(callbacks.AssetMovePayloadId, entry.PrimaryAssetKey.c_str(), static_cast<uint32_t>(entry.PrimaryAssetKey.size() + 1), ImGuiCond_Once);
                                ImGui::Text("%s", entry.DisplayName.c_str());
                                ImGui::EndDragDropSource();
                            }
                        }
                        else
                        {
                            HandleProjectGridItemSelection(state, selection, visibleAssetKeys, entry, releasedOnItemWithoutDrag);
                            HandleProjectGridItemActivation(state, selection, entry, hovered, callbacks);
                            DrawProjectGridItemContextMenu(assetsDirectory, state, selection, entry, callbacks);
                            DrawProjectGridItemDragDropSource(state, entry, callbacks);
                        }

                        ImGui::PopID();
                        if (entry.IsSubSprite)
                            ImGui::Unindent(18.0f);
                        ImGui::Dummy(ImVec2(0.0f, rowSpacing));
                    }
                }
            }
            else
            {
                const float gridScale = state.GridScale;
                const float tileWidth = 168.0f * gridScale;
                const float tileSpacing = 14.0f * gridScale;
                const float previewInset = 10.0f * gridScale;
                const float previewTopOffset = 24.0f * gridScale;
                const float previewHeight = 96.0f * gridScale;
                const float tileTextPadX = 12.0f * gridScale;
                const float textBlockTopPadding = 10.0f * gridScale;
                const float textLineGap = std::max(1.0f, 2.0f * gridScale);
                const float textBlockBottomPadding = 12.0f * gridScale;
                const float nameLineHeight = ImGui::GetTextLineHeight();
                const float pathLineHeight = ImGui::GetTextLineHeight();
                const float tileHeight = previewInset + previewTopOffset + previewHeight + textBlockTopPadding + nameLineHeight + textLineGap + pathLineHeight + textBlockBottomPadding;
                const float availableWidth = ImGui::GetContentRegionAvail().x;
                const int columns = std::max(1, static_cast<int>((availableWidth + tileSpacing) / (tileWidth + tileSpacing)));
                const auto fitTextToWidth = [](const std::string& text, const float maxWidth) {
                    if (text.empty() || maxWidth <= 0.0f)
                        return std::string{};
                    if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth)
                        return text;
                    constexpr const char* ellipsis = "...";
                    const float ellipsisWidth = ImGui::CalcTextSize(ellipsis).x;
                    if (ellipsisWidth >= maxWidth)
                        return std::string(ellipsis);
                    std::string fitted = text;
                    while (!fitted.empty())
                    {
                        fitted.pop_back();
                        const std::string candidate = fitted + ellipsis;
                        if (ImGui::CalcTextSize(candidate.c_str()).x <= maxWidth)
                            return candidate;
                    }
                    return std::string(ellipsis);
                };

                const float expandArrowSize = std::max(10.0f, 12.0f * gridScale);

                const int entryCount = static_cast<int>(gridEntries.size());
                const int totalRows = (entryCount + columns - 1) / columns;
                const float rowStepHeight = tileHeight + ImGui::GetStyle().ItemSpacing.y;

                ImGuiListClipper gridClipper;
                gridClipper.Begin(totalRows, rowStepHeight);
                while (gridClipper.Step())
                {
                    for (int row = gridClipper.DisplayStart; row < gridClipper.DisplayEnd; ++row)
                    {
                        const int rowStartIdx = row * columns;
                        const int rowEndIdx = std::min(rowStartIdx + columns, entryCount);

                        for (int index = rowStartIdx; index < rowEndIdx; ++index)
                        {
                            ProjectGridEntry& entry = gridEntries[index];
                            if (index > rowStartIdx)
                                ImGui::SameLine(0.0f, tileSpacing);

                            ImGui::PushID(index);
                            const ImVec2 tileMin = ImGui::GetCursorScreenPos();
                            ImGui::InvisibleButton("##AssetTile", ImVec2(tileWidth, tileHeight));
                            const bool hovered = ImGui::IsItemHovered();
                            const bool releasedOnItemWithoutDrag = hovered && ImGui::IsMouseReleased(0) && (ImGui::GetDragDropPayload() == nullptr);
                            const bool isMultiSelected = !entry.IsDirectory &&
                                std::find(state.MultiSelectedAssetKeys.begin(), state.MultiSelectedAssetKeys.end(), entry.PrimaryAssetKey) != state.MultiSelectedAssetKeys.end();
                            const bool isSelected = !entry.IsDirectory && (IsGridEntryPrimarySelected(entry, selection) || isMultiSelected);

                            const ImVec2 tileMax(tileMin.x + tileWidth, tileMin.y + tileHeight);
                            ImDrawList* drawList = ImGui::GetWindowDrawList();
                            const auto drawCrispText = [&](const ImVec2& pos, const ImU32 color, const std::string& text) {
                                drawList->AddText(pos, color, text.c_str());
                            };
                            const ImU32 fillColor = isSelected ? IM_COL32(44, 79, 138, 240) : (hovered ? IM_COL32(29, 39, 60, 240) : IM_COL32(19, 26, 40, 225));
                            const ImU32 borderColor = isSelected ? IM_COL32(92, 145, 230, 255) : IM_COL32(56, 72, 104, 210);
                            drawList->AddRectFilled(tileMin, tileMax, fillColor, 8.0f);
                            drawList->AddRect(tileMin, tileMax, borderColor, 8.0f, 0, isSelected ? 2.0f : 1.0f);

                            const ImVec2 previewMin(tileMin.x + previewInset, tileMin.y + previewInset + previewTopOffset);
                            const ImVec2 previewMax(tileMax.x - previewInset, previewMin.y + previewHeight);
                            drawList->AddRectFilled(previewMin, previewMax, IM_COL32(13, 18, 29, 240), 6.0f);
                            drawList->AddRect(previewMin, previewMax, IM_COL32(48, 61, 90, 220), 6.0f, 0, 1.0f);

                            DrawProjectGridEntryPreview(state, materialPreviewCache, entry, previewMin, previewMax);

                            if (entry.HasExpandableSubSprites)
                            {
                                const std::string& parentKey = entry.Entry.AssetKey.empty() ? entry.PrimaryAssetKey : entry.Entry.AssetKey;
                                const bool isExpanded = state.ExpandedSubSpriteTextureKeys.count(parentKey) > 0;
                                const float arrowCenterX = tileMax.x - previewInset - expandArrowSize * 0.5f;
                                const float arrowCenterY = tileMin.y + previewInset * 0.5f + previewTopOffset * 0.5f;
                                const float halfSize = expandArrowSize * 0.4f;

                                drawList->AddCircleFilled(ImVec2(arrowCenterX, arrowCenterY), expandArrowSize * 0.7f, IM_COL32(20, 28, 45, 200));
                                if (isExpanded)
                                {
                                    const ImVec2 p1(arrowCenterX - halfSize, arrowCenterY - halfSize * 0.4f);
                                    const ImVec2 p2(arrowCenterX + halfSize, arrowCenterY - halfSize * 0.4f);
                                    const ImVec2 p3(arrowCenterX, arrowCenterY + halfSize * 0.6f);
                                    drawList->AddTriangleFilled(p1, p2, p3, IM_COL32(220, 230, 245, 255));
                                }
                                else
                                {
                                    const ImVec2 p1(arrowCenterX - halfSize * 0.4f, arrowCenterY - halfSize);
                                    const ImVec2 p2(arrowCenterX + halfSize * 0.6f, arrowCenterY);
                                    const ImVec2 p3(arrowCenterX - halfSize * 0.4f, arrowCenterY + halfSize);
                                    drawList->AddTriangleFilled(p1, p2, p3, IM_COL32(220, 230, 245, 255));
                                }

                                const ImVec2 arrowHitMin(arrowCenterX - expandArrowSize, tileMin.y);
                                const ImVec2 arrowHitMax(tileMax.x, tileMin.y + previewInset + previewTopOffset);
                                if (hovered && ImGui::IsMouseClicked(0))
                                {
                                    const ImVec2 mousePos = ImGui::GetMousePos();
                                    if (mousePos.x >= arrowHitMin.x && mousePos.x <= arrowHitMax.x &&
                                        mousePos.y >= arrowHitMin.y && mousePos.y <= arrowHitMax.y)
                                    {
                                        if (isExpanded)
                                            state.ExpandedSubSpriteTextureKeys.erase(parentKey);
                                        else
                                            state.ExpandedSubSpriteTextureKeys.insert(parentKey);
                                    }
                                }
                            }

                            const std::string secondaryText = entry.IsDirectory
                                ? (entry.Entry.RelativePath.empty() ? std::string("Assets") : std::string("Assets/") + entry.Entry.RelativePath.generic_string())
                                : (entry.Entry.RelativePath.parent_path().empty() ? std::string("Assets") : std::string("Assets/") + entry.Entry.RelativePath.parent_path().generic_string());
                            const float availableTextWidth = std::max(0.0f, tileWidth - tileTextPadX * 2.0f);
                            const std::string fittedDisplayName = fitTextToWidth(entry.DisplayName, availableTextWidth);
                            const std::string fittedSecondaryText = fitTextToWidth(secondaryText, availableTextWidth);
                            const float nameTextY = previewMax.y + textBlockTopPadding;
                            const float pathTextY = nameTextY + nameLineHeight + textLineGap;
                            drawList->PushClipRect(tileMin, tileMax, true);
                            drawCrispText(ImVec2(tileMin.x + tileTextPadX, nameTextY), IM_COL32(230, 236, 245, 255), fittedDisplayName);
                            drawCrispText(ImVec2(tileMin.x + tileTextPadX, pathTextY), IM_COL32(145, 156, 176, 255), fittedSecondaryText);
                            drawList->PopClipRect();

                            if (entry.IsDirectory)
                            {
                                if (hovered)
                                    state.HoveredFolderRelativePathForExternalDrop = entry.Entry.RelativePath;
                                if (hovered && ImGui::IsMouseDoubleClicked(0))
                                    SetActiveFolder(state, entry.Entry.RelativePath);
                                beginFolderContextMenu(entry.Entry.RelativePath, entry.Entry.FileName, false);
                                acceptFolderDropTarget(entry.Entry.RelativePath);
                                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
                                {
                                    ImGui::SetDragDropPayload(callbacks.AssetMovePayloadId, entry.PrimaryAssetKey.c_str(), static_cast<uint32_t>(entry.PrimaryAssetKey.size() + 1), ImGuiCond_Once);
                                    ImGui::Text("%s", entry.DisplayName.c_str());
                                    ImGui::EndDragDropSource();
                                }
                            }
                            else
                            {
                                HandleProjectGridItemSelection(state, selection, visibleAssetKeys, entry, releasedOnItemWithoutDrag);
                                HandleProjectGridItemActivation(state, selection, entry, hovered, callbacks);
                                DrawProjectGridItemContextMenu(assetsDirectory, state, selection, entry, callbacks);
                                DrawProjectGridItemDragDropSource(state, entry, callbacks);
                            }

                            ImGui::PopID();
                        }
                    }
                }
            }
        }
        ImGui::EndChild();
        ImGui::EndChild();
    }
}
