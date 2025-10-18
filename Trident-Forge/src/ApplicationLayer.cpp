#include "ApplicationLayer.h"

#include "Application/Startup.h"
#include "ECS/Components/MeshComponent.h"
#include "ECS/Components/TagComponent.h"
#include "ECS/Components/TransformComponent.h"
#include "Loader/AssimpExtensions.h"
#include "Loader/ModelLoader.h"
#include "Renderer/RenderCommand.h"
#include "Core/Utilities.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <cmath>
#include <iterator>
#include <limits>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

void ApplicationLayer::Initialize()
{
    // Wire up the gizmo state so the viewport and inspector remain in sync.
    m_ViewportPanel.SetGizmoState(&m_GizmoState);
    m_InspectorPanel.SetGizmoState(&m_GizmoState);

    // Route drag-and-drop payloads originating inside the editor back into the shared import path.
    m_ViewportPanel.SetAssetDropHandler([this](const std::vector<std::string>& droppedPaths)
        {
            ImportDroppedAssets(droppedPaths);
        });

    // Seed the editor camera with a comfortable default orbit so the scene appears immediately.
    m_EditorCamera.SetPosition({ 0.0f, 3.0f, 8.0f });
    m_EditorYawDegrees = -90.0f;
    m_EditorPitchDegrees = -20.0f;
    m_EditorCamera.SetRotation({ m_EditorPitchDegrees, m_EditorYawDegrees, 0.0f });
    m_EditorCamera.SetClipPlanes(0.1f, 1000.0f);
    m_EditorCamera.SetProjectionType(Trident::Camera::ProjectionType::Perspective);

    // Hand the configured camera to the renderer once the panels are bound so subsequent renders use it immediately.
    Trident::RenderCommand::SetEditorCamera(&m_EditorCamera);
}

void ApplicationLayer::Shutdown()
{
    // Detach the editor camera before destruction to avoid dangling references inside the renderer singleton.
    Trident::RenderCommand::SetEditorCamera(nullptr);
}

void ApplicationLayer::Update()
{
    // Update the editor camera first so viewport interactions read the freshest position/rotation values for this frame.
    UpdateEditorCamera(Trident::Utilities::Time::GetDeltaTime());

    m_ViewportPanel.Update();
    m_ContentBrowserPanel.Update();
    m_SceneHierarchyPanel.Update();

    // Push the hierarchy selection into the inspector before it performs validation.
    const Trident::ECS::Entity l_SelectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();
    m_InspectorPanel.SetSelectedEntity(l_SelectedEntity);
    // Mirror the selection for the viewport so camera pivots follow the same entity focus.
    m_ViewportPanel.SetSelectedEntity(l_SelectedEntity);
    m_InspectorPanel.Update();
}

void ApplicationLayer::Render()
{
    m_ViewportPanel.Render();
    m_ContentBrowserPanel.Render();
    m_SceneHierarchyPanel.Render();
    m_InspectorPanel.Render();
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
    ImGuiIO& l_IO = ImGui::GetIO();
    const bool l_HasMousePosition = std::isfinite(l_IO.MousePos.x) && std::isfinite(l_IO.MousePos.y);
    const bool l_IsWithinViewport = l_HasMousePosition && m_ViewportPanel.ContainsPoint(l_IO.MousePos);

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
    Trident::ECS::Registry& l_Registry = Trident::Startup::GetRegistry();

    // Cache the current mesh count so new entities can reference the appended geometry correctly.
    const size_t l_InitialMeshCount = Trident::RenderCommand::GetModelCount();

    std::vector<Trident::Geometry::Mesh> l_ImportedMeshes{};
    std::vector<Trident::Geometry::Material> l_ImportedMaterials{};
    bool l_ImportedAny = false;

    for (const std::string& it_Path : droppedPaths)
    {
        std::filesystem::path l_PathView{ it_Path };
        std::string l_Extension = l_PathView.extension().string();
        std::transform(l_Extension.begin(), l_Extension.end(), l_Extension.begin(), [](unsigned char a_Char)
            {
                return static_cast<char>(std::tolower(a_Char));
            });

        const bool l_Supported = std::find(l_SupportedExtensions.begin(), l_SupportedExtensions.end(), l_Extension) != l_SupportedExtensions.end();
        if (!l_Supported)
        {
            continue;
        }

        Trident::Loader::ModelData l_ModelData = Trident::Loader::ModelLoader::Load(it_Path);
        if (l_ModelData.Meshes.empty())
        {
            continue;
        }

        const std::string l_BaseName = l_PathView.stem().string();
        const std::string l_TagRoot = l_BaseName.empty() ? std::string("Imported Mesh") : l_BaseName;

        for (size_t it_MeshIndex = 0; it_MeshIndex < l_ModelData.Meshes.size(); ++it_MeshIndex)
        {
            Trident::Geometry::Mesh& l_Mesh = l_ModelData.Meshes[it_MeshIndex];

            // Preserve the mesh data so the renderer can rebuild GPU buffers after all drops are processed.
            const size_t l_PreviousMeshCount = l_ImportedMeshes.size();
            l_ImportedMeshes.emplace_back(std::move(l_Mesh));
            const size_t l_AssignedMeshIndex = l_InitialMeshCount + l_PreviousMeshCount;

            Trident::ECS::Entity l_NewEntity = l_Registry.CreateEntity();
            // Default transform keeps the asset centred at the origin with unit scale so artists can position it manually.
            l_Registry.AddComponent<Trident::Transform>(l_NewEntity, Trident::Transform{});

            Trident::MeshComponent& l_MeshComponent = l_Registry.AddComponent<Trident::MeshComponent>(l_NewEntity);
            l_MeshComponent.m_MeshIndex = l_AssignedMeshIndex;
            l_MeshComponent.m_Visible = true;

            Trident::TagComponent& l_TagComponent = l_Registry.AddComponent<Trident::TagComponent>(l_NewEntity);
            l_TagComponent.m_Tag = l_TagRoot;
            if (l_ModelData.Meshes.size() > 1)
            {
                l_TagComponent.m_Tag += " (" + std::to_string(it_MeshIndex + 1) + ")";
            }

            l_ImportedAny = true;
        }

        // Transfer materials after entities so the renderer can align indices when rebuilding draw buffers.
        std::move(l_ModelData.Materials.begin(), l_ModelData.Materials.end(), std::back_inserter(l_ImportedMaterials));
    }

    if (!l_ImportedAny || l_ImportedMeshes.empty())
    {
        return false;
    }

    // Ask the renderer to append the new meshes so existing GPU resources stay valid and the ECS draw metadata stays synced.
    Trident::RenderCommand::AppendMeshes(std::move(l_ImportedMeshes), std::move(l_ImportedMaterials));

    return true;
}

void ApplicationLayer::UpdateEditorCamera(float a_DeltaTime)
{
    // Gather the latest ImGui IO snapshot so we can determine whether the editor should consume controls this frame.
    ImGuiIO& l_IO = ImGui::GetIO();

    // If ImGui intends to capture mouse or keyboard input we avoid touching the camera to respect focused widgets.
    if (l_IO.WantCaptureMouse || l_IO.WantCaptureKeyboard)
    {
        m_IsRotateOrbitActive = false;
        m_ResetRotateOrbitReference = true;
        return;
    }

    // Restrict camera updates to the viewport so other panels remain scrollable and do not steal focus.
    const bool l_HasViewportFocus = m_ViewportPanel.IsFocused() && m_ViewportPanel.IsHovered();
    if (!l_HasViewportFocus)
    {
        m_IsRotateOrbitActive = false;
        m_ResetRotateOrbitReference = true;

        return;
    }

    // Store the cursor location so the next drag can start without a large delta jump when the button is pressed.
    if (m_ResetRotateOrbitReference)
    {
        m_LastCursorPosition = { l_IO.MousePos.x, l_IO.MousePos.y };
        m_ResetRotateOrbitReference = false;
    }

    const bool l_IsRotating = l_IO.MouseDown[ImGuiMouseButton_Right];
    if (l_IsRotating)
    {
        // Mark the drag active so key handling below knows to treat WASD/QE as fly controls.
        m_IsRotateOrbitActive = true;

        // Convert the mouse delta into yaw/pitch adjustments to orbit around the focus point.
        const float l_YawDelta = l_IO.MouseDelta.x * m_MouseRotationSpeed;
        const float l_PitchDelta = l_IO.MouseDelta.y * m_MouseRotationSpeed;
        m_EditorYawDegrees += l_YawDelta;
        m_EditorPitchDegrees = std::clamp(m_EditorPitchDegrees - l_PitchDelta, -89.0f, 89.0f);

        m_EditorCamera.SetRotation({ m_EditorPitchDegrees, m_EditorYawDegrees, 0.0f });
        m_LastCursorPosition = { l_IO.MousePos.x, l_IO.MousePos.y };
    }
    else if (m_IsRotateOrbitActive)
    {
        // Once the button releases we clear the drag state so the next press seeds a fresh reference position.
        m_IsRotateOrbitActive = false;
        m_ResetRotateOrbitReference = true;
    }

    // Fetch forward/right vectors after any rotation updates so translation moves relative to the new heading.
    const glm::vec3 l_Forward = m_EditorCamera.GetForwardDirection();
    const glm::vec3 l_WorldUp{ 0.0f, 1.0f, 0.0f };
    glm::vec3 l_Right = glm::normalize(glm::cross(l_Forward, l_WorldUp));
    if (!std::isfinite(l_Right.x) || !std::isfinite(l_Right.y) || !std::isfinite(l_Right.z))
    {
        // Guard against degeneracy when looking straight up/down by falling back to a canonical horizontal axis.
        l_Right = glm::vec3{ 1.0f, 0.0f, 0.0f };
    }

    glm::vec3 l_Translation{ 0.0f };

    // Determine the frame's delta time from the engine clock so motion stays frame-rate independent.
    const float l_FrameDelta = Trident::Utilities::Time::GetDeltaTime();
    (void)a_DeltaTime; // The explicit parameter remains for future callers that provide custom deltas.

    // Allow faster motion when shift is held so large scenes are easier to traverse.
    const float l_SpeedMultiplier = l_IO.KeyShift ? m_CameraBoostMultiplier : 1.0f;
    const float l_MoveStep = m_CameraMoveSpeed * l_SpeedMultiplier * l_FrameDelta;

    // WASD drive forward/backward and strafing relative to the camera heading.
    if (l_IsRotating && ImGui::IsKeyDown(ImGuiKey_W))
    {
        l_Translation += l_Forward * l_MoveStep;
    }
    if (l_IsRotating && ImGui::IsKeyDown(ImGuiKey_S))
    {
        l_Translation -= l_Forward * l_MoveStep;
    }
    if (l_IsRotating && ImGui::IsKeyDown(ImGuiKey_D))
    {
        l_Translation += l_Right * l_MoveStep;
    }
    if (l_IsRotating && ImGui::IsKeyDown(ImGuiKey_A))
    {
        l_Translation -= l_Right * l_MoveStep;
    }

    // QE provide vertical movement for fly navigation when orbiting with the mouse.
    if (l_IsRotating && ImGui::IsKeyDown(ImGuiKey_E))
    {
        l_Translation += l_WorldUp * l_MoveStep;
    }
    if (l_IsRotating && ImGui::IsKeyDown(ImGuiKey_Q))
    {
        l_Translation -= l_WorldUp * l_MoveStep;
    }

    // Mouse wheel dolly adjusts the camera distance even without a key press to speed up framing.
    if (std::abs(l_IO.MouseWheel) > std::numeric_limits<float>::epsilon())
    {
        l_Translation += l_Forward * (l_IO.MouseWheel * m_MouseZoomSpeed);
    }

    if (glm::length(l_Translation) > std::numeric_limits<float>::epsilon())
    {
        glm::vec3 l_Position = m_EditorCamera.GetPosition();
        l_Position += l_Translation;
        m_EditorCamera.SetPosition(l_Position);
    }

    // TODO: Integrate ViewportPanel::FrameSelection via a dedicated focus key to snap the camera to selections with smoothing.
}