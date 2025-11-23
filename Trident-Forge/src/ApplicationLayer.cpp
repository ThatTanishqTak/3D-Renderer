#include "ApplicationLayer.h"

#include "Application/Startup.h"
#include "ECS/Components/CameraComponent.h"
#include "ECS/Components/MeshComponent.h"
#include "ECS/Components/TagComponent.h"
#include "ECS/Components/TransformComponent.h"
#include "Loader/AssimpExtensions.h"
#include "Loader/ModelLoader.h"
#include "Core/Utilities.h"
#include "Application/Input.h"
#include "Events/KeyCodes.h"
#include "Events/MouseCodes.h"

#include <spdlog/spdlog.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <filesystem>
#include <cmath>
#include <iterator>
#include <limits>
#include <system_error>

#include <glm/vec2.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

static inline float DegToRad(float deg) { return deg * 0.017453292519943295769f; }

void ApplicationLayer::Initialize()
{
    // Initialize editor panels before wiring cross-panel dependencies so their local state is ready.
    m_ConsolePanel.Initialize();
    m_AIDebugPanel.Initialize();
    m_AnimationGraphPanel.Initialize();

    // Wire up the gizmo state so the viewport and inspector remain in sync.
    m_ViewportPanel.SetGizmoState(&m_GizmoState);
    m_InspectorPanel.SetGizmoState(&m_GizmoState);

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

    RefreshRuntimeCameraBinding();
    // Future improvements may drive the runtime camera from gameplay systems, leaving this initialisation as a safe default.
    // Instantiate the active scene after the renderer is configured so registry hand-offs immediately reach the GPU.
    Trident::ECS::Registry& l_EditorRegistry = Trident::Startup::GetRegistry();
    m_ActiveScene = std::make_unique<Trident::Scene>(l_EditorRegistry);

    // Provide editor panels with the authoring registry. When play mode clones into a runtime registry these pointers stay put.
    Trident::ECS::Registry* l_RegistryForPanels = &m_ActiveScene->GetEditorRegistry();
    m_SceneHierarchyPanel.SetRegistry(l_RegistryForPanels);
    m_InspectorPanel.SetRegistry(l_RegistryForPanels);
    m_ViewportPanel.SetRegistry(l_RegistryForPanels);
    m_AIDebugPanel.SetRegistry(l_RegistryForPanels);
    m_AnimationGraphPanel.SetRegistry(l_RegistryForPanels);

    // Initialize Unity-like target state and pivot/distance
    m_TargetYawDegrees = m_EditorYawDegrees;
    m_TargetPitchDegrees = m_EditorPitchDegrees;

    m_CameraPivot = glm::vec3{ 0.0f, 0.0f, 0.0f };
    m_OrbitDistance = glm::length(m_TargetPosition - m_CameraPivot);
    if (!std::isfinite(m_OrbitDistance) || m_OrbitDistance <= 0.0f)
    {
        m_OrbitDistance = 8.0f;
    }
}

void ApplicationLayer::Shutdown()
{
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
    m_AIDebugPanel.SetSelectedEntity(l_SelectedEntity);
    m_AnimationGraphPanel.SetSelectedEntity(l_SelectedEntity);
    m_InspectorPanel.Update();
    m_AnimationGraphPanel.Update();
    m_ConsolePanel.Update();
    m_AIDebugPanel.Update();
}

void ApplicationLayer::Render()
{
    RenderMainMenuBar();
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
    {
        constexpr ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;
        ImGui::DockSpaceOverViewport((ImGuiID)ImGui::GetMainViewport(), 0);
    }

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
    m_AnimationGraphPanel.Render();
    m_ConsolePanel.Render();
    m_AIDebugPanel.Render();
}

void ApplicationLayer::RenderMainMenuBar()
{

}

void ApplicationLayer::RenderSceneToolbar()
{

}

void ApplicationLayer::HandleGlobalShortcuts()
{

}

void ApplicationLayer::HandleSceneFileDialogs()
{

}

void ApplicationLayer::RequestApplicationExit()
{

}

bool ApplicationLayer::SaveScene(const std::filesystem::path& path)
{
    return true;
}

bool ApplicationLayer::LoadScene(const std::filesystem::path& path)
{
    return true;
}

void ApplicationLayer::HandleSceneHierarchyContextMenu(const ImVec2& min, const ImVec2& max)
{

}

void ApplicationLayer::CreateEmptyEntity()
{

}

void ApplicationLayer::CreatePrimitiveEntity(PrimitiveType type)
{

}

std::string ApplicationLayer::MakeUniqueName(const std::string& baseName) const
{

}

void ApplicationLayer::OnEvent(Trident::Events& event)
{

}

bool ApplicationLayer::HandleFileDrop(Trident::FileDropEvent& event)
{

}

bool ApplicationLayer::ImportDroppedAssets(const std::vector<std::string>& droppedPaths)
{

}

void ApplicationLayer::RefreshRuntimeCameraBinding()
{

}

void ApplicationLayer::UpdateEditorCamera(float deltaTime)
{

}

void ApplicationLayer::FrameSelection()
{

}

glm::vec3 ApplicationLayer::ForwardFromYawPitch(float yawDegrees, float pitchDegrees)
{

}