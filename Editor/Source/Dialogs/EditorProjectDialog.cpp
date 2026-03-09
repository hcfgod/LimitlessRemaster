#include "EditorProjectDialog.h"

#include "EditorRecentProjects.h"

#include "Core/Debug/Log.h"
#include "Project/ProjectManager.h"

#include "imgui/imgui.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>

namespace Limitless::EditorProjectDialog
{
    namespace
    {
        // ── Colour palette (consistent with EditorPanelStyle) ───────────────
        static constexpr ImVec4 kBgDark          = ImVec4(0.02f, 0.03f, 0.06f, 1.0f);
        static constexpr ImVec4 kSidebarBg       = ImVec4(0.04f, 0.06f, 0.10f, 1.0f);
        static constexpr ImVec4 kCardBg          = ImVec4(0.06f, 0.09f, 0.15f, 1.0f);
        static constexpr ImVec4 kCardBorder      = ImVec4(0.18f, 0.28f, 0.42f, 0.50f);
        static constexpr ImVec4 kAccent          = ImVec4(0.30f, 0.55f, 0.95f, 1.0f);
        static constexpr ImVec4 kAccentHover     = ImVec4(0.40f, 0.65f, 1.00f, 1.0f);
        static constexpr ImVec4 kAccentActive    = ImVec4(0.25f, 0.48f, 0.85f, 1.0f);
        static constexpr ImVec4 kTextPrimary     = ImVec4(0.90f, 0.92f, 0.96f, 1.0f);
        static constexpr ImVec4 kTextSecondary   = ImVec4(0.55f, 0.60f, 0.70f, 1.0f);
        static constexpr ImVec4 kInputBg         = ImVec4(0.10f, 0.15f, 0.24f, 1.0f);
        static constexpr ImVec4 kInputBgHover    = ImVec4(0.14f, 0.21f, 0.33f, 1.0f);
        static constexpr ImVec4 kInputBgActive   = ImVec4(0.18f, 0.27f, 0.40f, 1.0f);
        static constexpr ImVec4 kBtnDefault      = ImVec4(0.16f, 0.23f, 0.35f, 1.0f);
        static constexpr ImVec4 kBtnDefaultHover = ImVec4(0.22f, 0.31f, 0.46f, 1.0f);
        static constexpr ImVec4 kBtnDefaultActive= ImVec4(0.27f, 0.37f, 0.54f, 1.0f);
        static constexpr ImVec4 kRowHover        = ImVec4(0.12f, 0.18f, 0.30f, 1.0f);
        static constexpr ImVec4 kRowSelected     = ImVec4(0.16f, 0.24f, 0.40f, 1.0f);
        static constexpr ImVec4 kSeparator       = ImVec4(0.18f, 0.29f, 0.43f, 0.40f);
        static constexpr ImVec4 kErrorText       = ImVec4(1.0f, 0.40f, 0.40f, 1.0f);
        static constexpr ImVec4 kSuccessText     = ImVec4(0.35f, 1.0f, 0.45f, 1.0f);

        static constexpr float kSidebarWidth     = 240.0f;
        static constexpr float kCardRounding     = 10.0f;
        static constexpr float kBtnRounding      = 8.0f;
        static constexpr float kInputRounding    = 8.0f;
        static constexpr float kRowHeight        = 60.0f;
        static constexpr float kContentInsetX    = 24.0f;

        float GetContentStartX()
        {
            return ImGui::GetCursorStartPos().x;
        }

        float GetContentWidth()
        {
            return ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x;
        }

        float GetInsetContentWidth(const float inset)
        {
            return std::max(0.0f, GetContentWidth() - inset * 2.0f);
        }

        void SetCursorToInsetX(const float inset)
        {
            ImGui::SetCursorPosX(GetContentStartX() + inset);
        }

        void SetCursorToCenteredX(const float itemWidth)
        {
            ImGui::SetCursorPosX(GetContentStartX() + std::max(0.0f, (GetContentWidth() - itemWidth) * 0.5f));
        }

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

        // ── Styled button helpers ───────────────────────────────────────────
        bool AccentButton(const char* label, const ImVec2& size = ImVec2(0, 0))
        {
            ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccentActive);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, kBtnRounding);
            const bool pressed = ImGui::Button(label, size);
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);
            return pressed;
        }

        bool DefaultButton(const char* label, const ImVec2& size = ImVec2(0, 0))
        {
            ImGui::PushStyleColor(ImGuiCol_Button, kBtnDefault);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kBtnDefaultHover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, kBtnDefaultActive);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, kBtnRounding);
            const bool pressed = ImGui::Button(label, size);
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);
            return pressed;
        }

        // ── Sidebar mode button (full-width, highlighted when active) ───────
        bool SidebarButton(const char* label, bool isActive)
        {
            static constexpr float kSidebarButtonHorizontalInset = 10.0f;
            const float cursorX = ImGui::GetCursorPosX();
            ImGui::SetCursorPosX(cursorX + kSidebarButtonHorizontalInset);
            const float w = std::max(0.0f, ImGui::GetContentRegionAvail().x - kSidebarButtonHorizontalInset);
            if (isActive)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccentActive);
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kRowHover);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, kRowSelected);
            }
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, kBtnRounding);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16.0f, 10.0f));
            const bool pressed = ImGui::Button(label, ImVec2(w, 0));
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(3);
            return pressed;
        }
    }

    void RequestOpen(EditorProjectDialogState& state, ProjectDialogMode mode)
    {
        state.IsOpen = true;
        state.RequestOpenPopup = true;
        state.Mode = mode;
        state.ActiveRecentProjectMenuIndex = -1;
        state.RecentProjectMenuOpenedFrame = -1;
        state.RecentProjectMenuAnchorX = 0.0f;
        state.RecentProjectMenuAnchorY = 0.0f;
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

    // ── Recent-projects list as styled cards ────────────────────────────────
    static bool DrawRecentProjectsList(EditorProjectDialogState& state, bool& keepOpen)
    {
        auto& recent = Editor::EditorRecentProjects::GetInstance();
        recent.EnsureLoaded();

        const auto entries = recent.GetEntries();
        if (entries.empty())
        {
            ImGui::Dummy(ImVec2(0, 30));
            SetCursorToInsetX(kContentInsetX);
            ImGui::PushStyleColor(ImGuiCol_Text, kTextSecondary);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + GetInsetContentWidth(kContentInsetX));
            ImGui::TextUnformatted("No recent projects. Open or create a project to get started.");
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            return false;
        }

        bool opened = false;

        for (size_t i = 0; i < entries.size(); ++i)
        {
            const auto& e = entries[i];
            const std::string projectName = e.ProjectName.empty() ? "(Unnamed)" : e.ProjectName;

            ImGui::PushID(static_cast<int>(i));

            // Card background
            SetCursorToInsetX(kContentInsetX);
            const ImVec2 cardMin = ImGui::GetCursorScreenPos();
            const float cardW = GetInsetContentWidth(kContentInsetX);
            const ImVec2 cardMax = ImVec2(cardMin.x + cardW, cardMin.y + kRowHeight);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(cardMin, cardMax, ImGui::ColorConvertFloat4ToU32(kCardBg), kCardRounding);
            dl->AddRect(cardMin, cardMax, ImGui::ColorConvertFloat4ToU32(kCardBorder), kCardRounding);

            const float btnH = 26.0f;
            const float menuBtnW = 34.0f;
            const float menuBtnRightMargin = 12.0f;
            const float selectionReservedW = menuBtnW + menuBtnRightMargin + 12.0f;
            const ImVec2 windowPos = ImGui::GetWindowPos();
            const float btnLocalY = (cardMin.y - windowPos.y) + (kRowHeight - btnH) * 0.5f;
            const float menuLocalX = (cardMax.x - windowPos.x) - menuBtnW - menuBtnRightMargin;
            const ImVec2 menuBtnScreenMin = ImVec2(windowPos.x + menuLocalX, windowPos.y + btnLocalY);
            const ImVec2 menuBtnScreenMax = ImVec2(menuBtnScreenMin.x + menuBtnW, menuBtnScreenMin.y + btnH);
            const ImVec2 selectionScreenMax = ImVec2(cardMax.x - selectionReservedW, cardMax.y);

            const bool actionMenuOpen = state.ActiveRecentProjectMenuIndex >= 0;
            const bool selectionHovered = !actionMenuOpen && ImGui::IsMouseHoveringRect(cardMin, selectionScreenMax);
            if (selectionHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                CopyText(state.ProjectRootPathBuffer, e.ProjectRoot);
                state.Mode = ProjectDialogMode::Open;
            }
            const bool menuHovered = ImGui::IsMouseHoveringRect(menuBtnScreenMin, menuBtnScreenMax);
            const bool hovered = !actionMenuOpen && ImGui::IsMouseHoveringRect(cardMin, cardMax);
            if (hovered)
            {
                dl->AddRectFilled(cardMin, cardMax, ImGui::ColorConvertFloat4ToU32(kRowHover), kCardRounding);
            }
            if (selectionHovered && !menuHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                if (TryOpenRecentProject(state, e))
                {
                    opened = true;
                    keepOpen = false;
                }
            }

            // Project name
            const ImVec2 namePos = ImVec2(cardMin.x + 20.0f, cardMin.y + 12.0f);
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 1.1f,
                namePos, ImGui::ColorConvertFloat4ToU32(kTextPrimary), projectName.c_str());

            // Path
            const ImVec2 pathPos = ImVec2(cardMin.x + 20.0f, cardMin.y + 34.0f);
            dl->AddText(pathPos, ImGui::ColorConvertFloat4ToU32(kTextSecondary), e.ProjectRoot.c_str());

            ImGui::SetCursorPos(ImVec2(menuLocalX, btnLocalY));
            DefaultButton(("...##menu_" + std::to_string(i)).c_str(), ImVec2(menuBtnW, btnH));
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            {
                if (state.ActiveRecentProjectMenuIndex == static_cast<int>(i))
                {
                    state.ActiveRecentProjectMenuIndex = -1;
                    state.RecentProjectMenuOpenedFrame = -1;
                }
                else
                {
                    state.ActiveRecentProjectMenuIndex = static_cast<int>(i);
                    state.RecentProjectMenuOpenedFrame = ImGui::GetFrameCount();
                    state.RecentProjectMenuAnchorX = menuBtnScreenMax.x;
                    state.RecentProjectMenuAnchorY = menuBtnScreenMin.y;
                }
            }

            // Advance cursor past card using Dummy to properly grow the content rect
            ImGui::SetCursorScreenPos(ImVec2(cardMin.x, cardMax.y + 6.0f));
            ImGui::Dummy(ImVec2(0, 0));

            ImGui::PopID();
        }

        return opened;
    }

    static bool DrawRecentProjectActionMenu(EditorProjectDialogState& state, bool& keepOpen)
    {
        auto& recent = Editor::EditorRecentProjects::GetInstance();
        recent.EnsureLoaded();

        const auto entries = recent.GetEntries();
        if (state.ActiveRecentProjectMenuIndex < 0 || state.ActiveRecentProjectMenuIndex >= static_cast<int>(entries.size()))
        {
            state.ActiveRecentProjectMenuIndex = -1;
            state.RecentProjectMenuOpenedFrame = -1;
            return false;
        }

        bool opened = false;
        const auto& activeEntry = entries[static_cast<size_t>(state.ActiveRecentProjectMenuIndex)];
        const int currentFrame = ImGui::GetFrameCount();

        ImGui::SetNextWindowPos(ImVec2(state.RecentProjectMenuAnchorX, state.RecentProjectMenuAnchorY), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, kCardRounding);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, kSidebarBg);
        ImGui::PushStyleColor(ImGuiCol_Border, kCardBorder);

        const ImGuiWindowFlags menuFlags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoDocking;

        if (ImGui::Begin("##RecentProjectActionMenu", nullptr, menuFlags))
        {
            const bool menuHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

            if (AccentButton("Open##recentMenuOpen", ImVec2(140.0f, 0.0f)))
            {
                if (TryOpenRecentProject(state, activeEntry))
                {
                    opened = true;
                    keepOpen = false;
                }
                state.ActiveRecentProjectMenuIndex = -1;
                state.RecentProjectMenuOpenedFrame = -1;
            }

            if (DefaultButton("Remove##recentMenuRemove", ImVec2(140.0f, 0.0f)))
            {
                recent.Remove(std::filesystem::path(activeEntry.ProjectRoot));
                state.ActiveRecentProjectMenuIndex = -1;
                state.RecentProjectMenuOpenedFrame = -1;
            }

            if (currentFrame > state.RecentProjectMenuOpenedFrame && !menuHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                state.ActiveRecentProjectMenuIndex = -1;
                state.RecentProjectMenuOpenedFrame = -1;
            }
        }
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);

        return opened;
    }

    // ── Open-project content ────────────────────────────────────────────────
    static bool DrawOpenContent(EditorProjectDialogState& state, bool& keepOpen)
    {
        bool opened = false;
        constexpr float kActionButtonWidth = 180.0f;

        ImGui::Dummy(ImVec2(0, 4));
        ImGui::PushStyleColor(ImGuiCol_Text, kTextPrimary);
        ImGui::SetWindowFontScale(1.4f);
        {
            const float titleW = ImGui::CalcTextSize("Open Project").x;
            SetCursorToCenteredX(titleW);
            ImGui::TextUnformatted("Open Project");
        }
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 12));

        // Path input
        ImGui::PushStyleColor(ImGuiCol_Text, kTextSecondary);
        ImGui::SetWindowFontScale(0.9f);
        {
            const float labelW = ImGui::CalcTextSize("Project Root Folder").x;
            SetCursorToCenteredX(labelW);
            ImGui::TextUnformatted("Project Root Folder");
        }
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 4));

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, kInputRounding);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f, 12.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, kInputBg);
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, kInputBgHover);
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, kInputBgActive);
        SetCursorToInsetX(kContentInsetX);
        ImGui::PushItemWidth(GetInsetContentWidth(kContentInsetX));
        ImGui::InputText("###OpenPath", state.ProjectRootPathBuffer.data(), state.ProjectRootPathBuffer.size());
        ImGui::PopItemWidth();
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);

        ImGui::Dummy(ImVec2(0, 10));

        SetCursorToCenteredX(kActionButtonWidth);
        if (AccentButton("Open Project", ImVec2(kActionButtonWidth, 40)))
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
                opened = true;
                keepOpen = false;
            }
        }

        // Status
        if (!state.StatusMessage.empty())
        {
            ImGui::Dummy(ImVec2(0, 2));
            const ImVec4 color = state.StatusIsError ? kErrorText : kSuccessText;
            ImGui::TextColored(color, "%s", state.StatusMessage.c_str());
        }

        // Divider
        ImGui::Dummy(ImVec2(0, 20));
        ImGui::PushStyleColor(ImGuiCol_Separator, kSeparator);
        ImGui::Separator();
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 16));

        // Recent projects header
        ImGui::PushStyleColor(ImGuiCol_Text, kTextSecondary);
        ImGui::SetWindowFontScale(0.9f);
        {
            const float rpW = ImGui::CalcTextSize("Recent Projects").x;
            SetCursorToCenteredX(rpW);
            ImGui::TextUnformatted("Recent Projects");
        }
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 8));

        if (DrawRecentProjectsList(state, keepOpen))
        {
            opened = true;
        }

        return opened;
    }

    // ── Create-project content ──────────────────────────────────────────────
    static bool DrawCreateContent(EditorProjectDialogState& state, bool& keepOpen)
    {
        bool created = false;
        constexpr float kActionButtonWidth = 180.0f;

        ImGui::Dummy(ImVec2(0, 4));
        ImGui::PushStyleColor(ImGuiCol_Text, kTextPrimary);
        ImGui::SetWindowFontScale(1.4f);
        {
            const float titleW = ImGui::CalcTextSize("Create New Project").x;
            SetCursorToCenteredX(titleW);
            ImGui::TextUnformatted("Create New Project");
        }
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 12));

        // Shared input style
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, kInputRounding);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f, 12.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, kInputBg);
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, kInputBgHover);
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, kInputBgActive);

        ImGui::PushStyleColor(ImGuiCol_Text, kTextSecondary);
        ImGui::SetWindowFontScale(0.9f);
        {
            const float labelW = ImGui::CalcTextSize("Project Name").x;
            SetCursorToCenteredX(labelW);
            ImGui::TextUnformatted("Project Name");
        }
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 4));
        SetCursorToInsetX(kContentInsetX);
        ImGui::PushItemWidth(GetInsetContentWidth(kContentInsetX));
        ImGui::InputText("###CreateName", state.ProjectNameBuffer.data(), state.ProjectNameBuffer.size());
        ImGui::PopItemWidth();

        ImGui::Dummy(ImVec2(0, 10));

        ImGui::PushStyleColor(ImGuiCol_Text, kTextSecondary);
        ImGui::SetWindowFontScale(0.9f);
        {
            const float labelW = ImGui::CalcTextSize("Parent Folder").x;
            SetCursorToCenteredX(labelW);
            ImGui::TextUnformatted("Parent Folder");
        }
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 4));
        SetCursorToInsetX(kContentInsetX);
        ImGui::PushItemWidth(GetInsetContentWidth(kContentInsetX));
        ImGui::InputText("###CreatePath", state.ProjectRootPathBuffer.data(), state.ProjectRootPathBuffer.size());
        ImGui::PopItemWidth();

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);

        ImGui::Dummy(ImVec2(0, 10));

        SetCursorToCenteredX(kActionButtonWidth);
        if (AccentButton("Create Project", ImVec2(kActionButtonWidth, 40)))
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
                created = true;
                keepOpen = false;
            }
        }

        // Status
        if (!state.StatusMessage.empty())
        {
            ImGui::Dummy(ImVec2(0, 2));
            const ImVec4 color = state.StatusIsError ? kErrorText : kSuccessText;
            ImGui::TextColored(color, "%s", state.StatusMessage.c_str());
        }

        // Divider
        ImGui::Dummy(ImVec2(0, 20));
        ImGui::PushStyleColor(ImGuiCol_Separator, kSeparator);
        ImGui::Separator();
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 16));

        // Recent projects below create form too
        ImGui::PushStyleColor(ImGuiCol_Text, kTextSecondary);
        ImGui::SetWindowFontScale(0.9f);
        {
            const float rpW = ImGui::CalcTextSize("Recent Projects").x;
            SetCursorToCenteredX(rpW);
            ImGui::TextUnformatted("Recent Projects");
        }
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 8));

        if (DrawRecentProjectsList(state, keepOpen))
        {
            created = true;
        }

        return created;
    }

    // ── Main draw (full-viewport window) ────────────────────────────────────
    bool Draw(EditorProjectDialogState& state)
    {
        if (!state.IsOpen)
        {
            return false;
        }

        state.RequestOpenPopup = false;

        bool keepOpen = true;
        bool openedOrCreated = false;

        // Cover the entire viewport
        const ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        if (state.ActiveRecentProjectMenuIndex < 0)
        {
            ImGui::SetNextWindowFocus();
        }

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, kBgDark);

        const ImGuiWindowFlags windowFlags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

        if (ImGui::Begin("###ProjectBrowserFullscreen", nullptr, windowFlags))
        {
            const ImVec2 windowSize = ImGui::GetContentRegionAvail();

            // ── Left sidebar ────────────────────────────────────────────
            {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, kSidebarBg);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 28.0f));
                ImGui::BeginChild("##Sidebar", ImVec2(kSidebarWidth, windowSize.y), false,
                    ImGuiWindowFlags_NoScrollbar);

                // Branding / title
                ImGui::Dummy(ImVec2(0, 4));
                ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
                ImGui::SetWindowFontScale(1.6f);
                SetCursorToCenteredX(ImGui::CalcTextSize("Limitless").x);
                ImGui::TextUnformatted("Limitless");
                ImGui::SetWindowFontScale(1.0f);
                ImGui::PopStyleColor();

                ImGui::Dummy(ImVec2(0, 2));
                ImGui::PushStyleColor(ImGuiCol_Text, kTextSecondary);
                ImGui::SetWindowFontScale(0.9f);
                SetCursorToCenteredX(ImGui::CalcTextSize("Game Engine").x);
                ImGui::TextUnformatted("Game Engine");
                ImGui::SetWindowFontScale(1.0f);
                ImGui::PopStyleColor();

                ImGui::Dummy(ImVec2(0, 36));

                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.50f, 0.60f, 1.0f));
                ImGui::SetWindowFontScale(0.95f);
                {
                    const float labelW = ImGui::CalcTextSize("PROJECT").x;
                    SetCursorToCenteredX(labelW);
                    ImGui::TextUnformatted("PROJECT");
                }
                ImGui::SetWindowFontScale(1.0f);
                ImGui::PopStyleColor();
                ImGui::Dummy(ImVec2(0, 4));
                ImGui::PushStyleColor(ImGuiCol_Separator, kSeparator);
                ImGui::Separator();
                ImGui::PopStyleColor();
                ImGui::Dummy(ImVec2(0, 8));

                if (SidebarButton("Open Project", state.Mode == ProjectDialogMode::Open))
                {
                    state.Mode = ProjectDialogMode::Open;
                    state.StatusMessage.clear();
                }

                ImGui::Dummy(ImVec2(0, 4));

                if (SidebarButton("New Project", state.Mode == ProjectDialogMode::Create))
                {
                    state.Mode = ProjectDialogMode::Create;
                    state.StatusMessage.clear();
                }

                // Version at bottom — centered
                const float sidebarChildH = ImGui::GetWindowHeight();
                ImGui::SetCursorPosY(sidebarChildH - 40.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.30f, 0.35f, 0.45f, 1.0f));
                ImGui::SetWindowFontScale(0.85f);
                {
                    const float versionW = ImGui::CalcTextSize("v1.0.0").x;
                    SetCursorToCenteredX(versionW);
                    ImGui::TextUnformatted("v1.0.0");
                }
                ImGui::SetWindowFontScale(1.0f);
                ImGui::PopStyleColor();

                ImGui::EndChild();
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();

                // Vertical divider line between sidebar and content
                {
                    ImGui::SameLine(0, 0);
                    const ImVec2 lineTop = ImGui::GetCursorScreenPos();
                    const ImVec2 lineBot = ImVec2(lineTop.x, lineTop.y + windowSize.y);
                    ImGui::GetWindowDrawList()->AddLine(lineTop, lineBot,
                        ImGui::ColorConvertFloat4ToU32(kSeparator), 1.0f);
                }
            }

            ImGui::SameLine(0, 0);

            // ── Right content area ──────────────────────────────────────
            {
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(80.0f, 40.0f));
                ImGui::BeginChild("##Content", ImVec2(0, windowSize.y), false);

                if (state.Mode == ProjectDialogMode::Open)
                {
                    if (DrawOpenContent(state, keepOpen))
                    {
                        openedOrCreated = true;
                    }
                }
                else
                {
                    if (DrawCreateContent(state, keepOpen))
                    {
                        openedOrCreated = true;
                    }
                }

                ImGui::EndChild();
                ImGui::PopStyleVar();
            }
        }
        ImGui::End();

        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);

        if (DrawRecentProjectActionMenu(state, keepOpen))
        {
            openedOrCreated = true;
        }

        state.IsOpen = keepOpen;
        return openedOrCreated;
    }
}

