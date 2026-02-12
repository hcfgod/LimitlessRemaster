#include "EditorLayer.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetPaths.h"
#include "Assets/AssetTypes.h"
#include "Assets/TextureAsset.h"
#include "Core/Debug/Log.h"
#include "Editor/EditorCameraController.h"
#include "EditorInspectorPanel.h"
#include "EditorMenuBar.h"
#include "EditorPlayMode.h"
#include "EditorProjectPanel.h"
#include "EditorRuntimeOperations.h"
#include "EditorScenePanel.h"
#include "EditorViewportPanel.h"
#include "Graphics/Camera/PerspectiveCamera3D.h"
#include "ImGui/ImGuiLayer.h"
#include "Scene/Scene.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <vector>

namespace Limitless
{
    namespace
    {
        constexpr const char* kAssetTexturePayload = "ASSET_TEXTURE";
        constexpr const char* kAssetMovePayload = "ASSET_MOVE";
        constexpr const char* kAssetScenePayload = "ASSET_SCENE";
        constexpr const char* kAssetMaterialPayload = "ASSET_MATERIAL";
        constexpr const char* kAssetShaderPayload = "ASSET_SHADER";
        constexpr const char* kDefaultSceneFileName = "New Scene.scene.json";
        constexpr const char* kSceneFileSuffix = ".scene.json";

        std::string SceneDisplayNameFromFileName(const std::string& fileName)
        {
            std::string lowerFileName = fileName;
            std::transform(lowerFileName.begin(), lowerFileName.end(), lowerFileName.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });

            const std::string lowerSuffix = kSceneFileSuffix;
            if (lowerFileName.size() >= lowerSuffix.size() &&
                lowerFileName.rfind(lowerSuffix) == (lowerFileName.size() - lowerSuffix.size()))
            {
                return fileName.substr(0, fileName.size() - lowerSuffix.size());
            }
            return fileName;
        }

        std::string NormalizeSceneFileName(const char* rawName)
        {
            std::string fileName = rawName ? rawName : "";
            while (!fileName.empty() && std::isspace(static_cast<unsigned char>(fileName.front())))
                fileName.erase(fileName.begin());
            while (!fileName.empty() && std::isspace(static_cast<unsigned char>(fileName.back())))
                fileName.pop_back();

            if (fileName.empty())
                fileName = "New Scene";

            for (char& character : fileName)
            {
                if (character == '/' || character == '\\' || character == ':' || character == '*' ||
                    character == '?' || character == '"' || character == '<' || character == '>' || character == '|')
                    character = '_';
            }

            std::string lowerFileName = fileName;
            std::transform(lowerFileName.begin(), lowerFileName.end(), lowerFileName.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });

            std::string lowerSuffix = kSceneFileSuffix;
            if (lowerFileName.size() < lowerSuffix.size() ||
                lowerFileName.rfind(lowerSuffix) != (lowerFileName.size() - lowerSuffix.size()))
            {
                fileName += kSceneFileSuffix;
            }
            return fileName;
        }

    }  // anonymous namespace

    EditorLayer::EditorLayer()
        : Layer("EditorLayer")
    {
        const std::string defaultName = SceneDisplayNameFromFileName(kDefaultSceneFileName);
        const size_t copyCount = std::min(defaultName.size(), m_SaveSceneFileNameBuffer.size() - 1);
        std::copy_n(defaultName.c_str(), copyCount, m_SaveSceneFileNameBuffer.begin());
        m_SaveSceneFileNameBuffer[copyCount] = '\0';
    }

    EditorLayer::~EditorLayer() = default;

    void EditorLayer::OnAttach()
    {
        EditorRuntimeOperations::Attach(
            m_ViewportWidthPixels,
            m_ViewportHeightPixels,
            m_Scene,
            m_CameraManager,
            m_EditorCameraId,
            m_EditorCameraController,
            m_ViewportFramebuffer);
    }

    void EditorLayer::OnDetach()
    {
        EditorRuntimeOperations::Detach(
            m_Scene,
            m_EditSceneStored,
            m_EditorCameraController,
            m_ViewportFramebuffer);
    }

    void EditorLayer::OnUpdate(float deltaTime)
    {
        EditorPlayMode::SyncSceneCamera(
            m_PlayModeState,
            m_Scene.get(),
            m_CameraManager,
            m_CachedGameplayCameraId,
            m_CreatedGameplayCameraFromScene);

        const ImGuiIO& io = ImGui::GetIO();
        EditorRuntimeOperations::Update(
            m_PlayModeState,
            m_ViewportHovered,
            io.WantTextInput,
            deltaTime,
            m_EditorCameraController.get());

        const bool saveModifierDown = io.KeyCtrl || io.KeySuper;
        if (saveModifierDown && !io.WantTextInput)
        {
            if (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false))
                SaveSceneAs();
            else if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false))
                SaveScene();
        }
    }

    void EditorLayer::OnRender()
    {
        DrawMenuBar();
        DrawViewportPanel();
        DrawScenePanel();
        DrawInspectorPanel();
        DrawProjectPanel();
        DrawSaveScenePopup();

        if (m_ShowDemoWindow)
            ImGui::ShowDemoWindow(&m_ShowDemoWindow);
    }

    void EditorLayer::OnWindowResize(Events::WindowResizeEvent& event)
    {
        EditorRuntimeOperations::HandleWindowResize(
            event.GetWidth(),
            event.GetHeight(),
            m_ViewportWidthPixels,
            m_ViewportHeightPixels,
            m_ViewportFramebuffer,
            m_EditorCameraController.get());
    }

    void EditorLayer::DrawMenuBar()
    {
        EditorMenuBar::Draw(
            m_PlayModeState,
            m_ShowDemoWindow,
            [this]() { NewScene(); },
            [this]() { SaveScene(); },
            [this]() { SaveSceneAs(); },
            [this]() { EnterPlayMode(); },
            [this]() { ExitPlayMode(); },
            [this]() { TogglePausePlayMode(); });
    }

    void EditorLayer::DrawViewportPanel()
    {
        EditorViewportPanel::Draw(
            m_ViewportWidthPixels,
            m_ViewportHeightPixels,
            m_ViewportFramebuffer,
            m_ViewportFocused,
            m_ViewportHovered,
            m_EditorCameraController.get(),
            m_CameraManager,
            m_Scene.get(),
            m_PlayModeState,
            m_PlayModeMissingGameplayCamera,
            [this](uint32_t width, uint32_t height) { EnsureViewportFramebuffer(width, height); },
            kAssetScenePayload,
            [this](const std::string& assetKey) { LoadSceneFromAssetKey(assetKey); },
            m_SelectedEntity,
            kAssetMaterialPayload,
            m_SelectedTextureAssetKey,
            m_CachedTextureAsset,
            m_SelectedMaterialAssetKey,
            m_CachedMaterialAsset);
    }

    void EditorLayer::DrawScenePanel()
    {
        EditorScenePanel::Draw(
            m_Scene.get(),
            m_ScenePanelState,
            m_SelectedEntity,
            m_SelectedTextureAssetKey,
            m_CachedTextureAsset,
            m_SelectedMaterialAssetKey,
            m_CachedMaterialAsset,
            kAssetMaterialPayload);
    }

    void EditorLayer::DrawInspectorPanel()
    {
        EditorInspectorPanel::Draw(
            m_Scene.get(),
            m_SelectedEntity,
            kAssetTexturePayload,
            m_SelectedTextureAssetKey,
            m_CachedTextureAsset,
            kAssetMaterialPayload,
            kAssetShaderPayload,
            m_SelectedMaterialAssetKey,
            m_CachedMaterialAsset);
    }

    void EditorLayer::DrawProjectPanel()
    {
        EditorProjectPanel::Draw(
            m_ProjectPanelState,
            m_SelectedEntity,
            m_SelectedTextureAssetKey,
            m_CachedTextureAsset,
            m_SelectedMaterialAssetKey,
            m_CachedMaterialAsset,
            kAssetTexturePayload,
            kAssetMovePayload,
            kAssetScenePayload,
            kAssetMaterialPayload,
            kAssetShaderPayload,
            [this](const std::string& assetKey) { LoadSceneFromAssetKey(assetKey); },
            [this](const std::filesystem::path& relativeFolderPath) {
                const std::string createdSceneAssetKey = CreateSceneAssetInFolder(relativeFolderPath);
                if (!createdSceneAssetKey.empty())
                    LoadSceneFromAssetKey(createdSceneAssetKey);
            });
    }

    void EditorLayer::EnsureViewportFramebuffer(uint32_t width, uint32_t height)
    {
        EditorRuntimeOperations::EnsureViewportFramebuffer(
            width,
            height,
            m_ViewportFramebuffer,
            m_ViewportWidthPixels,
            m_ViewportHeightPixels,
            m_EditorCameraController.get());
    }

    void EditorLayer::EnterPlayMode()
    {
        EditorPlayMode::Enter(
            m_PlayModeState,
            m_Scene,
            m_EditSceneStored,
            m_CameraManager,
            m_EditorCameraId,
            m_ViewportWidthPixels,
            m_ViewportHeightPixels,
            m_CachedGameplayCameraId,
            m_CreatedGameplayCameraFromScene,
            m_PlayModeMissingGameplayCamera,
            m_SelectedEntity,
            m_SelectedTextureAssetKey,
            m_CachedTextureAsset);
    }

    void EditorLayer::ExitPlayMode()
    {
        EditorPlayMode::Exit(
            m_PlayModeState,
            m_Scene,
            m_EditSceneStored,
            m_CameraManager,
            m_EditorCameraId,
            m_CachedGameplayCameraId,
            m_CreatedGameplayCameraFromScene,
            m_PlayModeMissingGameplayCamera,
            m_SelectedEntity,
            m_SelectedTextureAssetKey,
            m_CachedTextureAsset);
    }

    void EditorLayer::TogglePausePlayMode()
    {
        EditorPlayMode::TogglePause(m_PlayModeState);
    }

    void EditorLayer::NewScene()
    {
        if (m_PlayModeState != EditorPlayModeState::Edit)
            ExitPlayMode();

        m_Scene = std::make_unique<Scene>();
        m_SelectedEntity = entt::null;
        m_SelectedTextureAssetKey.clear();
        m_CachedTextureAsset.reset();
        m_CurrentSceneAssetKey.clear();
    }

    void EditorLayer::SaveScene()
    {
        if (m_PlayModeState != EditorPlayModeState::Edit || !m_Scene)
            return;

        if (!m_CurrentSceneAssetKey.empty())
        {
            (void)SaveSceneToAssetKey(m_CurrentSceneAssetKey);
            return;
        }

        SaveSceneAs();
    }

    void EditorLayer::SaveSceneAs()
    {
        if (m_PlayModeState != EditorPlayModeState::Edit || !m_Scene)
            return;

        if (!m_CurrentSceneAssetKey.empty())
        {
            std::filesystem::path currentPath(m_CurrentSceneAssetKey);
            if (currentPath.has_filename())
            {
                const std::string fileName = SceneDisplayNameFromFileName(currentPath.filename().string());
                const size_t copyCount = std::min(fileName.size(), m_SaveSceneFileNameBuffer.size() - 1);
                std::copy_n(fileName.c_str(), copyCount, m_SaveSceneFileNameBuffer.begin());
                m_SaveSceneFileNameBuffer[copyCount] = '\0';
            }
            if (currentPath.has_parent_path())
                m_SaveSceneFolderPath = currentPath.parent_path().lexically_relative("Assets");
        }
        else
        {
            const std::string defaultName = SceneDisplayNameFromFileName(kDefaultSceneFileName);
            const size_t copyCount = std::min(defaultName.size(), m_SaveSceneFileNameBuffer.size() - 1);
            std::copy_n(defaultName.c_str(), copyCount, m_SaveSceneFileNameBuffer.begin());
            m_SaveSceneFileNameBuffer[copyCount] = '\0';
            m_SaveSceneFolderPath = "Scenes";
        }

        m_RequestOpenSaveScenePopup = true;
    }

    void EditorLayer::DrawSaveScenePopup()
    {
        if (m_RequestOpenSaveScenePopup)
        {
            ImGui::OpenPopup("Save Scene As");
            m_RequestOpenSaveScenePopup = false;
            m_SaveScenePopupOpen = true;
        }

        if (!m_SaveScenePopupOpen)
            return;

        const auto rootResult = Assets::FindProjectRootFromWorkingDirectory();
        if (rootResult.IsFailure())
        {
            LT_ERROR("Could not open Save Scene popup: {}", rootResult.GetError().GetErrorMessage());
            m_SaveScenePopupOpen = false;
            return;
        }

        const std::filesystem::path assetsDirectory = rootResult.GetValue() / "Assets";
        std::error_code errorCode;
        std::filesystem::create_directories(assetsDirectory / m_SaveSceneFolderPath, errorCode);

        if (!ImGui::BeginPopupModal("Save Scene As", &m_SaveScenePopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
            return;

        ImGui::Text("Choose folder and file name");
        ImGui::Separator();
        ImGui::Text("Folder:");
        ImGui::SameLine();
        ImGui::TextUnformatted(("Assets/" + m_SaveSceneFolderPath.generic_string()).c_str());

        ImGui::BeginChild("SceneFolderTree", ImVec2(420.0f, 220.0f), true);
        std::function<void(const std::filesystem::path&)> drawFolderTree;
        drawFolderTree = [&](const std::filesystem::path& relativePath) {
            const std::filesystem::path absolutePath = assetsDirectory / relativePath;
            std::vector<std::filesystem::path> subFolders;
            std::error_code iterateError;
            for (const auto& entry : std::filesystem::directory_iterator(absolutePath, iterateError))
            {
                if (iterateError)
                    continue;
                if (!entry.is_directory())
                    continue;
                const std::string folderName = entry.path().filename().string();
                if (!folderName.empty() && folderName[0] == '.')
                    continue;
                if (folderName == "Cache")
                    continue;
                subFolders.push_back(entry.path().filename());
            }

            std::sort(subFolders.begin(), subFolders.end(), [](const auto& left, const auto& right) {
                return left.string() < right.string();
            });

            const std::string label = relativePath.empty() ? "Assets" : relativePath.filename().string();
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
            if (subFolders.empty())
                flags |= ImGuiTreeNodeFlags_Leaf;
            if (relativePath == m_SaveSceneFolderPath)
                flags |= ImGuiTreeNodeFlags_Selected;

            const bool opened = ImGui::TreeNodeEx(label.c_str(), flags);
            if (ImGui::IsItemClicked())
                m_SaveSceneFolderPath = relativePath;

            if (opened)
            {
                for (const auto& childFolder : subFolders)
                    drawFolderTree(relativePath / childFolder);
                ImGui::TreePop();
            }
        };
        drawFolderTree("");
        ImGui::EndChild();

        ImGui::InputText("File Name", m_SaveSceneFileNameBuffer.data(), m_SaveSceneFileNameBuffer.size());

        bool closePopup = false;
        if (ImGui::Button("Save", ImVec2(130.0f, 0.0f)))
        {
            const std::string normalizedFileName = NormalizeSceneFileName(m_SaveSceneFileNameBuffer.data());
            const std::string assetKey = CreateSceneAssetInFolder(m_SaveSceneFolderPath, normalizedFileName);
            if (!assetKey.empty() && SaveSceneToAssetKey(assetKey))
            {
                m_CurrentSceneAssetKey = assetKey;
                closePopup = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(130.0f, 0.0f)))
        {
            closePopup = true;
        }

        if (closePopup)
        {
            m_SaveScenePopupOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }


    bool EditorLayer::LoadSceneFromAssetKey(const std::string& assetKey)
    {
        if (assetKey.empty())
            return false;

        if (m_PlayModeState != EditorPlayModeState::Edit)
            ExitPlayMode();

        const auto resolvedPathResult = Assets::ResolveAssetKeyToPath(assetKey);
        if (resolvedPathResult.IsFailure())
        {
            LT_ERROR("Failed to resolve scene asset key {}: {}", assetKey, resolvedPathResult.GetError().GetErrorMessage());
            return false;
        }

        auto sceneResult = Scene::LoadFromFile(resolvedPathResult.GetValue());
        if (sceneResult.IsFailure())
        {
            LT_ERROR("Failed to load scene {}: {}", assetKey, sceneResult.GetError().GetErrorMessage());
            return false;
        }

        m_Scene = std::move(sceneResult.GetValue());
        m_SelectedEntity = entt::null;
        m_SelectedTextureAssetKey.clear();
        m_CachedTextureAsset.reset();
        m_CurrentSceneAssetKey = assetKey;

        if (auto* editorCamera = m_CameraManager.GetPerspective3D(m_EditorCameraId))
        {
            const auto& bookmark = m_Scene->GetEditorCameraBookmark();
            if (bookmark.has_value())
            {
                editorCamera->SetPosition(bookmark->Position);
                editorCamera->SetYawPitchDegrees(bookmark->YawDegrees, bookmark->PitchDegrees);
            }
        }

        LT_INFO("Loaded scene {}", assetKey);
        return true;
    }

    bool EditorLayer::SaveSceneToAssetKey(const std::string& assetKey)
    {
        if (assetKey.empty() || !m_Scene)
            return false;

        if (auto* editorCamera = m_CameraManager.GetPerspective3D(m_EditorCameraId))
        {
            Scene::EditorCameraBookmark bookmark{};
            bookmark.Position = editorCamera->GetPosition();
            bookmark.YawDegrees = editorCamera->GetYawDegrees();
            bookmark.PitchDegrees = editorCamera->GetPitchDegrees();
            m_Scene->SetEditorCameraBookmark(bookmark);
        }

        const auto resolvedPathResult = Assets::ResolveAssetKeyToPath(assetKey);
        if (resolvedPathResult.IsFailure())
        {
            LT_ERROR("Failed to resolve scene asset key {}: {}", assetKey, resolvedPathResult.GetError().GetErrorMessage());
            return false;
        }

        const auto saveResult = m_Scene->SaveToFile(resolvedPathResult.GetValue());
        if (saveResult.IsFailure())
        {
            LT_ERROR("Failed to save scene {}: {}", assetKey, saveResult.GetError().GetErrorMessage());
            return false;
        }

        const auto importResult = Assets::AssetDatabase::GetInstance().ImportOrUpdate(assetKey, Assets::AssetType::Scene);
        if (importResult.IsFailure())
        {
            LT_WARN("Scene saved but failed to import into AssetDatabase ({}): {}", assetKey, importResult.GetError().GetErrorMessage());
        }

        LT_INFO("Saved scene {}", assetKey);
        return true;
    }

    std::string EditorLayer::CreateSceneAssetInFolder(const std::filesystem::path& relativeFolderPath, const std::string& preferredFileName)
    {
        const auto rootResult = Assets::FindProjectRootFromWorkingDirectory();
        if (rootResult.IsFailure())
        {
            LT_ERROR("Could not create scene asset: {}", rootResult.GetError().GetErrorMessage());
            return {};
        }

        const std::filesystem::path assetsDirectory = rootResult.GetValue() / "Assets";
        const std::filesystem::path targetDirectory = assetsDirectory / relativeFolderPath;
        std::error_code errorCode;
        std::filesystem::create_directories(targetDirectory, errorCode);
        if (errorCode)
        {
            LT_ERROR("Could not create scene folder {}: {}", targetDirectory.string(), errorCode.message());
            return {};
        }

        const std::string normalizedFileName = preferredFileName.empty()
            ? std::string(kDefaultSceneFileName)
            : NormalizeSceneFileName(preferredFileName.c_str());
        std::filesystem::path scenePath = targetDirectory / normalizedFileName;
        if (preferredFileName.empty() && std::filesystem::exists(scenePath, errorCode))
        {
            for (int32_t index = 1; index < 1024; ++index)
            {
                const std::filesystem::path candidate = targetDirectory / ("New Scene " + std::to_string(index) + ".scene.json");
                if (!std::filesystem::exists(candidate, errorCode))
                {
                    scenePath = candidate;
                    break;
                }
            }
        }

        if (!std::filesystem::exists(scenePath, errorCode))
        {
            Scene scene;
            const auto saveResult = scene.SaveToFile(scenePath);
            if (saveResult.IsFailure())
            {
                LT_ERROR("Could not create scene asset {}: {}", scenePath.string(), saveResult.GetError().GetErrorMessage());
                return {};
            }
        }

        std::filesystem::path relativeAssetPath = std::filesystem::relative(scenePath, assetsDirectory, errorCode);
        if (errorCode)
        {
            LT_ERROR("Could not compute scene asset key for {}", scenePath.string());
            return {};
        }

        const std::string assetKey = "Assets/" + relativeAssetPath.generic_string();
        const auto importResult = Assets::AssetDatabase::GetInstance().ImportOrUpdate(assetKey, Assets::AssetType::Scene);
        if (importResult.IsFailure())
        {
            LT_WARN("Created scene asset but failed to import into AssetDatabase ({}): {}", assetKey, importResult.GetError().GetErrorMessage());
        }

        LT_INFO("Created scene asset {}", assetKey);
        return assetKey;
    }

}  // namespace Limitless
