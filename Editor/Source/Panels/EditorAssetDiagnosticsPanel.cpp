#include "EditorAssetDiagnosticsPanel.h"

#include "EditorPanelStyle.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetImportPipeline.h"
#include "Assets/AssetPaths.h"
#include "Assets/AssetUtils.h"

#include "Core/Debug/Log.h"

#include "imgui/imgui.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#ifdef LT_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <shellapi.h>
#endif

namespace Limitless::EditorAssetDiagnosticsPanel
{
    namespace
    {
        bool EndsWith(const std::string& s, const std::string& suffix)
        {
            if (s.size() < suffix.size()) return false;
            return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
        }

        std::optional<Assets::AssetType> GuessTypeFromPath(const std::filesystem::path& path)
        {
            const std::string name = path.filename().string();
            const std::string ext = path.extension().string();

            if (EndsWith(name, ".scene.json")) return Assets::AssetType::Scene;
            if (EndsWith(name, ".material.json")) return Assets::AssetType::Material;
            if (EndsWith(name, ".inputactions.json")) return Assets::AssetType::InputActions;
            if (ext == ".glsl") return Assets::AssetType::Shader;

            // Textures
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" ||
                ext == ".hdr" || ext == ".psd" || ext == ".gif" || ext == ".ppm" || ext == ".pnm")
            {
                return Assets::AssetType::Texture2D;
            }

            // Audio
            if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac")
            {
                return Assets::AssetType::AudioClip;
            }

            return std::nullopt;
        }

        bool RevealInFileExplorer(const std::filesystem::path& path)
        {
            if (path.empty())
            {
                return false;
            }

#ifdef LT_PLATFORM_WINDOWS
            const std::string p = path.string();
            if (std::filesystem::is_directory(path))
            {
                const HINSTANCE r = ShellExecuteA(nullptr, "open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                return reinterpret_cast<intptr_t>(r) > 32;
            }

            const std::string args = std::string("/select,\"") + p + "\"";
            const HINSTANCE r = ShellExecuteA(nullptr, "open", "explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
            return reinterpret_cast<intptr_t>(r) > 32;
#else
            (void)path;
            return false;
#endif
        }

        const char* IssueTypeToString(Assets::AssetDatabaseValidationIssue::Type type)
        {
            using T = Assets::AssetDatabaseValidationIssue::Type;
            switch (type)
            {
            case T::MissingFileForRecord: return "MissingFileForRecord";
            case T::StaleKeyMapping: return "StaleKeyMapping";
            case T::DuplicateGuidForDifferentKeys: return "DuplicateGuidForDifferentKeys";
            default: return "Unknown";
            }
        }
    }

    void Draw(bool& open)
    {
        if (!open)
        {
            return;
        }

        EditorPanelStyle::PushPanelVisualStyle();
        if (!ImGui::Begin("Asset Diagnostics", &open))
        {
            ImGui::End();
            EditorPanelStyle::PopPanelVisualStyle();
            return;
        }

        static std::vector<Assets::AssetDatabaseValidationIssue> issues;
        static bool loaded = false;
        static std::string status;
        static bool statusIsError = false;

        auto Refresh = [&]() {
            const auto result = Assets::AssetImportPipeline::ValidateAssetDatabase();
            if (result.IsFailure())
            {
                statusIsError = true;
                status = result.GetError().GetErrorMessage();
                issues.clear();
                loaded = true;
                return;
            }

            issues = result.GetValue();
            statusIsError = false;
            status = issues.empty() ? "No issues found." : ("Issues found: " + std::to_string(issues.size()));
            loaded = true;
        };

        if (!loaded)
        {
            Refresh();
        }

        if (ImGui::Button("Refresh", ImVec2(120, 0)))
        {
            Refresh();
        }

        ImGui::SameLine();
        ImGui::TextDisabled("Records: %zu", Assets::AssetDatabase::GetInstance().GetRecordCount());

        if (!status.empty())
        {
            ImGui::SameLine();
            const ImVec4 color = statusIsError ? ImVec4(1.0f, 0.35f, 0.35f, 1.0f) : ImVec4(0.35f, 1.0f, 0.35f, 1.0f);
            ImGui::TextColored(color, "%s", status.c_str());
        }

        ImGui::Separator();

        if (issues.empty())
        {
            ImGui::TextDisabled("No validation issues.");
            ImGui::End();
            EditorPanelStyle::PopPanelVisualStyle();
            return;
        }

        ImGui::TextDisabled("Issues");
        ImGui::Separator();

        ImGui::BeginChild("IssuesList", ImVec2(0, 0), false);

        for (size_t i = 0; i < issues.size(); ++i)
        {
            auto& issue = issues[i];

            ImGui::PushID(static_cast<int>(i));
            ImGui::Text("[%s] %s", IssueTypeToString(issue.IssueType), issue.Message.c_str());
            if (!issue.Key.empty())
            {
                ImGui::TextDisabled("Key: %s", issue.Key.c_str());
            }
            if (!issue.Guid.empty())
            {
                ImGui::TextDisabled("Guid: %s", issue.Guid.c_str());
            }
            if (!issue.ResolvedPath.empty())
            {
                ImGui::TextDisabled("Path: %s", issue.ResolvedPath.c_str());
            }

            // Actions (best-effort).
            const bool hasKey = !issue.Key.empty();
            const bool hasGuid = !issue.Guid.empty();

            if (hasKey)
            {
                if (ImGui::Button("Reveal", ImVec2(90, 0)))
                {
                    const auto resolved = Assets::ResolveAssetKeyToPath(issue.Key);
                    if (resolved.IsSuccess())
                    {
                        (void)RevealInFileExplorer(resolved.GetValue());
                    }
                }
                ImGui::SameLine();

                if (ImGui::Button("Reimport", ImVec2(90, 0)))
                {
                    const auto rec = Assets::AssetDatabase::GetInstance().FindByKey(issue.Key);
                    if (rec.IsSuccess())
                    {
                        (void)Assets::AssetDatabase::GetInstance().ImportOrUpdate(rec.GetValue().Key, rec.GetValue().Type, rec.GetValue().ImporterSettings);
                        Refresh();
                    }
                }

                ImGui::SameLine();

                if (ImGui::Button("Create .meta", ImVec2(110, 0)))
                {
                    const auto resolved = Assets::ResolveAssetKeyToPath(issue.Key);
                    if (resolved.IsSuccess())
                    {
                        (void)Assets::LoadOrCreateGuid(resolved.GetValue().string(), {{"key", issue.Key}});
                        Refresh();
                    }
                }

                ImGui::SameLine();

                ImGui::BeginDisabled(issue.IssueType == Assets::AssetDatabaseValidationIssue::Type::MissingFileForRecord);
                if (ImGui::Button("Regenerate GUID", ImVec2(130, 0)))
                {
                    const auto resolved = Assets::ResolveAssetKeyToPath(issue.Key);
                    if (resolved.IsSuccess())
                    {
                        Assets::AssetType type = Assets::AssetType::Unknown;
                        nlohmann::json importerSettings = nlohmann::json::object();

                        const auto existing = Assets::AssetDatabase::GetInstance().FindByKey(issue.Key);
                        if (existing.IsSuccess())
                        {
                            type = existing.GetValue().Type;
                            importerSettings = existing.GetValue().ImporterSettings;
                        }
                        else if (auto guess = GuessTypeFromPath(resolved.GetValue()); guess.has_value())
                        {
                            type = *guess;
                        }

                        // WARNING: breaks references (Unity-style).
                        (void)Assets::ForceRegenerateGuid(resolved.GetValue().string(), {{"key", issue.Key}});
                        (void)Assets::AssetDatabase::GetInstance().ImportOrUpdate(issue.Key, type, importerSettings);
                        Refresh();
                    }
                }
                ImGui::EndDisabled();
            }

            if (hasGuid && ImGui::Button("Remove Record", ImVec2(120, 0)))
            {
                (void)Assets::AssetDatabase::GetInstance().RemoveByGuid(issue.Guid);
                Refresh();
            }

            ImGui::Separator();
            ImGui::PopID();
        }

        ImGui::EndChild();
        ImGui::End();
        EditorPanelStyle::PopPanelVisualStyle();
    }
}

