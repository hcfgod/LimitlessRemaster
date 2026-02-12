#include "EditorLayer.h"
#include "Assets/AssetLoadProgress.h"
#include "Assets/AssetPaths.h"
#include "Core/Debug/Log.h"
#include "Editor/EditorCameraController.h"
#include "Graphics/Framebuffer.h"
#include "Scene/Scene.h"
#include "Graphics/Renderer2D.h"
#include "ImGui/ImGuiLayer.h"
#include "imgui/imgui.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <vector>

namespace Limitless
{
    namespace
    {
        constexpr const char* kAssetTexturePayload = "ASSET_TEXTURE";
        constexpr const char* kAssetMovePayload = "ASSET_MOVE";

        bool IsTextureExtension(const std::filesystem::path& path)
        {
            const std::string ext = path.extension().string();
            if (ext.empty()) return false;
            auto lower = ext;
            for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return lower == ".png" || lower == ".jpg" || lower == ".jpeg" || lower == ".ppm" ||
                   lower == ".pnm" || lower == ".bmp" || lower == ".tga" || lower == ".gif";
        }

        /// Creates a new folder under parentRelPath with the given name. Returns true if successful.
        bool CreateFolderInDirectory(const std::filesystem::path& assetsDir,
                                     const std::filesystem::path& parentRelPath,
                                     const std::string& folderName)
        {
            if (folderName.empty())
                return false;

            const std::filesystem::path parentDir = assetsDir / parentRelPath;
            std::error_code ec;
            if (!std::filesystem::exists(parentDir, ec) || !std::filesystem::is_directory(parentDir, ec))
                return false;

            const std::filesystem::path candidate = parentDir / folderName;
            if (std::filesystem::exists(candidate, ec))
            {
                LT_CORE_WARN("CreateFolderInDirectory: folder already exists: {}", candidate.string());
                return false;
            }

            return std::filesystem::create_directory(candidate, ec);
        }

        /// Deletes a folder and all its contents recursively. Returns true if successful.
        bool DeleteFolderInAssets(const std::filesystem::path& assetsDir,
                                 const std::filesystem::path& folderRelPath)
        {
            const std::filesystem::path folderPath = assetsDir / folderRelPath;
            std::error_code ec;
            if (!std::filesystem::exists(folderPath, ec) || !std::filesystem::is_directory(folderPath, ec))
                return false;

            std::filesystem::remove_all(folderPath, ec);
            return !ec;
        }

        /// Renames a folder. Returns true if successful.
        bool RenameFolderInAssets(const std::filesystem::path& assetsDir,
                                 const std::filesystem::path& folderRelPath,
                                 const std::string& newName)
        {
            if (newName.empty())
                return false;

            const std::filesystem::path parentDir = assetsDir / folderRelPath.parent_path();
            const std::filesystem::path oldPath = assetsDir / folderRelPath;
            const std::filesystem::path newPath = parentDir / newName;

            std::error_code ec;
            if (!std::filesystem::exists(oldPath, ec) || !std::filesystem::is_directory(oldPath, ec))
                return false;

            if (std::filesystem::exists(newPath, ec))
            {
                LT_CORE_WARN("RenameFolderInAssets: destination already exists: {}", newPath.string());
                return false;
            }

            std::filesystem::rename(oldPath, newPath, ec);
            return !ec;
        }

        /// Moves a folder into another folder. Returns true if successful.
        bool MoveFolderToFolder(const std::string& folderKey, const std::filesystem::path& destFolderRelPath)
        {
            auto resolveResult = Assets::ResolveAssetKeyToPath(folderKey);
            if (resolveResult.IsFailure())
                return false;

            const std::filesystem::path sourcePath = resolveResult.GetValue();
            if (!std::filesystem::is_directory(sourcePath))
                return false;

            auto rootResult = Assets::FindProjectRootFromWorkingDirectory();
            if (rootResult.IsFailure())
                return false;

            const std::filesystem::path destDir = rootResult.GetValue() / "Assets" / destFolderRelPath;
            std::error_code ec;
            if (!std::filesystem::exists(destDir, ec) || !std::filesystem::is_directory(destDir, ec))
                return false;

            const std::filesystem::path folderName = sourcePath.filename();
            const std::filesystem::path destPath = destDir / folderName;

            if (sourcePath == destPath)
                return true;

            // Prevent moving folder into itself or a descendant.
            auto rel = std::filesystem::relative(destDir, sourcePath, ec);
            if (!ec)
            {
                std::string relStr = rel.generic_string();
                if (relStr.find("..") != 0 && relStr != ".")
                {
                    LT_CORE_WARN("MoveFolderToFolder: cannot move folder into itself or descendant");
                    return false;
                }
            }

            if (std::filesystem::exists(destPath, ec))
            {
                LT_CORE_WARN("MoveFolderToFolder: destination already exists: {}", destPath.string());
                return false;
            }

            std::filesystem::rename(sourcePath, destPath, ec);
            return !ec;
        }

        /// Moves an asset (file + .meta if present) to the destination folder. Returns true if successful.
        bool MoveAssetToFolder(const std::string& assetKey, const std::filesystem::path& destFolderRelPath)
        {
            auto resolveResult = Assets::ResolveAssetKeyToPath(assetKey);
            if (resolveResult.IsFailure())
                return false;

            const std::filesystem::path sourcePath = resolveResult.GetValue();
            if (!std::filesystem::is_regular_file(sourcePath))
                return false;

            auto rootResult = Assets::FindProjectRootFromWorkingDirectory();
            if (rootResult.IsFailure())
                return false;

            const std::filesystem::path destDir = rootResult.GetValue() / "Assets" / destFolderRelPath;
            std::error_code ec;
            if (!std::filesystem::exists(destDir, ec) || !std::filesystem::is_directory(destDir, ec))
                return false;

            const std::filesystem::path filename = sourcePath.filename();
            const std::filesystem::path destPath = destDir / filename;

            if (sourcePath == destPath)
                return true;  // Already in place

            if (std::filesystem::exists(destPath, ec))
            {
                LT_CORE_WARN("MoveAssetToFolder: destination already exists: {}", destPath.string());
                return false;
            }

            std::filesystem::rename(sourcePath, destPath, ec);
            if (ec)
                return false;

            // Move .meta file if present.
            const std::filesystem::path metaPath = sourcePath.parent_path() / (sourcePath.filename().string() + ".meta");
            if (std::filesystem::exists(metaPath, ec))
            {
                const std::filesystem::path destMetaPath = destDir / (filename.string() + ".meta");
                std::filesystem::rename(metaPath, destMetaPath, ec);
            }

            return true;
        }

    }  // anonymous namespace

    void EditorLayer::DrawAssetTree(const std::filesystem::path& assetsDir, const std::filesystem::path& relPath)
    {
        const std::filesystem::path currentDir = assetsDir / relPath;
        std::error_code ec;
        if (!std::filesystem::exists(currentDir, ec) || !std::filesystem::is_directory(currentDir, ec))
            return;

        std::vector<std::filesystem::path> entries;
        for (const auto& e : std::filesystem::directory_iterator(currentDir, ec))
        {
            if (ec) continue;
            const std::string name = e.path().filename().string();
            if (name.empty() || name[0] == '.') continue;
            if (name == "Cache") continue;
            std::string ext = e.path().extension().string();
            for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (ext == ".meta") continue;
            entries.push_back(e.path());
        }
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
            const bool aDir = std::filesystem::is_directory(a);
            const bool bDir = std::filesystem::is_directory(b);
            if (aDir != bDir) return aDir;
            return a.filename().string() < b.filename().string();
        });

        for (const auto& entry : entries)
        {
            const std::string filename = entry.filename().string();
            const bool isDir = std::filesystem::is_directory(entry);
            std::string assetKey = ("Assets/" + (relPath / filename).generic_string());
            const std::filesystem::path entryRelPath = relPath / filename;

            if (isDir)
            {
                const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen;
                const bool nodeOpen = ImGui::TreeNodeEx(filename.c_str(), flags);

                if (ImGui::BeginPopupContextItem())
                {
                    m_ProjectFolderPopupParent = entryRelPath;
                    if (ImGui::MenuItem("Create Folder"))
                    {
                        m_ProjectFolderPopupPending = ProjectFolderPopup::Create;
                        strncpy(m_ProjectFolderPopupBuffer, "New Folder", sizeof(m_ProjectFolderPopupBuffer) - 1);
                        m_ProjectFolderPopupBuffer[sizeof(m_ProjectFolderPopupBuffer) - 1] = '\0';
                    }
                    if (ImGui::MenuItem("Rename"))
                    {
                        m_ProjectFolderPopupPending = ProjectFolderPopup::Rename;
                        strncpy(m_ProjectFolderPopupBuffer, filename.c_str(), sizeof(m_ProjectFolderPopupBuffer) - 1);
                        m_ProjectFolderPopupBuffer[sizeof(m_ProjectFolderPopupBuffer) - 1] = '\0';
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Delete"))
                    {
                        if (DeleteFolderInAssets(assetsDir, entryRelPath))
                            LT_INFO("Deleted folder {}", entryRelPath.generic_string());
                    }
                    ImGui::EndPopup();
                }

                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetTexturePayload))
                    {
                        const char* key = static_cast<const char*>(payload->Data);
                        if (key && key[0])
                        {
                            if (std::filesystem::path(key).extension().empty())
                                MoveFolderToFolder(key, entryRelPath);
                            else
                                MoveAssetToFolder(key, entryRelPath);
                        }
                    }
                    else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetMovePayload))
                    {
                        const char* key = static_cast<const char*>(payload->Data);
                        if (key && key[0])
                        {
                            if (std::filesystem::path(key).extension().empty())
                                MoveFolderToFolder(key, entryRelPath);
                            else
                                MoveAssetToFolder(key, entryRelPath);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
                {
                    ImGui::SetDragDropPayload(kAssetMovePayload, assetKey.c_str(),
                        static_cast<uint32_t>(assetKey.size() + 1), ImGuiCond_Once);
                    ImGui::Text("%s", filename.c_str());
                    ImGui::EndDragDropSource();
                }

                if (nodeOpen)
                {
                    DrawAssetTree(assetsDir, entryRelPath);
                    ImGui::TreePop();
                }
            }
            else
            {
                const bool isTexture = IsTextureExtension(entry);
                const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                ImGui::TreeNodeEx(filename.c_str(), flags);

                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
                {
                    if (isTexture)
                        ImGui::SetDragDropPayload(kAssetTexturePayload, assetKey.c_str(),
                            static_cast<uint32_t>(assetKey.size() + 1), ImGuiCond_Once);
                    else
                        ImGui::SetDragDropPayload(kAssetMovePayload, assetKey.c_str(),
                            static_cast<uint32_t>(assetKey.size() + 1), ImGuiCond_Once);
                    ImGui::Text("%s", filename.c_str());
                    ImGui::EndDragDropSource();
                }
            }
        }
    }

    void EditorLayer::DrawProjectFolderPopups(const std::filesystem::path& assetsDir)
    {
        // Defer OpenPopup to here: calling it from inside a closing context menu doesn't work.
        if (m_ProjectFolderPopupPending == ProjectFolderPopup::Create)
        {
            ImGui::OpenPopup("CreateFolder");
            ImGui::SetNextWindowFocus();
            m_ProjectFolderPopupPending = ProjectFolderPopup::None;
            m_CreateFolderPopupOpen = true;
        }
        else if (m_ProjectFolderPopupPending == ProjectFolderPopup::Rename)
        {
            ImGui::OpenPopup("RenameFolder");
            ImGui::SetNextWindowFocus();
            m_ProjectFolderPopupPending = ProjectFolderPopup::None;
            m_RenameFolderPopupOpen = true;
        }

        // Use p_open so closing works reliably with docking (CloseCurrentPopup can fail).
        if (ImGui::BeginPopupModal("CreateFolder", &m_CreateFolderPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Create Folder");
            ImGui::Separator();
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();
            const bool create = ImGui::InputText("##Name", m_ProjectFolderPopupBuffer, sizeof(m_ProjectFolderPopupBuffer),
                ImGuiInputTextFlags_EnterReturnsTrue);

            if (ImGui::Button("Create", ImVec2(120, 0)) || create)
            {
                if (m_ProjectFolderPopupBuffer[0] != '\0')
                {
                    if (CreateFolderInDirectory(assetsDir, m_ProjectFolderPopupParent, m_ProjectFolderPopupBuffer))
                        LT_INFO("Created folder {}", m_ProjectFolderPopupBuffer);
                    m_CreateFolderPopupOpen = false;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
                m_CreateFolderPopupOpen = false;
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("RenameFolder", &m_RenameFolderPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Rename Folder");
            ImGui::Separator();
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();
            const bool rename = ImGui::InputText("##Name", m_ProjectFolderPopupBuffer, sizeof(m_ProjectFolderPopupBuffer),
                ImGuiInputTextFlags_EnterReturnsTrue);

            if (ImGui::Button("Rename", ImVec2(120, 0)) || rename)
            {
                if (m_ProjectFolderPopupBuffer[0] != '\0')
                {
                    if (RenameFolderInAssets(assetsDir, m_ProjectFolderPopupParent, m_ProjectFolderPopupBuffer))
                        LT_INFO("Renamed folder to {}", m_ProjectFolderPopupBuffer);
                    m_RenameFolderPopupOpen = false;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
                m_RenameFolderPopupOpen = false;
            ImGui::EndPopup();
        }
    }

    EditorLayer::EditorLayer()
        : Layer("EditorLayer")
    {
    }

    EditorLayer::~EditorLayer() = default;

    void EditorLayer::OnAttach()
    {
        Renderer2D::Initialize();

        m_Scene = std::make_unique<Scene>();
        auto spriteEntity = m_Scene->CreateEntity("Sprite (Test)");
        auto& transform = m_Scene->GetRegistry().get<TransformComponent>(spriteEntity);
        transform.Position = glm::vec3(-0.5f, -0.5f, 0.0f);
        transform.Scale = glm::vec3(1.0f, 1.0f, 1.0f);
        auto& sprite = m_Scene->GetRegistry().emplace<SpriteComponent>(spriteEntity);
        sprite.Color = glm::vec4(0.2f, 0.6f, 0.9f, 1.0f);

        // Create editor camera (3D perspective).
        CameraManager::Perspective3DCreateInfo cameraInfo{};
        cameraInfo.Name = "EditorCamera";
        cameraInfo.Usage = CameraUsage::Editor;
        cameraInfo.ViewportWidthPixels = m_ViewportWidthPixels;
        cameraInfo.ViewportHeightPixels = m_ViewportHeightPixels;
        cameraInfo.FieldOfViewYDegrees = 60.0f;
        cameraInfo.NearPlane = 0.1f;
        cameraInfo.FarPlane = 1000.0f;

        m_CameraId = m_CameraManager.CreatePerspective3D(cameraInfo);
        m_CameraManager.SetActiveCamera(m_CameraId);

        if (auto* camera = m_CameraManager.GetPerspective3D(m_CameraId))
        {
            camera->SetPosition(glm::vec3(0.0f, 0.0f, 2.0f));
            camera->SetYawPitchDegrees(-90.0f, 0.0f);
        }

        m_EditorCameraController = std::make_unique<EditorCameraController>();
        EditorCameraController::Settings editorCameraSettings{};
        editorCameraSettings.InputActionsAssetKey = "Assets/InputActions/EditorCamera.inputactions.json";
        editorCameraSettings.UseOverrideActionAsset = true;
        m_EditorCameraController->Initialize(m_CameraManager, m_CameraId, editorCameraSettings);

        // Create initial viewport framebuffer (min size to avoid 0x0).
        EnsureViewportFramebuffer(m_ViewportWidthPixels, m_ViewportHeightPixels);

        LT_INFO("EditorLayer attached");
    }

    void EditorLayer::OnDetach()
    {
        if (m_EditorCameraController)
        {
            m_EditorCameraController->Shutdown();
            m_EditorCameraController.reset();
        }

        m_Scene.reset();
        m_ViewportFramebuffer.reset();
        Renderer2D::Shutdown();

        LT_INFO("EditorLayer detached");
    }

    void EditorLayer::OnUpdate(float deltaTime)
    {
        // Only process editor camera input when viewport is focused.
        m_EditorCameraController->SetInputEnabled(m_ViewportFocused);
        m_EditorCameraController->Update(deltaTime);
    }

    void EditorLayer::OnRender()
    {
        DrawMenuBar();
        DrawViewportPanel();
        DrawScenePanel();
        DrawInspectorPanel();
        DrawProjectPanel();

        if (m_ShowDemoWindow)
            ImGui::ShowDemoWindow(&m_ShowDemoWindow);
    }

    void EditorLayer::OnWindowResize(Events::WindowResizeEvent& event)
    {
        // On fullscreen/resize, the viewport size from ImGui::GetContentRegionAvail() may lag.
        // Update viewport and camera with the new window size to avoid rendering artifacts.
        const uint32_t w = event.GetWidth();
        const uint32_t h = event.GetHeight();
        if (w > 0 && h > 0)
        {
            m_ViewportWidthPixels = w;
            m_ViewportHeightPixels = h;
            EnsureViewportFramebuffer(w, h);
            m_EditorCameraController->OnWindowResize(w, h);
        }
    }

    void EditorLayer::DrawMenuBar()
    {
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("New")) {}
                if (ImGui::MenuItem("Open")) {}
                if (ImGui::MenuItem("Save")) {}
                ImGui::Separator();
                if (ImGui::MenuItem("Exit")) { Application::GetInstance().SetRunning(false); }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Edit"))
            {
                if (ImGui::MenuItem("Undo")) {}
                if (ImGui::MenuItem("Redo")) {}
                ImGui::Separator();
                if (ImGui::MenuItem("Preferences")) {}
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("View"))
            {
                ImGui::MenuItem("Scene", nullptr, nullptr);
                ImGui::MenuItem("Inspector", nullptr, nullptr);
                ImGui::MenuItem("Viewport", nullptr, nullptr);
                ImGui::MenuItem("Project", nullptr, nullptr);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Window"))
            {
                ImGui::MenuItem("Demo Window", nullptr, &m_ShowDemoWindow);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Help"))
            {
                if (ImGui::MenuItem("About")) {}
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }
    }

    void EditorLayer::DrawViewportPanel()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Viewport");

        m_ViewportFocused = ImGui::IsWindowFocused();
        m_ViewportHovered = ImGui::IsWindowHovered();

        // Skip render only when collapsed.
        const bool skipRender = ImGui::IsWindowCollapsed();

        ImVec2 viewportSize = ImGui::GetContentRegionAvail();
        uint32_t width = static_cast<uint32_t>(viewportSize.x);
        uint32_t height = static_cast<uint32_t>(viewportSize.y);

        if (!skipRender && width > 0 && height > 0)
        {
            EnsureViewportFramebuffer(width, height);

            if (m_ViewportWidthPixels != width || m_ViewportHeightPixels != height)
            {
                m_ViewportWidthPixels = width;
                m_ViewportHeightPixels = height;
                m_EditorCameraController->OnWindowResize(width, height);
            }

            const Camera* camera = m_CameraManager.GetCamera(m_CameraId);
            if (camera && m_Scene && m_ViewportFramebuffer)
            {
                SceneRenderer::RenderToViewport(*m_Scene, *camera, m_ViewportFramebuffer, width, height);
            }

            if (m_ViewportFramebuffer && m_ViewportFramebuffer->GetColorAttachment())
            {
                ImGui::Image(
                    (ImTextureID)(void*)(uintptr_t)m_ViewportFramebuffer->GetColorAttachment()->GetRendererID(),
                    ImVec2(static_cast<float>(width), static_cast<float>(height)),
                    ImVec2(0, 1),
                    ImVec2(1, 0));

                if (!Renderer2D::IsShaderReady())
                {
                    ImVec2 minPos = ImGui::GetItemRectMin();
                    ImVec2 maxPos = ImGui::GetItemRectMax();
                    ImVec2 center = ImVec2((minPos.x + maxPos.x) * 0.5f, (minPos.y + maxPos.y) * 0.5f);

                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    drawList->AddRectFilled(minPos, maxPos, IM_COL32(0, 0, 0, 160));

                    const char* loadingText = "Loading shader...";
                    float progressValue = 0.0f;
                    const auto progressInfo = Assets::AssetLoadProgress::GetProgress(Renderer2D::GetDefaultShaderKey());
                    if (progressInfo.has_value())
                    {
                        loadingText = progressInfo->Status.empty() ? "Loading shader..." : progressInfo->Status.c_str();
                        progressValue = progressInfo->Progress;
                    }
                    else
                    {
                        progressValue = fmodf(static_cast<float>(ImGui::GetTime() * 0.8), 1.0f);
                    }

                    ImVec2 textSize = ImGui::CalcTextSize(loadingText);
                    drawList->AddText(ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f - 24.0f),
                        IM_COL32(255, 255, 255, 255), loadingText);

                    float barWidth = 200.0f;
                    float barHeight = 8.0f;
                    ImVec2 barMin = ImVec2(center.x - barWidth * 0.5f, center.y - barHeight * 0.5f + 8.0f);
                    ImVec2 barMax = ImVec2(center.x + barWidth * 0.5f, center.y + barHeight * 0.5f + 8.0f);
                    drawList->AddRectFilled(barMin, barMax, IM_COL32(50, 50, 55, 255));
                    ImVec2 fillMax = ImVec2(barMin.x + barWidth * progressValue, barMax.y);
                    drawList->AddRectFilled(barMin, fillMax, IM_COL32(80, 140, 220, 255));
                }
            }
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }

    void EditorLayer::DrawScenePanel()
    {
        ImGui::Begin("Scene");

        if (m_Scene)
        {
            if (ImGui::TreeNodeEx("Scene Root", ImGuiTreeNodeFlags_DefaultOpen))
            {
                auto view = m_Scene->GetRegistry().view<TagComponent>();
                for (entt::entity entity : view)
                {
                    const auto& tag = view.get<TagComponent>(entity);
                    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                    if (m_SelectedEntity == entity)
                        flags |= ImGuiTreeNodeFlags_Selected;
                    ImGui::TreeNodeEx(tag.Tag.c_str(), flags);
                    if (ImGui::IsItemClicked())
                        m_SelectedEntity = entity;
                    // No TreePop for leaf nodes: NoTreePushOnOpen means they don't push to the stack.
                }
                ImGui::TreePop();
            }
        }

        ImGui::End();
    }

    void EditorLayer::DrawInspectorPanel()
    {
        ImGui::Begin("Inspector");

        if (!m_Scene || m_SelectedEntity == entt::null || !m_Scene->IsValid(m_SelectedEntity))
        {
            ImGui::Text("Select an object to edit.");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text("No selection.");
        }
        else
        {
            auto& registry = m_Scene->GetRegistry();
            if (auto* tag = registry.try_get<TagComponent>(m_SelectedEntity))
            {
                ImGui::Text("Tag: %s", tag->Tag.c_str());
            }
            if (auto* transform = registry.try_get<TransformComponent>(m_SelectedEntity))
            {
                if (ImGui::TreeNodeEx("Transform", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::DragFloat3("Position", &transform->Position.x, 0.1f);
                    ImGui::DragFloat3("Rotation", &transform->Rotation.x, 1.0f);
                    ImGui::DragFloat3("Scale", &transform->Scale.x, 0.1f);
                    ImGui::TreePop();
                }
            }
            if (auto* sprite = registry.try_get<SpriteComponent>(m_SelectedEntity))
            {
                if (ImGui::TreeNodeEx("Sprite", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    // Image slot: drag-drop target for textures from Project panel.
                    ImGui::Text("Image");
                    ImGui::SameLine(80);
                    const char* label = sprite->TextureKey.empty() ? "None" : sprite->TextureKey.c_str();
                    ImGui::Button(label, ImVec2(ImGui::GetContentRegionAvail().x - 60, 0));
                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetTexturePayload))
                        {
                            const char* key = static_cast<const char*>(payload->Data);
                            if (key && key[0])
                            {
                                sprite->TextureKey = key;
                                sprite->CachedTexture.reset();  // Force reload
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                    if (!sprite->TextureKey.empty())
                    {
                        ImGui::SameLine();
                        if (ImGui::Button("Clear"))
                        {
                            sprite->TextureKey.clear();
                            sprite->CachedTexture.reset();
                        }
                    }
                    ImGui::ColorEdit4("Color", &sprite->Color.r);
                    ImGui::TreePop();
                }
            }
        }

        ImGui::End();
    }

    void EditorLayer::DrawProjectPanel()
    {
        ImGui::Begin("Project");

        auto rootResult = Assets::FindProjectRootFromWorkingDirectory();
        if (rootResult.IsFailure())
        {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Could not find Assets folder.");
            ImGui::End();
            return;
        }

        const std::filesystem::path assetsDir = rootResult.GetValue() / "Assets";
        std::error_code ec;
        if (!std::filesystem::exists(assetsDir, ec) || !std::filesystem::is_directory(assetsDir, ec))
        {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Assets directory not found.");
            ImGui::End();
            return;
        }

        // Right-click on empty space: Create Folder at Assets root.
        if (ImGui::BeginPopupContextWindow("ProjectContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            if (ImGui::MenuItem("Create Folder"))
            {
                m_ProjectFolderPopupParent = "";
                m_ProjectFolderPopupPending = ProjectFolderPopup::Create;
                strncpy(m_ProjectFolderPopupBuffer, "New Folder", sizeof(m_ProjectFolderPopupBuffer) - 1);
                m_ProjectFolderPopupBuffer[sizeof(m_ProjectFolderPopupBuffer) - 1] = '\0';
            }
            ImGui::EndPopup();
        }

        if (ImGui::TreeNodeEx("Assets", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::BeginPopupContextItem())
            {
                m_ProjectFolderPopupParent = "";
                if (ImGui::MenuItem("Create Folder"))
                {
                    m_ProjectFolderPopupPending = ProjectFolderPopup::Create;
                    strncpy(m_ProjectFolderPopupBuffer, "New Folder", sizeof(m_ProjectFolderPopupBuffer) - 1);
                    m_ProjectFolderPopupBuffer[sizeof(m_ProjectFolderPopupBuffer) - 1] = '\0';
                }
                ImGui::EndPopup();
            }

            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetTexturePayload))
                {
                    const char* key = static_cast<const char*>(payload->Data);
                    if (key && key[0])
                    {
                        if (std::filesystem::path(key).extension().empty())
                            MoveFolderToFolder(key, "");
                        else
                            MoveAssetToFolder(key, "");
                    }
                }
                else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetMovePayload))
                {
                    const char* key = static_cast<const char*>(payload->Data);
                    if (key && key[0])
                    {
                        if (std::filesystem::path(key).extension().empty())
                            MoveFolderToFolder(key, "");
                        else
                            MoveAssetToFolder(key, "");
                    }
                }
                ImGui::EndDragDropTarget();
            }

            DrawAssetTree(assetsDir, "");
            ImGui::TreePop();
        }

        DrawProjectFolderPopups(assetsDir);

        ImGui::End();
    }

    void EditorLayer::EnsureViewportFramebuffer(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0)
            return;

        // Clamp to minimum size to avoid GL_INVALID_OPERATION (0x502) with tiny framebuffers.
        constexpr uint32_t kMinViewportSize = 32;
        width = (width < kMinViewportSize) ? kMinViewportSize : width;
        height = (height < kMinViewportSize) ? kMinViewportSize : height;

        if (!m_ViewportFramebuffer || m_ViewportFramebuffer->GetWidth() != width || m_ViewportFramebuffer->GetHeight() != height)
        {
            FramebufferSpecification spec;
            spec.Width = width;
            spec.Height = height;
            spec.Samples = 1;
            spec.DepthAttachment = true;
            spec.StencilAttachment = false;

            m_ViewportFramebuffer = Framebuffer::Create(spec);
            m_ViewportWidthPixels = width;
            m_ViewportHeightPixels = height;
            m_EditorCameraController->OnWindowResize(width, height);
        }
    }

}  // namespace Limitless
