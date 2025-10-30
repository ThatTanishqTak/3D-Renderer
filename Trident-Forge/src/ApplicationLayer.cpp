#include "ApplicationLayer.h"

#include "Application/Startup.h"
#include "ECS/Components/CameraComponent.h"
#include "ECS/Components/MeshComponent.h"
#include "ECS/Components/TagComponent.h"
#include "ECS/Components/TransformComponent.h"
#include "Loader/AssimpExtensions.h"
#include "Loader/ModelLoader.h"
#include "Renderer/RenderCommand.h"
#include "Core/Utilities.h"
#include "Application/Input.h"
#include "Events/KeyCodes.h"
#include "Events/MouseCodes.h"
#include "UI/FileDialog.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <cmath>
#include <iterator>
#include <limits>
#include <system_error>
#include <cstdio>

#include <glm/vec2.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

static inline float DegToRad(float deg) { return deg * 0.017453292519943295769f; }

void ApplicationLayer::Initialize()
{
    // Wire up the gizmo state so the viewport and inspector remain in sync.
    m_ViewportPanel.SetGizmoState(&m_GizmoState);
    m_InspectorPanel.SetGizmoState(&m_GizmoState);

    // Prime the export service with the current working directory so it can locate the runtime project.
    m_ExportService.SetProjectRoot(std::filesystem::current_path());

    // Ensure the console mirrors Unity-style defaults by surfacing info/warning/error output immediately.
    m_ConsolePanel.SetLevelVisibility(spdlog::level::trace, false);
    m_ConsolePanel.SetLevelVisibility(spdlog::level::debug, false);
    m_ConsolePanel.SetLevelVisibility(spdlog::level::info, true);
    m_ConsolePanel.SetLevelVisibility(spdlog::level::warn, true);
    m_ConsolePanel.SetLevelVisibility(spdlog::level::err, true);
    m_ConsolePanel.SetLevelVisibility(spdlog::level::critical, true);

    // Route drag-and-drop payloads originating inside the editor back into the shared import path.
    m_ViewportPanel.SetAssetDropHandler([this](const std::vector<std::string>& droppedPaths)
        {
            ImportDroppedAssets(droppedPaths);
        });
    // Mirror the same asset import callback into the runtime viewport so designers can drop levels or prefabs there as well.
    m_GameViewportPanel.SetAssetDropHandler([this](const std::vector<std::string>& droppedPaths)
        {
            ImportDroppedAssets(droppedPaths);
        });

    // Wire the hierarchy context menu into the layer so right-click creation routes through our helpers.
    m_SceneHierarchyPanel.SetContextMenuActions
    (
        [this]()
        {
            // Allow artists to spawn an empty entity directly from the hierarchy context menu.
            CreateEmptyEntity();
        },
        [this]()
        {
            // Spawn a cube primitive near the camera when requested from the context menu.
            CreatePrimitiveEntity(PrimitiveType::Cube);
        },
        [this]()
        {
            // Spawn a sphere primitive via the hierarchy context menu callback.
            CreatePrimitiveEntity(PrimitiveType::Sphere);
        },
        [this]()
        {
            // Spawn a quad primitive so flat geometry is quickly accessible.
            CreatePrimitiveEntity(PrimitiveType::Quad);
        }
    );

    // Seed the editor camera with a comfortable default orbit so the scene appears immediately.
    m_EditorCamera.SetPosition({ 0.0f, 3.0f, 8.0f });
    m_EditorYawDegrees = 0.0f;
    m_EditorPitchDegrees = 0.0f;
    m_EditorCamera.SetRotation({ m_EditorPitchDegrees, m_EditorYawDegrees, 0.0f });
    m_EditorCamera.SetClipPlanes(0.1f, 1000.0f);
    m_EditorCamera.SetProjectionType(Trident::Camera::ProjectionType::Perspective);

    // Hand the configured camera to the renderer once the panels are bound so subsequent renders use it immediately.
    Trident::RenderCommand::SetEditorCamera(&m_EditorCamera);
    // Mirror the configuration for the runtime camera so play mode can maintain its own independent transform state.
    m_RuntimeCamera.SetPosition(m_EditorCamera.GetPosition());
    m_RuntimeCamera.SetRotation(m_EditorCamera.GetRotation());
    m_RuntimeCamera.SetClipPlanes(0.1f, 1000.0f);
    m_RuntimeCamera.SetProjectionType(Trident::Camera::ProjectionType::Perspective);
    Trident::RenderCommand::SetRuntimeCamera(nullptr);
    Trident::RenderCommand::SetRuntimeCameraReady(false);
    RefreshRuntimeCameraBinding();
    // Future improvements may drive the runtime camera from gameplay systems, leaving this initialisation as a safe default.
    // Instantiate the active scene after the renderer is configured so registry hand-offs immediately reach the GPU.
    Trident::ECS::Registry& l_EditorRegistry = Trident::Startup::GetRegistry();
    m_ActiveScene = std::make_unique<Trident::Scene>(l_EditorRegistry);
    Trident::RenderCommand::SetActiveRegistry(&m_ActiveScene->GetEditorRegistry());

    // Provide editor panels with the authoring registry. When play mode clones into a runtime registry these pointers stay put.
    Trident::ECS::Registry* l_RegistryForPanels = &m_ActiveScene->GetEditorRegistry();
    m_SceneHierarchyPanel.SetRegistry(l_RegistryForPanels);
    m_InspectorPanel.SetRegistry(l_RegistryForPanels);
    m_ViewportPanel.SetRegistry(l_RegistryForPanels);
    m_AnimationGraphPanel.SetRegistry(l_RegistryForPanels);

    // Initialize Unity-like target state and pivot/distance
    m_TargetYawDegrees = m_EditorYawDegrees;
    m_TargetPitchDegrees = m_EditorPitchDegrees;
    m_TargetPosition = m_EditorCamera.GetPosition();

    m_CameraPivot = glm::vec3{ 0.0f, 0.0f, 0.0f };
    m_OrbitDistance = glm::length(m_TargetPosition - m_CameraPivot);
    if (!std::isfinite(m_OrbitDistance) || m_OrbitDistance <= 0.0f)
    {
        m_OrbitDistance = 8.0f;
    }
}

void ApplicationLayer::Shutdown()
{
    // Detach both cameras before destruction to avoid dangling references inside the renderer singleton.
    Trident::RenderCommand::SetEditorCamera(nullptr);
    Trident::RenderCommand::SetRuntimeCamera(nullptr);
    Trident::RenderCommand::SetRuntimeCameraReady(false);
    Trident::RenderCommand::SetActiveRegistry(nullptr);

    m_ActiveScene.reset();
}

void ApplicationLayer::Update()
{
    Trident::Input::Get().BeginFrame();

    // Update the editor camera first so viewport interactions read the freshest position/rotation values for this frame.
    UpdateEditorCamera(Trident::Utilities::Time::GetDeltaTime());

    // Process global shortcuts after the camera update so input state is ready for high-level actions like save/load.
    HandleGlobalShortcuts();

    m_ViewportPanel.Update();
    // Keep the runtime viewport state aligned with the editor viewport so shared handlers see up-to-date focus/hover data.
    RefreshRuntimeCameraBinding();

    if (m_ActiveScene != nullptr && m_ActiveScene->IsPlaying())
    {
        // Drive runtime scripts and other simulation features while the sandbox registry is active.
        m_ActiveScene->Update(Trident::Utilities::Time::GetDeltaTime());
    }

    m_GameViewportPanel.Update();
    m_ContentBrowserPanel.Update();
    m_SceneHierarchyPanel.Update();

    // Push the hierarchy selection into the inspector before it performs validation.
    const Trident::ECS::Entity l_SelectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();
    m_InspectorPanel.SetSelectedEntity(l_SelectedEntity);
    // Mirror the selection for the viewport so camera pivots follow the same entity l_Focus.
    m_ViewportPanel.SetSelectedEntity(l_SelectedEntity);
    //m_AnimationGraphPanel.SetSelectedEntity(l_SelectedEntity);
    m_InspectorPanel.Update();
    //m_AnimationGraphPanel.Update();
    m_ConsolePanel.Update();
}

void ApplicationLayer::Render()
{
    RenderMainMenuBar();
    RenderSceneToolbar();
    HandleSceneFileDialogs();

    // The editor viewport always renders with the editor camera so gizmos and transform tools remain deterministic.
    m_ViewportPanel.Render();
    // Surface the runtime viewport directly after the scene so future play/pause widgets can live alongside it.
    // The game viewport now presents the runtime camera feed, keeping simulation visuals separate from authoring tools.
    m_GameViewportPanel.Render();
    m_ContentBrowserPanel.Render();
    m_SceneHierarchyPanel.Render();
    m_InspectorPanel.Render();
    //m_AnimationGraphPanel.Render();
    m_ConsolePanel.Render();
}

void ApplicationLayer::RenderMainMenuBar()
{
    if (!ImGui::BeginMainMenuBar())
    {
        return;
    }

    const bool l_HasScene = m_ActiveScene != nullptr;

    if (ImGui::BeginMenu("File"))
    {
        // Persist the current scene when possible, otherwise fall back to Save As so the user can provide a path.
        if (ImGui::MenuItem("Save", "Ctrl+S", false, l_HasScene))
        {
            if (!m_CurrentScenePath.empty())
            {
                SaveScene(m_CurrentScenePath);
            }
            else
            {
                m_OpenSaveSceneAsPopup = true;
            }
        }

        if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S", false, l_HasScene))
        {
            m_OpenSaveSceneAsPopup = true;
        }

        if (ImGui::MenuItem("Open...", "Ctrl+O", false, l_HasScene))
        {
            m_OpenLoadScenePopup = true;
        }

        if (ImGui::MenuItem("Export...", nullptr, false, l_HasScene))
        {
            // Launch the export modal so the user can select a destination and build configuration.
            m_OpenExportPopup = true;
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Exit", "Ctrl+Q"))
        {
            RequestApplicationExit();
        }

        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

void ApplicationLayer::RenderSceneToolbar()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
    const ImGuiWindowFlags l_WindowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNavFocus;
    if (ImGui::Begin("Scene Controls", nullptr, l_WindowFlags))
    {
        const bool l_HasScene = m_ActiveScene != nullptr;
        const bool l_IsPlaying = l_HasScene && m_ActiveScene->IsPlaying();

        // Surface the last scene I/O status so artists receive immediate feedback near the transport controls.
        if (!m_SceneIoTooltip.empty())
        {
            const ImVec4 l_Color = m_LastSceneIoFailed ? ImVec4(0.85f, 0.35f, 0.35f, 1.0f) : ImVec4(0.35f, 0.85f, 0.45f, 1.0f);
            ImGui::TextColored(l_Color, m_LastSceneIoFailed ? "Scene IO Error" : "Scene IO");
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", m_SceneIoTooltip.c_str());
            }
        }

        ImGui::Separator();

        ImGui::BeginDisabled(!l_HasScene);
        if (l_HasScene)
        {
            if (l_IsPlaying)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.13f, 0.59f, 0.30f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.66f, 0.34f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.11f, 0.52f, 0.27f, 1.0f));
            }

            if (ImGui::Button("Play"))
            {
                if (!l_IsPlaying)
                {
                    // Promote the editor registry into a runtime clone so gameplay code can run against isolated data.
                    m_ActiveScene->Play();
                    Trident::RenderCommand::SetActiveRegistry(&m_ActiveScene->GetActiveRegistry());
                    RefreshRuntimeCameraBinding();
                }
            }

            if (l_IsPlaying)
            {
                ImGui::PopStyleColor(3);
            }
        }
        else
        {
            ImGui::Button("Play");
        }
        ImGui::EndDisabled();

        ImGui::SameLine();

        ImGui::BeginDisabled(true);
        if (ImGui::Button("Pause"))
        {
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip("Pause will activate once the runtime exposes time scaling. This toolbar is the hand-off point.");
        }
        ImGui::EndDisabled();

        ImGui::SameLine();

        ImGui::BeginDisabled(!l_HasScene || !l_IsPlaying);
        if (ImGui::Button("Stop"))
        {
            // Restore the editor registry and notify the renderer so authored data is visible again.
            m_ActiveScene->Stop();
            Trident::RenderCommand::SetActiveRegistry(&m_ActiveScene->GetEditorRegistry());
            RefreshRuntimeCameraBinding();
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        const char* l_StatusLabel = l_IsPlaying ? "Playing" : "Editing";
        ImGui::Text("Scene State: %s", l_StatusLabel);

        ImGui::Separator();

        // Mirror renderer performance capture controls so teams can trigger recordings from the editor toolbar.
        const bool l_IsCapturing = Trident::RenderCommand::IsPerformanceCaptureEnabled();
        const size_t l_CaptureSamples = Trident::RenderCommand::GetPerformanceCaptureSampleCount();
        const char* l_CaptureLabel = l_IsCapturing ? "Stop Performance Capture" : "Start Performance Capture";
        if (ImGui::Button(l_CaptureLabel))
        {
            // Toggling the flag automatically starts or exports the capture through the renderer's existing logic.
            Trident::RenderCommand::SetPerformanceCaptureEnabled(!l_IsCapturing);
        }

        ImGui::SameLine();
        ImGui::Text("Captured Samples: %zu", l_CaptureSamples);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Samples export automatically when capture stops. Future tooling can expand analytics here.");
        }

        if (l_IsCapturing)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.85f, 0.25f, 0.25f, 1.0f), "Recording...");
        }

        // Future improvements can add icons, hotkeys, or advanced transport controls here without touching other panels.
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void ApplicationLayer::HandleGlobalShortcuts()
{
    if (ImGui::GetCurrentContext() == nullptr)
    {
        return;
    }

    ImGuiIO& l_ImGuiIO = ImGui::GetIO();
    if (l_ImGuiIO.WantCaptureKeyboard)
    {
        // Respect ImGui's capture so typing into text fields does not accidentally trigger global actions.
        return;
    }

    Trident::Input& l_Input = Trident::Input::Get();
    const bool l_ControlDown = l_Input.IsKeyDown(Trident::Key::LeftControl) || l_Input.IsKeyDown(Trident::Key::RightControl);
    if (!l_ControlDown)
    {
        return;
    }

    const bool l_ShiftDown = l_Input.IsKeyDown(Trident::Key::LeftShift) || l_Input.IsKeyDown(Trident::Key::RightShift);
    const bool l_HasScene = m_ActiveScene != nullptr;

    if (l_HasScene && l_Input.IsKeyPressed(Trident::Key::S))
    {
        if (l_ShiftDown || m_CurrentScenePath.empty())
        {
            m_OpenSaveSceneAsPopup = true;
        }
        else
        {
            SaveScene(m_CurrentScenePath);
        }
    }

    if (l_HasScene && l_Input.IsKeyPressed(Trident::Key::O))
    {
        m_OpenLoadScenePopup = true;
    }

    if (l_Input.IsKeyPressed(Trident::Key::Q))
    {
        RequestApplicationExit();
    }
}

void ApplicationLayer::HandleSceneFileDialogs()
{
    if (m_OpenSaveSceneAsPopup)
    {
        ImGui::OpenPopup("Save Scene As");
        m_OpenSaveSceneAsPopup = false;
    }

    if (m_OpenLoadScenePopup)
    {
        ImGui::OpenPopup("Load Scene");
        m_OpenLoadScenePopup = false;
    }

    if (m_OpenExportPopup)
    {
        // Prefill the export directory with the last used location or the current scene folder for convenience.
        if (m_ExportDirectoryBuffer[0] == '\0')
        {
            const std::filesystem::path l_DefaultDirectory = !m_LastExportDirectory.empty()
                ? m_LastExportDirectory
                : (m_CurrentScenePath.empty() ? std::filesystem::current_path() : m_CurrentScenePath.parent_path());
            const std::string l_DefaultString = l_DefaultDirectory.string();
            std::snprintf(m_ExportDirectoryBuffer.data(), m_ExportDirectoryBuffer.size(), "%s", l_DefaultString.c_str());
        }

        m_LastExportStatus.clear();
        m_LastExportFailed = false;
        ImGui::OpenPopup("Export Scene");
        m_OpenExportPopup = false;
    }

    std::string l_SavePath = m_CurrentScenePath.empty() ? std::string{} : m_CurrentScenePath.string();
    if (Trident::UI::FileDialog::Save("Save Scene As", l_SavePath, ".trident"))
    {
        SaveScene(l_SavePath);
    }

    std::string l_LoadPath = m_CurrentScenePath.empty() ? std::string{} : m_CurrentScenePath.string();
    if (Trident::UI::FileDialog::Open("Load Scene", l_LoadPath, ".trident"))
    {
        LoadScene(l_LoadPath);
    }

    if (m_OpenExportDirectoryDialog)
    {
        ImGui::OpenPopup("Select Export Directory");
        m_OpenExportDirectoryDialog = false;
    }

    std::string l_ExportPath = std::string(m_ExportDirectoryBuffer.data());
    if (Trident::UI::FileDialog::SelectDirectory("Select Export Directory", l_ExportPath))
    {
        std::snprintf(m_ExportDirectoryBuffer.data(), m_ExportDirectoryBuffer.size(), "%s", l_ExportPath.c_str());
    }

    const bool l_HasScene = m_ActiveScene != nullptr;
    if (ImGui::BeginPopupModal("Export Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        const char* l_Configurations[] = { "Debug", "Release", "RelWithDebInfo" };
        const int l_ConfigurationCount = static_cast<int>(std::size(l_Configurations));
        if (m_SelectedExportConfiguration < 0 || m_SelectedExportConfiguration >= l_ConfigurationCount)
        {
            m_SelectedExportConfiguration = std::clamp(m_SelectedExportConfiguration, 0, l_ConfigurationCount - 1);
        }

        if (!l_HasScene)
        {
            ImGui::TextWrapped("No active scene is available. Load or create a scene before exporting.");
        }
        else
        {
            ImGui::TextWrapped("Export packages the active scene, captures the runtime camera transform, and builds the Trident launcher using MSVC.");
            ImGui::Separator();

            ImGui::Text("Destination Folder");
            ImGui::InputText("##ExportDirectory", m_ExportDirectoryBuffer.data(), m_ExportDirectoryBuffer.size());
            ImGui::SameLine();
            if (ImGui::Button("Browse..."))
            {
                // Delegate to the shared directory picker so authors can navigate comfortably.
                m_OpenExportDirectoryDialog = true;
            }

            ImGui::Combo("Build Configuration", &m_SelectedExportConfiguration, l_Configurations, l_ConfigurationCount);

            if (!m_LastExportStatus.empty())
            {
                const ImVec4 l_StatusColour = m_LastExportFailed ? ImVec4(0.85f, 0.35f, 0.35f, 1.0f) : ImVec4(0.35f, 0.85f, 0.45f, 1.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, l_StatusColour);
                ImGui::TextWrapped("%s", m_LastExportStatus.c_str());
                ImGui::PopStyleColor();
            }

            if (ImGui::Button("Export", ImVec2(140.0f, 0.0f)))
            {
                const std::string l_OutputDirectory = std::string(m_ExportDirectoryBuffer.data());
                if (l_OutputDirectory.empty())
                {
                    m_LastExportStatus = "Please choose an output directory.";
                    m_LastExportFailed = true;
                }
                else
                {
                    EditorExportService::ExportOptions l_Options{};
                    l_Options.m_OutputDirectory = l_OutputDirectory;
                    l_Options.m_BuildConfiguration = l_Configurations[m_SelectedExportConfiguration];

                    const EditorExportService::ExportResult l_ExportResult = m_ExportService.ExportScene(*m_ActiveScene, m_RuntimeCamera, m_CurrentScenePath, l_Options);
                    m_LastExportStatus = l_ExportResult.m_Message.empty() ? "Export finished." : l_ExportResult.m_Message;
                    m_LastExportFailed = !l_ExportResult.m_Succeeded;

                    if (l_ExportResult.m_Succeeded)
                    {
                        m_LastExportDirectory = l_Options.m_OutputDirectory;
                    }
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("Close", ImVec2(140.0f, 0.0f)))
            {
                ImGui::CloseCurrentPopup();
            }

            ImGui::Separator();
            ImGui::TextWrapped("Future improvements: support platform-specific exports and cache build outputs between runs to speed iteration.");
        }

        if (!l_HasScene && ImGui::Button("Close"))
        {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void ApplicationLayer::RequestApplicationExit()
{
    if (!Trident::Startup::HasInstance())
    {
        return;
    }
}

bool ApplicationLayer::SaveScene(const std::filesystem::path& path)
{
    if (m_ActiveScene == nullptr)
    {
        // The scene bridge should always exist while the editor is running, but guard against unexpected lifetimes.
        TR_CORE_ERROR("Cannot save scene because no active scene is available.");
        m_SceneIoTooltip = "Saving failed because there is no active scene.";
        m_LastSceneIoFailed = true;
        return false;
    }

    if (path.empty())
    {
        TR_CORE_ERROR("Cannot save scene because the provided path is empty.");
        m_SceneIoTooltip = "Saving failed because the selected path was empty.";
        m_LastSceneIoFailed = true;
        return false;
    }

    std::filesystem::path l_TargetPath = path;
    if (!l_TargetPath.has_extension() || l_TargetPath.extension() != ".trident")
    {
        // Normalise the extension to the project-standard format so future loads resolve cleanly.
        l_TargetPath.replace_extension(".trident");
    }

    if (!l_TargetPath.has_filename())
    {
        TR_CORE_ERROR("Cannot save scene because the path '{}' is missing a filename.", l_TargetPath.string());
        m_SceneIoTooltip = "Saving failed because no filename was provided.";
        m_LastSceneIoFailed = true;
        return false;
    }

    const std::filesystem::path l_TargetDirectory = l_TargetPath.parent_path();
    if (!l_TargetDirectory.empty())
    {
        std::error_code l_CreateError{};
        std::filesystem::create_directories(l_TargetDirectory, l_CreateError);
        if (l_CreateError)
        {
            TR_CORE_ERROR("Failed to create scene directory '{}': {}", l_TargetDirectory.string(), l_CreateError.message());
            m_SceneIoTooltip = "Saving failed because the destination directory could not be created.";
            m_LastSceneIoFailed = true;
            return false;
        }
    }

    // Keep the scene metadata aligned with the filename so hierarchy panels display friendly titles.
    m_ActiveScene->SetName(l_TargetPath.stem().string());
    m_ActiveScene->Save(l_TargetPath.string());

    std::error_code l_ExistsError{};
    const bool l_FileExists = std::filesystem::exists(l_TargetPath, l_ExistsError);
    if (!l_FileExists || l_ExistsError)
    {
        if (l_ExistsError)
        {
            TR_CORE_ERROR("Failed to verify saved scene '{}': {}", l_TargetPath.string(), l_ExistsError.message());
        }
        else
        {
            TR_CORE_ERROR("Scene file '{}' was not created during save.", l_TargetPath.string());
        }

        m_SceneIoTooltip = "Saving failed. Check the log for details.";
        m_LastSceneIoFailed = true;
        return false;
    }

    m_CurrentScenePath = l_TargetPath;
    m_SceneIoTooltip = "Scene saved to " + l_TargetPath.string();
    m_LastSceneIoFailed = false;

    // Rebind the current registry and cameras so renderer state stays consistent with the new save point.
    if (m_ActiveScene->IsPlaying())
    {
        Trident::RenderCommand::SetActiveRegistry(&m_ActiveScene->GetActiveRegistry());
    }
    else
    {
        Trident::RenderCommand::SetActiveRegistry(&m_ActiveScene->GetEditorRegistry());
    }
    RefreshRuntimeCameraBinding();

    return true;
}

bool ApplicationLayer::LoadScene(const std::filesystem::path& path)
{
    if (m_ActiveScene == nullptr)
    {
        TR_CORE_ERROR("Cannot load scene because no active scene is available.");
        m_SceneIoTooltip = "Loading failed because there is no active scene.";
        m_LastSceneIoFailed = true;
        return false;
    }

    if (path.empty())
    {
        TR_CORE_ERROR("Cannot load scene because the provided path is empty.");
        m_SceneIoTooltip = "Loading failed because the selected path was empty.";
        m_LastSceneIoFailed = true;
        return false;
    }

    std::filesystem::path l_SourcePath = path;
    if (!l_SourcePath.has_extension() || l_SourcePath.extension() != ".trident")
    {
        l_SourcePath.replace_extension(".trident");
    }

    std::error_code l_ExistsError{};
    const bool l_FileExists = std::filesystem::exists(l_SourcePath, l_ExistsError);
    if (!l_FileExists || l_ExistsError)
    {
        if (l_ExistsError)
        {
            TR_CORE_ERROR("Failed to validate scene '{}' before loading: {}", l_SourcePath.string(), l_ExistsError.message());
        }
        else
        {
            TR_CORE_ERROR("Scene file '{}' does not exist.", l_SourcePath.string());
        }

        m_SceneIoTooltip = "Loading failed because the file could not be found.";
        m_LastSceneIoFailed = true;
        return false;
    }

    if (m_ActiveScene->IsPlaying())
    {
        // Stop runtime playback so the load operation can safely rebuild the editor registry.
        m_ActiveScene->Stop();
    }

    const bool l_Loaded = m_ActiveScene->Load(l_SourcePath.string());
    if (!l_Loaded)
    {
        m_SceneIoTooltip = "Loading failed. Check the log for details.";
        m_LastSceneIoFailed = true;
        return false;
    }

    m_ActiveScene->SetName(l_SourcePath.stem().string());
    m_CurrentScenePath = l_SourcePath;
    m_SceneIoTooltip = "Scene loaded from " + l_SourcePath.string();
    m_LastSceneIoFailed = false;

    Trident::RenderCommand::SetActiveRegistry(&m_ActiveScene->GetEditorRegistry());
    RefreshRuntimeCameraBinding();

    return true;
}

void ApplicationLayer::HandleSceneHierarchyContextMenu(const ImVec2& min, const ImVec2& max)
{
    // Pull the shared input manager so context menu activation respects the editor's capture rules.
    Trident::Input& l_Input = Trident::Input::Get();
    if (!l_Input.HasMousePosition())
    {
        // Without a valid cursor position there is no reliable way to perform hit-testing on the hierarchy window.
        return;
    }

    const glm::vec2 l_MousePosition = l_Input.GetMousePosition();
    const bool l_MouseInsideHierarchy = (l_MousePosition.x >= min.x) && (l_MousePosition.x <= max.x) &&
        (l_MousePosition.y >= min.y) && (l_MousePosition.y <= max.y);
    const bool l_WindowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    // When the hierarchy owns focus and receives a right-click, surface the contextual options popup.
    if (l_WindowFocused && l_MouseInsideHierarchy && l_Input.WasMouseButtonPressed(Trident::Mouse::ButtonRight))
    {
        ImGui::OpenPopup("SceneHierarchyContextMenu");
    }

    // Author the popup entries that allow quick creation of common entity types directly from the hierarchy.
    if (ImGui::BeginPopup("SceneHierarchyContextMenu"))
    {
        if (ImGui::MenuItem("Create Empty Entity"))
        {
            CreateEmptyEntity();
        }

        if (ImGui::BeginMenu("Create Primitive"))
        {
            if (ImGui::MenuItem("Cube"))
            {
                CreatePrimitiveEntity(PrimitiveType::Cube);
            }
            if (ImGui::MenuItem("Sphere"))
            {
                CreatePrimitiveEntity(PrimitiveType::Sphere);
            }
            if (ImGui::MenuItem("Quad"))
            {
                CreatePrimitiveEntity(PrimitiveType::Quad);
            }
            ImGui::EndMenu();
        }

        ImGui::EndPopup();
    }
}

void ApplicationLayer::CreateEmptyEntity()
{
    if (m_ActiveScene == nullptr)
    {
        // Scene construction happens during Initialize(); bail out defensively if a future refactor reorders calls.
        return;
    }

    // Resolve the editor registry so the new entity integrates with the hierarchy and inspector immediately.
    Trident::ECS::Registry& l_Registry = m_ActiveScene->GetEditorRegistry();

    Trident::ECS::Entity l_NewEntity = l_Registry.CreateEntity();

    // Authoring defaults keep the entity centred at the origin with identity rotation and unit scale.
    Trident::Transform l_DefaultTransform{};
    l_DefaultTransform.Position = glm::vec3{ 0.0f, 0.0f, 0.0f };
    l_DefaultTransform.Rotation = glm::vec3{ 0.0f, 0.0f, 0.0f };
    l_DefaultTransform.Scale = glm::vec3{ 1.0f, 1.0f, 1.0f };
    l_Registry.AddComponent<Trident::Transform>(l_NewEntity, l_DefaultTransform);

    // Assign a readable label so the hierarchy stays organised even when multiple empties are created.
    Trident::TagComponent& l_TagComponent = l_Registry.AddComponent<Trident::TagComponent>(l_NewEntity);
    l_TagComponent.m_Tag = MakeUniqueName("Empty Entity");

    // TODO: Expose a selection setter on the hierarchy so new entities can auto-focus after creation.
}

void ApplicationLayer::CreatePrimitiveEntity(PrimitiveType type)
{
    if (m_ActiveScene == nullptr)
    {
        return;
    }

    // Resolve the ECS registry so newly created entities immediately integrate with the renderer and inspector panels.
    Trident::ECS::Registry& l_Registry = m_ActiveScene->GetEditorRegistry();

    // Spawn primitives a short distance in front of the camera so they appear within the artist's view frustum.
    const glm::vec3 l_SpawnPosition = m_EditorCamera.GetPosition() + (m_EditorCamera.GetForwardDirection() * 10.0f);
    Trident::ECS::Entity l_NewEntity = l_Registry.CreateEntity();

    // Initialise the transform so authoring begins with predictable orientation and scale.
    Trident::Transform l_Transform{};
    l_Transform.Position = l_SpawnPosition;
    l_Transform.Rotation = glm::vec3{ 0.0f };
    l_Transform.Scale = glm::vec3{ 1.0f };
    l_Registry.AddComponent<Trident::Transform>(l_NewEntity, l_Transform);

    // Attach a mesh component so the renderer recognises the entity as drawable geometry.
    Trident::MeshComponent& l_MeshComponent = l_Registry.AddComponent<Trident::MeshComponent>(l_NewEntity);
    l_MeshComponent.m_Visible = true;
    // TODO: Wire specific mesh indices once the renderer exposes procedural primitives for these shapes.
    switch (type)
    {
    case PrimitiveType::Cube:
        l_MeshComponent.m_Primitive = Trident::MeshComponent::PrimitiveType::Cube;
        break;
    case PrimitiveType::Sphere:
        l_MeshComponent.m_Primitive = Trident::MeshComponent::PrimitiveType::Sphere;
        break;
    case PrimitiveType::Quad:
        l_MeshComponent.m_Primitive = Trident::MeshComponent::PrimitiveType::Quad;
        break;
    default:
        l_MeshComponent.m_Primitive = Trident::MeshComponent::PrimitiveType::None;
        break;
    }
    // TODO: Promote primitives into a dedicated authoring path once automatic mesh assignment is available.

    // Assign a tag that reads clearly in the hierarchy, ensuring duplicates receive numbered suffixes.
    std::string l_BaseTag = "Primitive";
    switch (type)
    {
    case PrimitiveType::Cube: l_BaseTag = "Cube"; break;
    case PrimitiveType::Sphere: l_BaseTag = "Sphere"; break;
    case PrimitiveType::Quad: l_BaseTag = "Quad"; break;
    default: break;
    }

    Trident::TagComponent& l_TagComponent = l_Registry.AddComponent<Trident::TagComponent>(l_NewEntity);
    l_TagComponent.m_Tag = MakeUniqueName(l_BaseTag);
    // TODO: Consider attaching a lightweight PrimitiveTag marker component so batch operations can filter these entities.
}

std::string ApplicationLayer::MakeUniqueName(const std::string& baseName) const
{
    if (m_ActiveScene == nullptr)
    {
        return baseName.empty() ? std::string("Primitive") : baseName;
    }

    // Collect all existing tags so the uniqueness check runs in constant time when evaluating potential names.
    Trident::ECS::Registry& l_Registry = m_ActiveScene->GetEditorRegistry();
    std::unordered_set<std::string> l_ExistingTags{};
    const std::vector<Trident::ECS::Entity>& l_Entities = l_Registry.GetEntities();
    l_ExistingTags.reserve(l_Entities.size());

    for (Trident::ECS::Entity it_Entity : l_Entities)
    {
        if (!l_Registry.HasComponent<Trident::TagComponent>(it_Entity))
        {
            continue;
        }

        const Trident::TagComponent& l_TagComponent = l_Registry.GetComponent<Trident::TagComponent>(it_Entity);
        l_ExistingTags.insert(l_TagComponent.m_Tag);
    }

    const std::string l_RootName = baseName.empty() ? std::string("Primitive") : baseName;
    if (!l_ExistingTags.contains(l_RootName))
    {
        return l_RootName;
    }

    int l_Suffix = 2;
    while (true)
    {
        std::string l_Candidate = l_RootName + " (" + std::to_string(l_Suffix) + ")";
        if (!l_ExistingTags.contains(l_Candidate))
        {
            return l_Candidate;
        }

        ++l_Suffix;
    }
}

void ApplicationLayer::OnEvent(Trident::Events& event)
{
    Trident::EventDispatcher l_Dispatcher(event);
    l_Dispatcher.Dispatch<Trident::FileDropEvent>([this](Trident::FileDropEvent& dropEvent)
        {
            return HandleFileDrop(dropEvent);
        });
}

bool ApplicationLayer::HandleFileDrop(Trident::FileDropEvent& event)
{
    // File drops arrive via the engine event queue, so rely on the shared input manager's
    // cached cursor state instead of querying ImGui directly.
    Trident::Input& l_Input = Trident::Input::Get();
    if (!l_Input.HasMousePosition())
    {
        return false;
    }

    const glm::vec2 l_MousePosition = l_Input.GetMousePosition();
    if (!std::isfinite(l_MousePosition.x) || !std::isfinite(l_MousePosition.y))
    {
        return false;
    }

    const ImVec2 l_MouseImGui{ l_MousePosition.x, l_MousePosition.y };
    const bool l_IsWithinViewport = m_ViewportPanel.ContainsPoint(l_MouseImGui);

    if (!m_ViewportPanel.IsHovered() && !l_IsWithinViewport)
    {
        // Ignore drops that land outside the viewport so accidental drags do not spawn entities.
        return false;
    }

    return ImportDroppedAssets(event.GetPaths());
}

bool ApplicationLayer::ImportDroppedAssets(const std::vector<std::string>& droppedPaths)
{
    if (droppedPaths.empty())
    {
        return false;
    }

    const std::vector<std::string>& l_SupportedExtensions = Trident::Loader::AssimpExtensions::GetNormalizedExtensions();
    if (m_ActiveScene == nullptr)
    {
        return false;
    }

    Trident::ECS::Registry& l_Registry = m_ActiveScene->GetEditorRegistry();

    // Cache the current mesh count so new entities can reference the appended geometry correctly.
    const size_t l_InitialMeshCount = Trident::RenderCommand::GetModelCount();

    std::vector<Trident::Geometry::Mesh> l_ImportedMeshes{};
    std::vector<Trident::Geometry::Material> l_ImportedMaterials{};
    bool l_ImportedAny = false;
    std::vector<std::string> l_ImportedTextures{};

    const auto DecomposeMatrixToTransform = [](const glm::mat4& modelMatrix) -> Trident::Transform
        {
            Trident::Transform l_Result{};
            glm::vec3 l_Scale{ 1.0f };
            glm::quat l_Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
            glm::vec3 l_Translation{ 0.0f };
            glm::vec3 l_Skew{ 0.0f };
            glm::vec4 l_Perspective{ 0.0f };

            if (glm::decompose(modelMatrix, l_Scale, l_Rotation, l_Translation, l_Skew, l_Perspective))
            {
                l_Rotation = glm::normalize(l_Rotation);
                l_Result.Position = l_Translation;
                l_Result.Scale = l_Scale;
                l_Result.Rotation = glm::degrees(glm::eulerAngles(l_Rotation));
            }
            else
            {
                // Retain the default transform so failed decompositions do not inject garbage into the scene graph.
                TR_CORE_WARN("Failed to decompose transform matrix while importing dropped asset");
            }

            return l_Result;
        };

    for (const std::string& it_RawPath : droppedPaths)
    {
        std::string l_NormalizedPath = Trident::Utilities::FileManagement::NormalizePath(it_RawPath);
        std::filesystem::path l_PathView{ l_NormalizedPath };
        std::string l_Extension = l_PathView.extension().string();
        std::transform(l_Extension.begin(), l_Extension.end(), l_Extension.begin(), [](unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            });

        const bool l_Supported = std::find(l_SupportedExtensions.begin(), l_SupportedExtensions.end(), l_Extension) != l_SupportedExtensions.end();
        if (!l_Supported)
        {
            continue;
        }

        Trident::Loader::ModelData l_ModelData = Trident::Loader::ModelLoader::Load(l_NormalizedPath);
        if (l_ModelData.m_Meshes.empty() && l_ModelData.m_MeshInstances.empty())
        {
            continue;
        }

        // Persist the imported skeleton and clip data so the animation system can resolve them in future updates.
        Trident::Animation::Skeleton& l_StoredSkeleton = m_ImportedSkeletonAssets[l_NormalizedPath];
        l_StoredSkeleton = l_ModelData.m_Skeleton;
        std::vector<Trident::Animation::AnimationClip>& l_StoredClips = m_ImportedAnimationLibraries[l_NormalizedPath];
        l_StoredClips = l_ModelData.m_AnimationClips;

        const int l_TextureOffset = static_cast<int>(l_ImportedTextures.size());
        l_ImportedTextures.reserve(l_ImportedTextures.size() + l_ModelData.m_Textures.size());
        for (std::string& it_TexturePath : l_ModelData.m_Textures)
        {
            l_ImportedTextures.emplace_back(std::move(it_TexturePath));
        }

        const size_t l_MaterialOffset = l_ImportedMaterials.size();
        for (Trident::Geometry::Material& l_Material : l_ModelData.m_Materials)
        {
            if (l_Material.BaseColorTextureIndex >= 0)
            {
                l_Material.BaseColorTextureIndex += l_TextureOffset;
            }
            if (l_Material.MetallicRoughnessTextureIndex >= 0)
            {
                l_Material.MetallicRoughnessTextureIndex += l_TextureOffset;
            }
            if (l_Material.NormalTextureIndex >= 0)
            {
                l_Material.NormalTextureIndex += l_TextureOffset;
            }
        }

        const std::string l_BaseName = l_PathView.stem().string();
        const std::string l_TagRoot = l_BaseName.empty() ? std::string("Imported Mesh") : l_BaseName;

        const size_t l_MeshOffset = l_ImportedMeshes.size();
        std::vector<bool> l_MeshHasSkin(l_ModelData.m_Meshes.size(), false);
        for (size_t it_MeshIndex = 0; it_MeshIndex < l_ModelData.m_Meshes.size(); ++it_MeshIndex)
        {
            Trident::Geometry::Mesh& l_Mesh = l_ModelData.m_Meshes[it_MeshIndex];

            if (l_Mesh.MaterialIndex >= 0)
            {
                l_Mesh.MaterialIndex += static_cast<int>(l_MaterialOffset);
            }

            const bool l_HasBoneWeights = std::any_of(l_Mesh.Vertices.begin(), l_Mesh.Vertices.end(), [](const Vertex& vertex)
                {
                    return (vertex.m_BoneWeights.x > 0.0f) || (vertex.m_BoneWeights.y > 0.0f) || (vertex.m_BoneWeights.z > 0.0f) || (vertex.m_BoneWeights.w > 0.0f);
                });
            l_MeshHasSkin[it_MeshIndex] = l_HasBoneWeights;

            // Preserve the mesh data so the renderer can rebuild GPU buffers after all drops are processed.
            l_ImportedMeshes.emplace_back(std::move(l_Mesh));
        }

        const size_t l_GlobalMeshOffset = l_InitialMeshCount + l_MeshOffset;
        const size_t l_TotalInstanceCount = !l_ModelData.m_MeshInstances.empty() ? l_ModelData.m_MeshInstances.size() : l_ModelData.m_Meshes.size();
        size_t l_InstanceCounter = 0;

        const auto SpawnMeshEntity = [&](size_t localMeshIndex, const glm::mat4& modelMatrix, const std::string& nodeName)
            {
                if (localMeshIndex >= l_MeshHasSkin.size())
                {
                    // Ignore invalid indices so corrupt assets do not crash the editor.
                    TR_CORE_WARN("Mesh instance references invalid mesh index {} while importing {}", localMeshIndex, l_NormalizedPath);
                    return;
                }

                const size_t l_GlobalMeshIndex = l_GlobalMeshOffset + localMeshIndex;
                if (l_GlobalMeshIndex >= l_InitialMeshCount + l_ImportedMeshes.size())
                {
                    TR_CORE_WARN("Mesh instance resolved to out-of-range global index {} while importing {}", l_GlobalMeshIndex, l_NormalizedPath);
                    return;
                }

                Trident::ECS::Entity l_NewEntity = l_Registry.CreateEntity();

                // Decompose the baked transform so ECS systems can manipulate TRS values directly.
                Trident::Transform l_Decomposed = DecomposeMatrixToTransform(modelMatrix);
                l_Registry.AddComponent<Trident::Transform>(l_NewEntity, l_Decomposed);

                Trident::MeshComponent& l_MeshComponent = l_Registry.AddComponent<Trident::MeshComponent>(l_NewEntity);
                l_MeshComponent.m_MeshIndex = l_GlobalMeshIndex;
                l_MeshComponent.m_Visible = true;
                // Persist the asset provenance so saved scenes can reconstruct renderer geometry after reloads.
                l_MeshComponent.m_SourceAssetPath = l_NormalizedPath;
                l_MeshComponent.m_SourceMeshIndex = localMeshIndex;

                Trident::TagComponent& l_TagComponent = l_Registry.AddComponent<Trident::TagComponent>(l_NewEntity);
                std::string l_TagValue = l_TagRoot;
                if (!nodeName.empty())
                {
                    l_TagValue += " - " + nodeName;
                }
                if (l_TotalInstanceCount > 1)
                {
                    l_TagValue += " (" + std::to_string(l_InstanceCounter + 1) + ")";
                }
                l_TagComponent.m_Tag = l_TagValue;

                if (l_MeshHasSkin[localMeshIndex])
                {
                    // Create an animation component so skinned meshes can bind to the imported skeleton on the next update tick.
                    Trident::AnimationComponent& l_AnimationComponent = l_Registry.AddComponent<Trident::AnimationComponent>(l_NewEntity);
                    l_AnimationComponent.m_SkeletonAssetId = l_NormalizedPath;
                    l_AnimationComponent.m_AnimationAssetId = l_NormalizedPath;
                    if (!l_StoredClips.empty())
                    {
                        l_AnimationComponent.m_CurrentClip = l_StoredClips.front().m_Name;
                    }
                    l_AnimationComponent.m_BoneMatrices.assign(l_StoredSkeleton.m_Bones.size(), glm::mat4(1.0f));
                    l_AnimationComponent.m_IsPlaying = true;
                    l_AnimationComponent.m_IsLooping = true;
                    // Force cached handles to refresh so the runtime resolves the new skeleton and clip set immediately.
                    l_AnimationComponent.InvalidateCachedAssets();
                }

                ++l_InstanceCounter;
                l_ImportedAny = true;
            };

        if (!l_ModelData.m_MeshInstances.empty())
        {
            for (const Trident::Loader::MeshInstance& l_Instance : l_ModelData.m_MeshInstances)
            {
                SpawnMeshEntity(l_Instance.m_MeshIndex, l_Instance.m_ModelMatrix, l_Instance.m_NodeName);
            }
        }
        else
        {
            // Fall back to identity transforms so legacy assets without instance data continue to spawn correctly.
            for (size_t it_MeshIndex = 0; it_MeshIndex < l_ModelData.m_Meshes.size(); ++it_MeshIndex)
            {
                SpawnMeshEntity(it_MeshIndex, glm::mat4(1.0f), {});
            }
        }

        // Transfer materials after entities so the renderer can align indices when rebuilding draw buffers.
        std::move(l_ModelData.m_Materials.begin(), l_ModelData.m_Materials.end(), std::back_inserter(l_ImportedMaterials));
    }

    if (!l_ImportedAny || l_ImportedMeshes.empty())
    {
        return false;
    }

    // Ask the renderer to append the new meshes so existing GPU resources stay valid and the ECS draw metadata stays synced.
    Trident::RenderCommand::AppendMeshes(std::move(l_ImportedMeshes), std::move(l_ImportedMaterials), std::move(l_ImportedTextures));

    return true;
}

void ApplicationLayer::RefreshRuntimeCameraBinding()
{
    // Locate the first available gameplay camera. Prefer entities explicitly flagged as primary, but fall back to
    // the first camera encountered so empty scenes still show content once a camera is authored.
    const Trident::ECS::Entity l_InvalidEntity = std::numeric_limits<Trident::ECS::Entity>::max();

    Trident::ECS::Registry* l_RegistryPtr = nullptr;
    if (m_ActiveScene != nullptr)
    {
        if (m_ActiveScene->IsPlaying())
        {
            // While playing we must inspect the runtime registry so gameplay state drives the viewport.
            l_RegistryPtr = &m_ActiveScene->GetActiveRegistry();
        }
        else
        {
            // When idle we always consult the editor registry to avoid stale runtime pointers after Stop().
            l_RegistryPtr = &m_ActiveScene->GetEditorRegistry();
        }
    }
    else if (Trident::Startup::HasInstance())
    {
        // Fallback used during bootstrapping before a scene is created.
        l_RegistryPtr = &Trident::Startup::GetRegistry();
    }

    static Trident::ECS::Registry* s_PreviousRegistry = nullptr;
    if (l_RegistryPtr != s_PreviousRegistry)
    {
        // A registry swap occurs whenever play mode toggles or the active scene is destroyed.
        // Reset the cached entity so the next scan cannot dereference a destroyed registry pointer.
        m_BoundRuntimeCameraEntity = l_InvalidEntity;
        s_PreviousRegistry = l_RegistryPtr;
    }

    if (l_RegistryPtr == nullptr)
    {
        Trident::RenderCommand::SetRuntimeCamera(nullptr);
        Trident::RenderCommand::SetRuntimeCameraReady(false);

        return;
    }

    Trident::ECS::Registry& l_Registry = *l_RegistryPtr;
    const std::vector<Trident::ECS::Entity>& l_Entities = l_Registry.GetEntities();

    Trident::ECS::Entity l_SelectedEntity = l_InvalidEntity;

    for (Trident::ECS::Entity it_Entity : l_Entities)
    {
        if (!l_Registry.HasComponent<Trident::CameraComponent>(it_Entity))
        {
            continue;
        }

        if (!l_Registry.HasComponent<Trident::Transform>(it_Entity))
        {
            // Cameras without transforms cannot drive the runtime view yet. Future systems may infer transforms automatically.
            continue;
        }

        Trident::CameraComponent& l_CameraComponent = l_Registry.GetComponent<Trident::CameraComponent>(it_Entity);
        if (l_CameraComponent.m_Primary)
        {
            l_SelectedEntity = it_Entity;
            break;
        }

        if (l_SelectedEntity == l_InvalidEntity)
        {
            l_SelectedEntity = it_Entity;
        }
    }

    if (l_SelectedEntity != l_InvalidEntity)
    {
        Trident::CameraComponent& l_CameraComponent = l_Registry.GetComponent<Trident::CameraComponent>(l_SelectedEntity);
        Trident::Transform& l_TransformComponent = l_Registry.GetComponent<Trident::Transform>(l_SelectedEntity);

        // Cache the selection so repeated scans can detect changes. This also keeps room for future multi-camera routing.
        m_BoundRuntimeCameraEntity = l_SelectedEntity;

        // Push transform state into the runtime camera so gameplay visuals mirror the authored entity.
        m_RuntimeCamera.SetPosition(l_TransformComponent.Position);
        m_RuntimeCamera.SetRotation(l_TransformComponent.Rotation);

        // Apply projection settings stored on the ECS component.
        m_RuntimeCamera.SetProjectionType(l_CameraComponent.m_ProjectionType);
        m_RuntimeCamera.SetFieldOfView(l_CameraComponent.m_FieldOfView);
        m_RuntimeCamera.SetOrthographicSize(l_CameraComponent.m_OrthographicSize);
        m_RuntimeCamera.SetClipPlanes(l_CameraComponent.m_NearClip, l_CameraComponent.m_FarClip);

        if (l_CameraComponent.m_FixedAspectRatio && l_CameraComponent.m_AspectRatio > std::numeric_limits<float>::epsilon())
        {
            // Respect fixed aspect ratios by adjusting the runtime viewport width while retaining the current height.
            glm::vec2 l_ViewportSize = m_RuntimeCamera.GetViewportSize();
            if (l_ViewportSize.y <= std::numeric_limits<float>::epsilon())
            {
                l_ViewportSize.y = 1.0f;
            }
            l_ViewportSize.x = l_ViewportSize.y * l_CameraComponent.m_AspectRatio;
            m_RuntimeCamera.SetViewportSize(l_ViewportSize);
        }

        m_RuntimeCamera.Invalidate();

        // Hand the configured runtime camera to the renderer and flag it as ready for consumption by the viewport panel.
        // Future upgrades may promote this to support multiple simultaneous runtime cameras streamed to different views.
        Trident::RenderCommand::SetRuntimeCamera(&m_RuntimeCamera);
        Trident::RenderCommand::SetRuntimeCameraReady(true);
        Trident::RenderCommand::SetViewportCamera(l_SelectedEntity);
    }
    else
    {
        // Without a gameplay camera we clear the binding so the renderer can fall back to editor visuals gracefully.
        m_BoundRuntimeCameraEntity = l_InvalidEntity;
        Trident::RenderCommand::SetRuntimeCamera(nullptr);
        Trident::RenderCommand::SetRuntimeCameraReady(false);
        Trident::RenderCommand::SetViewportCamera(l_InvalidEntity);
    }
}

void ApplicationLayer::UpdateEditorCamera(float deltaTime)
{
    // Keep Trident's input system synchronised with ImGui so mouse/keyboard queries honour UI captures while still
    // permitting viewport interaction whenever the scene window is hovered or focused.
    Trident::Input& l_Input = Trident::Input::Get();
    ImGuiIO& l_ImGuiIO = ImGui::GetIO();
    const bool l_ViewportHovered = m_ViewportPanel.IsHovered();
    const bool l_ViewportFocused = m_ViewportPanel.IsFocused();
    const bool l_BlockMouse = l_ImGuiIO.WantCaptureMouse && !l_ViewportHovered;
    const bool l_BlockKeyboard = l_ImGuiIO.WantCaptureKeyboard && !l_ViewportFocused;
    l_Input.SetUICapture(l_BlockMouse, l_BlockKeyboard);

    // Abort navigation when the viewport is not the active recipient of input, but continue to interpolate toward the
    // latest target to keep smoothing responsive after the mouse leaves the window.
    const bool l_CanProcessMouse = l_ViewportHovered && !l_BlockMouse;
    const bool l_CanProcessKeyboard = l_ViewportFocused && !l_BlockKeyboard;

    if (l_CanProcessKeyboard && l_Input.IsKeyPressed(Trident::Key::F))
    {
        // Provide a Unity-like focus shortcut so artists can frame the current selection quickly.
        FrameSelection();
    }

    glm::vec2 l_MouseDelta = l_CanProcessMouse ? l_Input.GetMouseDelta() : glm::vec2{ 0.0f, 0.0f };
    const glm::vec2 l_ScrollDelta = l_CanProcessMouse ? l_Input.GetScrollDelta() : glm::vec2{ 0.0f, 0.0f };

    const bool l_IsAltDown = l_Input.IsKeyDown(Trident::Key::LeftAlt) || l_Input.IsKeyDown(Trident::Key::RightAlt);
    const bool l_IsShiftDown = l_Input.IsKeyDown(Trident::Key::LeftShift) || l_Input.IsKeyDown(Trident::Key::RightShift);
    const bool l_IsLeftMouseDown = l_Input.IsMouseButtonDown(Trident::Mouse::ButtonLeft);
    const bool l_IsRightMouseDown = l_Input.IsMouseButtonDown(Trident::Mouse::ButtonRight);
    const bool l_IsMiddleMouseDown = l_Input.IsMouseButtonDown(Trident::Mouse::ButtonMiddle);

    // Determine which navigation mode should run this frame. Alt + LMB orbits, MMB pans, Alt + RMB dollies, and RMB
    // without Alt enables fly navigation with WASD style controls.
    const bool l_ShouldOrbit = l_CanProcessMouse && l_IsAltDown && l_IsLeftMouseDown;
    const bool l_ShouldPan = l_CanProcessMouse && l_IsMiddleMouseDown && !l_ShouldOrbit;
    const bool l_ShouldDolly = l_CanProcessMouse && l_IsAltDown && l_IsRightMouseDown;
    const bool l_IsFlyMode = l_CanProcessMouse && l_IsRightMouseDown && !l_IsAltDown;
    const bool l_ShouldFlyRotate = l_IsFlyMode;
    const bool l_IsRotating = l_ShouldOrbit || l_ShouldFlyRotate;

    // Reset the first frame of a drag so accumulated deltas do not cause a jump when buttons are pressed mid-frame.
    if (l_IsRotating)
    {
        if (m_ResetRotateOrbitReference)
        {
            l_MouseDelta = glm::vec2{ 0.0f, 0.0f };
            m_ResetRotateOrbitReference = false;
        }
    }
    else
    {
        m_ResetRotateOrbitReference = true;
    }
    m_IsRotateOrbitActive = l_ShouldOrbit;

    // Apply pitch/yaw adjustments using mouse movement for orbit and fly modes, clamping the pitch to avoid flipping.
    if (l_IsRotating)
    {
        m_TargetYawDegrees += l_MouseDelta.x * m_MouseRotationSpeed;
        m_TargetPitchDegrees -= l_MouseDelta.y * m_MouseRotationSpeed;
        m_TargetPitchDegrees = std::clamp(m_TargetPitchDegrees, -89.0f, 89.0f);
    }

    // Derive the camera basis vectors from the updated target orientation so translation modes move relative to view.
    const glm::quat l_TargetOrientation = glm::quat(glm::radians(glm::vec3{ m_TargetPitchDegrees, m_TargetYawDegrees, 0.0f }));
    glm::vec3 l_Forward = l_TargetOrientation * glm::vec3{ 0.0f, 0.0f, -1.0f };
    glm::vec3 l_Right = l_TargetOrientation * glm::vec3{ 1.0f, 0.0f, 0.0f };
    glm::vec3 l_Up = l_TargetOrientation * glm::vec3{ 0.0f, 1.0f, 0.0f };

    if (glm::length2(l_Forward) <= std::numeric_limits<float>::epsilon())
    {
        l_Forward = glm::vec3{ 0.0f, 0.0f, -1.0f };
    }
    if (glm::length2(l_Right) <= std::numeric_limits<float>::epsilon())
    {
        l_Right = glm::vec3{ 1.0f, 0.0f, 0.0f };
    }
    if (glm::length2(l_Up) <= std::numeric_limits<float>::epsilon())
    {
        l_Up = glm::vec3{ 0.0f, 1.0f, 0.0f };
    }

    l_Forward = glm::normalize(l_Forward);
    l_Right = glm::normalize(l_Right);
    l_Up = glm::normalize(l_Up);

    if (l_ShouldOrbit)
    {
        // Maintain orbit distance around the stored pivot whenever Alt + LMB drags occur.
        m_TargetPosition = m_CameraPivot - l_Forward * m_OrbitDistance;
    }

    if (l_ShouldPan)
    {
        // Translate both the camera and pivot laterally so orbiting continues around the same relative point.
        const float l_Distance = std::max(m_OrbitDistance, m_MinOrbitDistance);
        const float l_PanSpeed = l_Distance * m_PanSpeedFactor * 0.0015f;
        const glm::vec3 l_PanOffset = (-l_MouseDelta.x * l_Right + l_MouseDelta.y * l_Up) * l_PanSpeed;
        m_TargetPosition += l_PanOffset;
        m_CameraPivot += l_PanOffset;
    }

    if (l_ShouldDolly)
    {
        // Alt + RMB dolly adjusts the orbit radius, clamping to avoid inverting around the pivot.
        m_OrbitDistance += -l_MouseDelta.y * m_DollySpeedFactor;
        m_OrbitDistance = std::max(m_OrbitDistance, m_MinOrbitDistance);
        m_TargetPosition = m_CameraPivot - l_Forward * m_OrbitDistance;
    }

    if (l_CanProcessMouse && l_ScrollDelta.y != 0.0f)
    {
        // Scroll wheel zooms along the forward axis for quick framing adjustments.
        m_OrbitDistance -= l_ScrollDelta.y * m_MouseZoomSpeed;
        m_OrbitDistance = std::max(m_OrbitDistance, m_MinOrbitDistance);
        m_TargetPosition = m_CameraPivot - l_Forward * m_OrbitDistance;
    }

    if (l_IsFlyMode && l_CanProcessKeyboard)
    {
        // RMB + WASD style fly camera that respects boost and vertical translation.
        glm::vec3 l_MoveDirection{ 0.0f, 0.0f, 0.0f };
        if (l_Input.IsKeyDown(Trident::Key::W))
        {
            l_MoveDirection += l_Forward;
        }
        if (l_Input.IsKeyDown(Trident::Key::S))
        {
            l_MoveDirection -= l_Forward;
        }
        if (l_Input.IsKeyDown(Trident::Key::D))
        {
            l_MoveDirection += l_Right;
        }
        if (l_Input.IsKeyDown(Trident::Key::A))
        {
            l_MoveDirection -= l_Right;
        }
        if (l_Input.IsKeyDown(Trident::Key::E) || l_Input.IsKeyDown(Trident::Key::Space))
        {
            l_MoveDirection += l_Up;
        }
        if (l_Input.IsKeyDown(Trident::Key::Q) || l_Input.IsKeyDown(Trident::Key::LeftControl))
        {
            l_MoveDirection -= l_Up;
        }

        if (glm::length2(l_MoveDirection) > std::numeric_limits<float>::epsilon())
        {
            l_MoveDirection = glm::normalize(l_MoveDirection);
            float l_MoveSpeed = m_CameraMoveSpeed;
            if (l_IsShiftDown)
            {
                l_MoveSpeed *= m_CameraBoostMultiplier;
            }

            m_TargetPosition += l_MoveDirection * l_MoveSpeed * deltaTime;
            m_CameraPivot = m_TargetPosition + l_Forward * m_OrbitDistance;
        }
    }

    // Re-evaluate orbit distance after all translations so scroll/orbit remain in sync with the new position.
    m_OrbitDistance = std::max(glm::length(m_CameraPivot - m_TargetPosition), m_MinOrbitDistance);

    // Smoothly interpolate the actual camera toward the desired state to avoid abrupt jumps when switching modes.
    const glm::vec3 l_CurrentPosition = m_EditorCamera.GetPosition();
    const float l_PosAlpha = (m_PosSmoothing <= 0.0f) ? 1.0f : std::clamp(1.0f - std::exp(-m_PosSmoothing * deltaTime), 0.0f, 1.0f);
    const glm::vec3 l_NewPosition = l_CurrentPosition + (m_TargetPosition - l_CurrentPosition) * l_PosAlpha;
    m_EditorCamera.SetPosition(l_NewPosition);

    const glm::vec3 l_CurrentRotation = m_EditorCamera.GetRotation();
    const float l_RotAlpha = (m_RotSmoothing <= 0.0f) ? 1.0f : std::clamp(1.0f - std::exp(-m_RotSmoothing * deltaTime), 0.0f, 1.0f);
    const float l_NewPitch = std::lerp(l_CurrentRotation.x, m_TargetPitchDegrees, l_RotAlpha);
    const float l_NewYaw = std::lerp(l_CurrentRotation.y, m_TargetYawDegrees, l_RotAlpha);
    m_EditorCamera.SetRotation({ l_NewPitch, l_NewYaw, 0.0f });
}

void ApplicationLayer::FrameSelection()
{
    Trident::ECS::Entity l_Selected = m_SceneHierarchyPanel.GetSelectedEntity();
    glm::vec3 l_Focus{ 0.0f };
    float l_Radius = 1.0f;

    Trident::ECS::Registry* l_RegistryPtr = nullptr;
    if (m_ActiveScene != nullptr)
    {
        l_RegistryPtr = &m_ActiveScene->GetEditorRegistry();
    }
    else if (Trident::Startup::HasInstance())
    {
        l_RegistryPtr = &Trident::Startup::GetRegistry();
    }

    if (l_Selected && l_RegistryPtr != nullptr && l_RegistryPtr->HasComponent<Trident::Transform>(l_Selected))
    {
        const auto& l_Transform = l_RegistryPtr->GetComponent<Trident::Transform>(l_Selected);
        l_Focus = l_Transform.Position;
        // If you have bounds, set l_Radius from them for smarter framing.
    }

    m_CameraPivot = l_Focus;

    // Choose distance based on a simple heuristic and clamp to a sensible range.
    m_OrbitDistance = std::clamp(l_Radius * 3.0f, 2.0f, 50.0f);

    // Aim camera at pivot using target state so smoothing handles the rest.
    // Aim camera at pivot using target state so smoothing handles the rest.
    glm::vec3 l_ToPivot = m_CameraPivot - m_EditorCamera.GetPosition();
    if (glm::length2(l_ToPivot) > std::numeric_limits<float>::epsilon())
    {
        l_ToPivot = glm::normalize(l_ToPivot);

        // Convert the desired forward vector back into yaw/pitch that respect the -Z forward frame.
        const float l_Pitch = glm::degrees(std::asin(glm::clamp(-l_ToPivot.y, -1.0f, 1.0f)));
        const float l_Yaw = glm::degrees(std::atan2(l_ToPivot.x, -l_ToPivot.z));

        m_TargetYawDegrees = l_Yaw;
        m_TargetPitchDegrees = std::clamp(l_Pitch, -89.0f, 89.0f);
    }
    const glm::vec3 l_Forward = ForwardFromYawPitch(m_TargetYawDegrees, m_TargetPitchDegrees);
    m_TargetPosition = m_CameraPivot - l_Forward * m_OrbitDistance;
}

glm::vec3 ApplicationLayer::ForwardFromYawPitch(float yawDegrees, float pitchDegrees)
{
    // Build the quaternion directly from Euler angles so we match EditorCamera's -Z forward reference frame.
    const glm::quat l_Orientation = glm::quat(glm::radians(glm::vec3{ pitchDegrees, yawDegrees, 0.0f }));
    const glm::vec3 l_Forward = l_Orientation * glm::vec3{ 0.0f, 0.0f, -1.0f };

    if (!std::isfinite(l_Forward.x) || !std::isfinite(l_Forward.y) || !std::isfinite(l_Forward.z))
    {
        return glm::vec3{ 0.0f, 0.0f, -1.0f };
    }

    return glm::normalize(l_Forward);
}