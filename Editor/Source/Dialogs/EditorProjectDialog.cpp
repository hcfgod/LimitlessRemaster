#include "EditorProjectDialog.h"

#include "EditorRecentProjects.h"

#include "Core/Debug/Log.h"
#include "Project/ProjectManager.h"

#include "imgui/imgui.h"

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>

namespace Limitless::EditorProjectDialog
{
    namespace
    {
        // IMPORTANT:
        // This popup must NOT use the same name as the dockable "Project" panel window.
        // If it does, ImGui can assert when closing popups due to window/popup flag collisions.
        static constexpr const char* kProjectBrowserPopupName = "Project Browser###ProjectBrowserPopup";

        void CopyText(std::array<char, 512>& dst, const std::string& src)
        {
            std::snprintf(dst.data(), dst.size(), "%s", src.c_str());
        }

        void CopyText(std::array<char, 256>& dst, const std::string& src)
        {
            std::snprintf(dst.data(), dst.size(), "%s", src.c_str());
        }

        std::filesystem::path ToPath(const std::array<char, 512>& buf)
        {
            const std::string s = buf.data();
            if (s.empty())
            {
                return {};
            }
            return std::filesystem::path(s);
        }

        std::string ToString(const std::array<char, 256>& buf)
        {
            return std::string(buf.data());
        }

        bool PathAlreadyEndsWithProjectName(const std::filesystem::path& folderPath, const std::string& projectName)
        {
            if (folderPath.empty() || projectName.empty() || !folderPath.has_filename())
            {
                return false;
            }

            const std::string tail = folderPath.filename().string();
#if defined(LT_PLATFORM_WINDOWS)
            if (tail.size() != projectName.size())
            {
                return false;
            }
            for (size_t index = 0; index < tail.size(); ++index)
            {
                const char left = static_cast<char>(std::tolower(static_cast<unsigned char>(tail[index])));
                const char right = static_cast<char>(std::tolower(static_cast<unsigned char>(projectName[index])));
                if (left != right)
                {
                    return false;
                }
            }
            return true;
#else
            return tail == projectName;
#endif
        }

        void SetStatus(EditorProjectDialogState& state, bool isError, const std::string& msg)
        {
            state.StatusIsError = isError;
            state.StatusMessage = msg;
        }
    }

    void RequestOpen(EditorProjectDialogState& state, ProjectDialogMode mode)
    {
        state.IsOpen = true;
        state.RequestOpenPopup = true;
        state.Mode = mode;
        state.StatusMessage.clear();
        state.StatusIsError = false;

        // Pre-fill with current project root when available.
        const auto& pm = Limitless::Project::ProjectManager::GetInstance();
        if (pm.HasOpenProject())
        {
            CopyText(state.ProjectRootPathBuffer, pm.GetProjectRoot().string());
        }
        else
        {
            // Default to working directory to reduce typing.
            CopyText(state.ProjectRootPathBuffer, std::filesystem::current_path().string());
        }

        if (mode == ProjectDialogMode::Create)
        {
            CopyText(state.ProjectNameBuffer, "New Project");
        }
    }

    static bool TryOpenRecentProject(EditorProjectDialogState& state, const Editor::RecentProjectEntry& entry)
    {
        const std::filesystem::path root(entry.ProjectRoot);
        Project::ProjectManager::GetInstance().CloseProject();
        const auto result = Project::ProjectManager::GetInstance().OpenProjectRoot(root);
        if (result.IsFailure())
        {
            SetStatus(state, true, result.GetError().GetErrorMessage());
            return false;
        }

        const auto def = Project::ProjectManager::GetInstance().GetProjectDefinition();
        const std::string name = def.has_value() ? def->ProjectName : std::string{};
        Editor::EditorRecentProjects::GetInstance().AddOrUpdate(root, name);
        SetStatus(state, false, "Project opened.");
        return true;
    }

    static bool DrawRecentProjectsList(EditorProjectDialogState& state)
    {
        auto& recent = Editor::EditorRecentProjects::GetInstance();
        recent.EnsureLoaded();

        const auto entries = recent.GetEntries();
        if (entries.empty())
        {
            ImGui::TextDisabled("No recent projects yet.");
            return false;
        }

        ImGui::TextDisabled("Recent Projects");
        ImGui::Separator();

        if (ImGui::BeginTable("RecentProjectsTable", 3, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("Project", ImGuiTableColumnFlags_WidthStretch, 0.30f);
            ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch, 0.55f);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableHeadersRow();

            for (size_t i = 0; i < entries.size(); ++i)
            {
                const auto& e = entries[i];
                const std::string projectName = e.ProjectName.empty() ? "(Unnamed)" : e.ProjectName;
                const std::string rowLabel = projectName + "###recent_" + std::to_string(i);

                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                if (ImGui::Selectable(rowLabel.c_str()))
                {
                    CopyText(state.ProjectRootPathBuffer, e.ProjectRoot);
                    state.Mode = ProjectDialogMode::Open;
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    if (TryOpenRecentProject(state, e))
                    {
                        ImGui::EndTable();
                        return true;
                    }
                }

                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("%s", e.ProjectRoot.c_str());

                ImGui::TableSetColumnIndex(2);
                const std::string openLabel = "Open###open_recent_" + std::to_string(i);
                if (ImGui::SmallButton(openLabel.c_str()))
                {
                    if (TryOpenRecentProject(state, e))
                    {
                        ImGui::EndTable();
                        return true;
                    }
                }
                ImGui::SameLine();
                const std::string removeLabel = "Remove###remove_recent_" + std::to_string(i);
                if (ImGui::SmallButton(removeLabel.c_str()))
                {
                    recent.Remove(std::filesystem::path(e.ProjectRoot));
                }
            }

            ImGui::EndTable();
        }

        return false;
    }

    bool Draw(EditorProjectDialogState& state)
    {
        if (!state.IsOpen)
        {
            return false;
        }

        if (state.RequestOpenPopup)
        {
            ImGui::OpenPopup(kProjectBrowserPopupName);
            state.RequestOpenPopup = false;
        }
        bool keepOpen = true;
        bool openedOrCreated = false;

        if (ImGui::BeginPopupModal(kProjectBrowserPopupName, &keepOpen, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Project System");
            ImGui::Separator();

            if (ImGui::BeginTabBar("ProjectTabs"))
            {
                if (ImGui::BeginTabItem("Open"))
                {
                    state.Mode = ProjectDialogMode::Open;
                    ImGui::InputText("Project Root Folder", state.ProjectRootPathBuffer.data(), state.ProjectRootPathBuffer.size());

                    if (ImGui::Button("Open", ImVec2(120, 0)))
                    {
                        const auto root = ToPath(state.ProjectRootPathBuffer);
                        Project::ProjectManager::GetInstance().CloseProject();
                        const auto result = Project::ProjectManager::GetInstance().OpenProjectRoot(root);
                        if (result.IsFailure())
                        {
                            SetStatus(state, true, result.GetError().GetErrorMessage());
                        }
                        else
                        {
                            const auto def = Project::ProjectManager::GetInstance().GetProjectDefinition();
                            const std::string name = def.has_value() ? def->ProjectName : std::string{};
                            Editor::EditorRecentProjects::GetInstance().AddOrUpdate(root, name);
                            SetStatus(state, false, "Project opened.");
                            openedOrCreated = true;
                            keepOpen = false;
                        }
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(120, 0)))
                    {
                        keepOpen = false;
                    }

                    ImGui::Separator();
                    if (DrawRecentProjectsList(state))
                    {
                        openedOrCreated = true;
                        keepOpen = false;
                    }

                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Create"))
                {
                    state.Mode = ProjectDialogMode::Create;
                    ImGui::InputText("Project Root Folder", state.ProjectRootPathBuffer.data(), state.ProjectRootPathBuffer.size());
                    ImGui::InputText("Project Name", state.ProjectNameBuffer.data(), state.ProjectNameBuffer.size());

                    if (ImGui::Button("Create", ImVec2(120, 0)))
                    {
                        const std::string name = ToString(state.ProjectNameBuffer);
                        const auto selectedFolder = ToPath(state.ProjectRootPathBuffer);

                        // Unity-style project creation:
                        // treat the chosen folder as the parent directory and create <Folder>/<ProjectName>.
                        const std::filesystem::path projectRoot =
                            PathAlreadyEndsWithProjectName(selectedFolder, name)
                                ? selectedFolder
                                : (selectedFolder / name);

                        Project::ProjectManager::GetInstance().CloseProject();
                        const auto result = Project::ProjectManager::GetInstance().CreateProjectRoot(projectRoot, name);
                        if (result.IsFailure())
                        {
                            SetStatus(state, true, result.GetError().GetErrorMessage());
                        }
                        else
                        {
                            Editor::EditorRecentProjects::GetInstance().AddOrUpdate(projectRoot, name);
                            SetStatus(state, false, "Project created and opened.");
                            openedOrCreated = true;
                            keepOpen = false;
                        }
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(120, 0)))
                    {
                        keepOpen = false;
                    }

                    ImGui::Separator();
                    if (DrawRecentProjectsList(state))
                    {
                        openedOrCreated = true;
                        keepOpen = false;
                    }

                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }

            if (!state.StatusMessage.empty())
            {
                const ImVec4 color = state.StatusIsError ? ImVec4(1.0f, 0.35f, 0.35f, 1.0f) : ImVec4(0.35f, 1.0f, 0.35f, 1.0f);
                ImGui::Separator();
                ImGui::TextColored(color, "%s", state.StatusMessage.c_str());
            }

            ImGui::EndPopup();
        }

        state.IsOpen = keepOpen;
        return openedOrCreated;
    }
}

