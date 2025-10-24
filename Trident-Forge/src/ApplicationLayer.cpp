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

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <cmath>
#include <iterator>
#include <limits>
#include <optional>

#include <glm/vec2.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

static inline float DegToRad(float deg) { return deg * 0.017453292519943295769f; }

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
    // Mirror the same asset import callback into the runtime viewport so designers can drop levels or prefabs there as well.
    m_GameViewportPanel.SetAssetDropHandler([this](const std::vector<std::string>& droppedPaths)
        {
            ImportDroppedAssets(droppedPaths);
        });

    // Wire the hierarchy context menu into the layer so right-click creation routes through our helpers.
    m_SceneHierarchyPanel.SetContextMenuActions(
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
        });

    // Seed the editor camera with a comfortable default orbit so the scene appears immediately.
    m_EditorCamera.SetPosition({ 0.0f, 3.0f, 8.0f });
    m_EditorYawDegrees = 0.0f;
    m_EditorPitchDegrees = 0.0f;
    m_EditorCamera.SetRotation({ m_EditorPitchDegrees, m_EditorYawDegrees, 0.0f });
    m_EditorCamera.SetClipPlanes(0.1f, 1000.0f);
    m_EditorCamera.SetProjectionType(Trident::Camera::ProjectionType::Perspective);

    // Hand the configured camera to the renderer once the panels are bound so subsequent renders use it immediately.
    Trident::RenderCommand::SetEditorCamera(&m_EditorCamera);
    // Provide the runtime camera access to the registry so it can resolve ECS state without additional allocations.
    m_RuntimeCamera.SetRegistry(&Trident::Startup::GetRegistry());

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
    // Detach the editor camera before destruction to avoid dangling references inside the renderer singleton.
    Trident::RenderCommand::SetEditorCamera(nullptr);
    // Clear the runtime camera pointer to prevent dangling references when the layer shuts down.
    Trident::RenderCommand::SetRuntimeCamera(nullptr);
    m_HasRuntimeCamera = false;
}

void ApplicationLayer::Update()
{
    Trident::Input::Get().BeginFrame();

    // Update the editor camera first so viewport interactions read the freshest position/rotation values for this frame.
    UpdateEditorCamera(Trident::Utilities::Time::GetDeltaTime());

    m_ViewportPanel.Update();
    // Keep the runtime viewport state aligned with the editor viewport so shared handlers see up-to-date focus/hover data.
    m_GameViewportPanel.Update();
    m_ContentBrowserPanel.Update();
    m_SceneHierarchyPanel.Update();

    // Push the hierarchy selection into the inspector before it performs validation.
    const Trident::ECS::Entity l_SelectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();
    m_InspectorPanel.SetSelectedEntity(l_SelectedEntity);
    // Mirror the selection for the viewport so camera pivots follow the same entity l_Focus.
    m_ViewportPanel.SetSelectedEntity(l_SelectedEntity);
    m_InspectorPanel.Update();
}

void ApplicationLayer::Render()
{
    // Ensure the editor viewport always renders with the editor camera before handing off to runtime previews.
    Trident::RenderCommand::SetRuntimeCameraActive(false);
    m_ViewportPanel.Render();
    // Surface the runtime viewport directly after the scene so future play/pause widgets can live alongside it.
    RefreshRuntimeCameraBinding();
    m_GameViewportPanel.SetRuntimeCameraPresence(m_HasRuntimeCamera);
    m_GameViewportPanel.Render();
    // Default back to the editor camera so ancillary panels (gizmos, thumbnails) sample predictable state.
    Trident::RenderCommand::SetRuntimeCameraActive(false);
    m_ContentBrowserPanel.Render();
    m_SceneHierarchyPanel.Render();
    m_InspectorPanel.Render();
}

void ApplicationLayer::RefreshRuntimeCameraBinding()
{
    // The runtime viewport should favour a camera explicitly marked as primary and otherwise fall back to the first camera.
    Trident::ECS::Registry& l_Registry = Trident::Startup::GetRegistry();
    const std::vector<Trident::ECS::Entity>& l_Entities = l_Registry.GetEntities();

    std::optional<Trident::ECS::Entity> l_PrimaryCamera{};
    std::optional<Trident::ECS::Entity> l_FirstCamera{};

    for (Trident::ECS::Entity it_Entity : l_Entities)
    {
        if (!l_Registry.HasComponent<Trident::CameraComponent>(it_Entity))
        {
            continue;
        }

        if (!l_FirstCamera.has_value())
        {
            l_FirstCamera = it_Entity;
        }

        const Trident::CameraComponent& l_CameraComponent = l_Registry.GetComponent<Trident::CameraComponent>(it_Entity);
        if (l_CameraComponent.m_Primary)
        {
            // Prefer the first primary camera we encounter so authors can explicitly choose the gameplay view.
            l_PrimaryCamera = it_Entity;
            break;
        }
    }

    const std::optional<Trident::ECS::Entity> l_SelectedCamera = l_PrimaryCamera.has_value() ? l_PrimaryCamera : l_FirstCamera;
    if (!l_SelectedCamera.has_value())
    {
        // Without a camera in the scene the renderer must hand control back to the editor camera.
        Trident::RenderCommand::SetRuntimeCamera(nullptr);
        m_HasRuntimeCamera = false;
        // TODO: Revisit this once explicit scene play states can provide a temporary camera override.
        return;
    }

    m_RuntimeCamera.SetEntity(*l_SelectedCamera);
    Trident::RenderCommand::SetRuntimeCamera(&m_RuntimeCamera);
    m_HasRuntimeCamera = true;
    // TODO: Extend this binding to honour explicit user selection and pause/play states when the runtime gains controls.
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
    // Resolve the registry so the new entity integrates with the hierarchy and inspector immediately.
    Trident::ECS::Registry& l_Registry = Trident::Startup::GetRegistry();

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
    // Resolve the ECS registry so newly created entities immediately integrate with the renderer and inspector panels.
    Trident::ECS::Registry& l_Registry = Trident::Startup::GetRegistry();

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
    // Collect all existing tags so the uniqueness check runs in constant time when evaluating potential names.
    Trident::ECS::Registry& l_Registry = Trident::Startup::GetRegistry();
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
        std::transform(l_Extension.begin(), l_Extension.end(), l_Extension.begin(), [](unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
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
            // Default transform keeps the asset centred at the origin with unit l_Scale so artists can position it manually.
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

    if (l_Selected && Trident::Startup::GetRegistry().HasComponent<Trident::Transform>(l_Selected))
    {
        const auto& l_Transform = Trident::Startup::GetRegistry().GetComponent<Trident::Transform>(l_Selected);
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