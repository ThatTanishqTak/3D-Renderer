#include "ApplicationLayer.h"

#include "Application/Startup.h"
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
#include "Events/MouseEvents.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <cmath>
#include <iterator>
#include <limits>

#include <glm/gtx/norm.hpp>
#include <glm/geometric.hpp>

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

    // Bridge viewport rendering back to the application layer so the contextual menu can react to image interactions.
    m_ViewportPanel.SetContextMenuHandler([this](const ImVec2& min, const ImVec2& max)
        {
            HandleViewportContextMenu(min, max);
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
    // Mirror the selection for the viewport so camera pivots follow the same entity l_Focus.
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

void ApplicationLayer::HandleViewportContextMenu(const ImVec2& min, const ImVec2& max)
{
    // The viewport image is hosted within an ImGui window, so we verify the cursor lies inside the draw rectangle before
    // responding to mouse releases. This keeps the context menu from appearing while resizing docks or dragging overlays.
    const bool l_IsHovered = ImGui::IsMouseHoveringRect(min, max, true);
    const bool l_IsMouseReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Right);

    // Block popups when the editor is actively dragging a widget or resizing splitters to avoid double consumption of inputs.
    const bool l_IsDragging = ImGui::IsMouseDragging(ImGuiMouseButton_Left) ||
        ImGui::IsMouseDragging(ImGuiMouseButton_Right) ||
        ImGui::IsMouseDragging(ImGuiMouseButton_Middle);
    const bool l_IsManipulatingItem = ImGui::IsAnyItemActive();

    if (l_IsHovered && l_IsMouseReleased && !l_IsDragging && !l_IsManipulatingItem)
    {
        ImGui::OpenPopup("ViewportContextMenu");
    }

    if (ImGui::BeginPopup("ViewportContextMenu"))
    {
        // Provide a dedicated submenu so the primitive list scales cleanly as future shapes are added.
        if (ImGui::BeginMenu("Add Primitive"))
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

        // TODO: Extend the context menu with lighting helpers, cameras, and custom authoring tools.
        ImGui::EndPopup();
    }
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

    l_Dispatcher.Dispatch<Trident::MouseMovedEvent>([this](Trident::MouseMovedEvent& mouseEvent)
        {
            const glm::vec2 l_NewPosition{ mouseEvent.GetX(), mouseEvent.GetY() };

            if (!m_HasCursorPosition)
            {
                // Seed the cursor state the first time we receive input so subsequent deltas are relative to a real position.
                m_CurrentCursorPosition = l_NewPosition;
                m_HasCursorPosition = true;
                m_PendingCursorDelta = glm::vec2{ 0.0f, 0.0f };
                return false;
            }

            const glm::vec2 l_Delta = l_NewPosition - m_CurrentCursorPosition;
            m_PendingCursorDelta += l_Delta;
            m_CurrentCursorPosition = l_NewPosition;

            return false;
        });

    l_Dispatcher.Dispatch<Trident::MouseScrolledEvent>([this](Trident::MouseScrolledEvent& scrollEvent)
        {
            // Accumulate scroll input so the update loop can apply it once per frame.
            m_PendingScrollDelta += scrollEvent.GetYOffset();

            return false;
        });

    // The input manager already tracks button state, so we only reset editor-specific cursors here.
    l_Dispatcher.Dispatch<Trident::MouseButtonPressedEvent>([this](Trident::MouseButtonPressedEvent& mouseButtonEvent)
        {
            const Trident::MouseCode l_Button = mouseButtonEvent.GetMouseButton();

            if (l_Button == Trident::Mouse::ButtonRight)
            {
                m_ResetRotateOrbitReference = true;
            }

            // Reset cursor deltas so fresh drags begin from the click point regardless of button.
            if (l_Button == Trident::Mouse::ButtonRight ||
                l_Button == Trident::Mouse::ButtonMiddle ||
                l_Button == Trident::Mouse::ButtonLeft)
            {
                m_PendingCursorDelta = glm::vec2{ 0.0f, 0.0f };
            }

            return false;
        });

    l_Dispatcher.Dispatch<Trident::MouseButtonReleasedEvent>([this](Trident::MouseButtonReleasedEvent& mouseButtonEvent)
        {
            if (mouseButtonEvent.GetMouseButton() == Trident::Mouse::ButtonRight)
            {
                m_IsRotateOrbitActive = false;
                m_ResetRotateOrbitReference = true;
            }

            return false;
        });
}

bool ApplicationLayer::HandleFileDrop(Trident::FileDropEvent& event)
{
    // File drops arrive via the engine event queue, so rely on the cached cursor
    // position sourced from MouseMovedEvent instead of querying ImGui directly.
    const bool l_HasMousePosition = m_HasCursorPosition && std::isfinite(m_CurrentCursorPosition.x) && std::isfinite(m_CurrentCursorPosition.y);
    if (!l_HasMousePosition)
    {
        return false;
    }

    const ImVec2 l_MousePosition{ m_CurrentCursorPosition.x, m_CurrentCursorPosition.y };
    const bool l_IsWithinViewport = m_ViewportPanel.ContainsPoint(l_MousePosition);

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
    // Guard against stale state before the window system has provided any cursor coordinates.
    if (!m_HasCursorPosition)
    {
        m_PendingCursorDelta = glm::vec2{ 0.0f, 0.0f };
        m_PendingScrollDelta = 0.0f;

        return;
    }

    // Restrict camera updates to the viewport so other panels remain scrollable and do not steal l_Focus.
    const bool l_HasFocus = m_ViewportPanel.IsFocused() && m_ViewportPanel.IsHovered();
    if (!l_HasFocus)
    {
        m_IsRotateOrbitActive = false;
        m_ResetRotateOrbitReference = true;
        m_PendingCursorDelta = glm::vec2{ 0.0f, 0.0f };
        m_PendingScrollDelta = 0.0f;

        return;
    }

    const float l_DeltaTime = std::max(deltaTime, 0.0f);
    const glm::vec3 l_WorldUp{ 0.0f, 1.0f, 0.0f };

    // Query the shared input manager once so all calculations share the same snapshot of key/mouse state.
    const Trident::Input& l_Input = Trident::Input::Get();
    const bool l_IsAltDown = l_Input.IsKeyDown(Trident::Key::LeftAlt) || l_Input.IsKeyDown(Trident::Key::RightAlt);
    const bool l_IsShiftDown = l_Input.IsKeyDown(Trident::Key::LeftShift) || l_Input.IsKeyDown(Trident::Key::RightShift);

    // Handle one-shot frame request before applying input deltas.
    if (l_Input.IsKeyPressed(Trident::Key::F))
    {
        // Frame selection reacts to the rising edge so holding F does not repeatedly recenter the camera.
        FrameSelection();
    }

    // Store the cursor location so the next drag can start without a large l_Delta jump when the button is pressed.
    if (m_ResetRotateOrbitReference)
    {
        m_LastCursorPosition = m_CurrentCursorPosition;
        m_ResetRotateOrbitReference = false;
    }

    // Current state
    glm::vec3 l_CursorPosition = m_EditorCamera.GetPosition();

    // Start with existing targets
    m_TargetYawDegrees = m_EditorYawDegrees;
    m_TargetPitchDegrees = m_EditorPitchDegrees;
    m_TargetPosition = l_CursorPosition;

    // Capture mouse buttons once so orbit/fly/pan toggles read consistent values even if callbacks arrive mid-frame.
    const bool l_RightMouseDown = l_Input.IsMouseButtonDown(Trident::Mouse::ButtonRight);
    const bool l_LeftMouseDown = l_Input.IsMouseButtonDown(Trident::Mouse::ButtonLeft);
    const bool l_MiddleMouseDown = l_Input.IsMouseButtonDown(Trident::Mouse::ButtonMiddle);

    const bool l_FlyMode = l_RightMouseDown && !l_IsAltDown;     // RMB
    const bool l_OrbitMode = l_IsAltDown && l_LeftMouseDown;       // Alt+LMB
    const bool l_PanMode = l_MiddleMouseDown || (l_IsAltDown && l_MiddleMouseDown); // MMB

    if (l_FlyMode)
    {
        m_IsRotateOrbitActive = true;
        m_TargetYawDegrees += m_PendingCursorDelta.x * m_MouseRotationSpeed;
        m_TargetPitchDegrees = std::clamp(m_TargetPitchDegrees - m_PendingCursorDelta.y * m_MouseRotationSpeed, -89.0f, 89.0f);
    }
    else if (l_OrbitMode)
    {
        m_TargetYawDegrees += m_PendingCursorDelta.x * m_MouseRotationSpeed;
        m_TargetPitchDegrees = std::clamp(m_TargetPitchDegrees - m_PendingCursorDelta.y * m_MouseRotationSpeed, -89.0f, 89.0f);
        glm::vec3 l_Forward = ForwardFromYawPitch(m_TargetYawDegrees, m_TargetPitchDegrees);
        m_OrbitDistance = std::max(m_OrbitDistance, m_MinOrbitDistance);
        m_TargetPosition = m_CameraPivot - l_Forward * m_OrbitDistance;
    }
    else
    {
        m_IsRotateOrbitActive = false;
    }

    // Recompute basis from targets
    glm::vec3 l_TargetForward = ForwardFromYawPitch(m_TargetYawDegrees, m_TargetPitchDegrees);
    glm::vec3 l_TargetRight = glm::normalize(glm::cross(l_TargetForward, l_WorldUp));
    if (!std::isfinite(l_TargetRight.x))
    {
        l_TargetRight = { 1.0f, 0.0f, 0.0f };
    }
    glm::vec3 l_TargetUp = glm::normalize(glm::cross(l_TargetRight, l_TargetForward));

    const auto l_IsFiniteVec3 = [](const glm::vec3& a_Value) -> bool
        {
            return std::isfinite(a_Value.x) && std::isfinite(a_Value.y) && std::isfinite(a_Value.z);
        };

    // Read the actual camera orientation so translational motion follows what the user currently sees on screen.
    // Query the current camera basis vectors so translation matches the live view.
    glm::vec3 l_CurrentForward = m_EditorCamera.GetForwardDirection();
    if (!l_IsFiniteVec3(l_CurrentForward))
    {
        l_CurrentForward = l_TargetForward;
    }

    glm::vec3 l_CurrentRight = m_EditorCamera.GetRightDirection();
    if (!l_IsFiniteVec3(l_CurrentRight))
    {
        l_CurrentRight = l_TargetRight;
    }

    glm::vec3 l_CurrentUp = m_EditorCamera.GetUpDirection();
    if (!l_IsFiniteVec3(l_CurrentUp))
    {
        l_CurrentUp = l_TargetUp;
    }

    const float l_SpeedMultiplier = l_IsShiftDown ? m_CameraBoostMultiplier : 1.0f;
    float l_MoveStep = m_CameraMoveSpeed * l_SpeedMultiplier * l_DeltaTime;

    if (l_FlyMode)
    {
        // Use the camera's smoothed forward/right vectors so WASD motion stays in lockstep with the rendered view.
        // The dedicated input manager exposes explicit checks for each key, making it easy to remap or extend controls later.
        const bool l_KeyWDown = l_Input.IsKeyDown(Trident::Key::W);
        const bool l_KeySDown = l_Input.IsKeyDown(Trident::Key::S);
        const bool l_KeyDDown = l_Input.IsKeyDown(Trident::Key::D);
        const bool l_KeyADown = l_Input.IsKeyDown(Trident::Key::A);
        const bool l_KeyEDown = l_Input.IsKeyDown(Trident::Key::E);
        const bool l_KeyQDown = l_Input.IsKeyDown(Trident::Key::Q);

        if (l_KeyWDown)
        {
            m_TargetPosition += l_CurrentForward * l_MoveStep;
        }

        if (l_KeySDown)
        {
            m_TargetPosition -= l_CurrentForward * l_MoveStep;
        }

        if (l_KeyDDown)
        {
            m_TargetPosition += l_CurrentRight * l_MoveStep;
        }

        if (l_KeyADown)
        {
            m_TargetPosition -= l_CurrentRight * l_MoveStep;
        }

        if (l_KeyEDown)
        {
            m_TargetPosition += l_CurrentUp * l_MoveStep;
        }

        if (l_KeyQDown)
        {
            m_TargetPosition -= l_CurrentUp * l_MoveStep;
        }
    }

    if (l_PanMode)
    {
        const float l_Distance = std::max(glm::length(m_TargetPosition - m_CameraPivot), 1.0f);
        const float l_PanScale = l_Distance * m_PanSpeedFactor;
        const glm::vec3 l_PanDelta = (-l_TargetRight * m_PendingCursorDelta.x + l_TargetUp * m_PendingCursorDelta.y) * l_PanScale;
        m_TargetPosition += l_PanDelta;
        if (l_OrbitMode) m_CameraPivot += l_PanDelta; // keep pivot under cursor while orbit-panning
    }

    if (std::abs(m_PendingScrollDelta) > std::numeric_limits<float>::epsilon())
    {
        if (l_FlyMode)
        {
            // Scroll while RMB adjusts fly speed exponentially within limits
            float l_Scale = std::exp(m_PendingScrollDelta * 0.1f);
            m_CameraMoveSpeed = glm::clamp(m_CameraMoveSpeed * l_Scale, m_MinMoveSpeed, m_MaxMoveSpeed);
        }
        else if (l_IsAltDown || l_OrbitMode)
        {
            // Dolly to pivot
            float l_Delta = -m_PendingScrollDelta * (m_OrbitDistance * m_DollySpeedFactor);
            m_OrbitDistance = std::max(m_OrbitDistance + l_Delta, m_MinOrbitDistance);
            glm::vec3 l_Forward = ForwardFromYawPitch(m_TargetYawDegrees, m_TargetPitchDegrees);
            m_TargetPosition = m_CameraPivot - l_Forward * m_OrbitDistance;
        }
        else
        {
            // Generic dolly along forward
            // Align dolly movement with the camera's current facing so zooms do not drift when rotation smoothing is active.
            m_TargetPosition += l_CurrentForward * (m_PendingScrollDelta * m_MouseZoomSpeed);
        }
    }

    auto l_Ease = [](float smoothing, float dtv) -> float
        {
            return smoothing <= 0.0f ? 1.0f : (1.0f - std::exp(-smoothing * dtv));
        };

    const float l_PositionAlpha = l_Ease(m_PosSmoothing, l_DeltaTime);
    const float l_RotationAlpha = l_Ease(m_RotSmoothing, l_DeltaTime);

    // Position smoothing
    glm::vec3 l_NewPosition = l_CursorPosition + (m_TargetPosition - l_CursorPosition) * l_PositionAlpha;

    // Angle smoothing in degrees
    m_EditorYawDegrees = m_EditorYawDegrees + (m_TargetYawDegrees - m_EditorYawDegrees) * l_RotationAlpha;
    m_EditorPitchDegrees = m_EditorPitchDegrees + (m_TargetPitchDegrees - m_EditorPitchDegrees) * l_RotationAlpha;

    m_EditorCamera.SetPosition(l_NewPosition);
    m_EditorCamera.SetRotation({ m_EditorPitchDegrees, m_EditorYawDegrees, 0.0f });

    // Housekeeping
    m_LastCursorPosition = m_CurrentCursorPosition;
    m_PendingCursorDelta = glm::vec2{ 0.0f, 0.0f };
    m_PendingScrollDelta = 0.0f;
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
    glm::vec3 l_ToPivot = glm::normalize(m_CameraPivot - m_EditorCamera.GetPosition());
    const float l_YAW = std::atan2(l_ToPivot.z, l_ToPivot.x) * 57.29577951308232f;  // rad->deg
    const float l_Pitch = std::asin(std::clamp(l_ToPivot.y, -1.0f, 1.0f)) * 57.29577951308232f;

    m_TargetYawDegrees = l_YAW;
    m_TargetPitchDegrees = std::clamp(l_Pitch, -89.0f, 89.0f);
    const glm::vec3 l_Forward = ForwardFromYawPitch(m_TargetYawDegrees, m_TargetPitchDegrees);
    m_TargetPosition = m_CameraPivot - l_Forward * m_OrbitDistance;
}

glm::vec3 ApplicationLayer::ForwardFromYawPitch(float yawDegrees, float pitchDegrees)
{
    const float l_YAW = DegToRad(yawDegrees);
    const float l_Pitch = DegToRad(pitchDegrees);
    const float l_CosPoint  = std::cos(l_Pitch);
    glm::vec3 l_Forward{ l_CosPoint  * std::cos(l_YAW), std::sin(l_Pitch), l_CosPoint  * std::sin(l_YAW) };
    if (!std::isfinite(l_Forward.x) || !std::isfinite(l_Forward.y) || !std::isfinite(l_Forward.z))
    {
        return glm::vec3{ 0.0f, 0.0f, -1.0f };
    }
    
    return glm::normalize(l_Forward);
}