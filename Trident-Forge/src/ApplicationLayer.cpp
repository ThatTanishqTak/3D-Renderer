#include "ApplicationLayer.h"

#include <imgui.h>
#include <ImGuizmo.h>

#include <string>
#include <vector>
#include <limits>
#include <algorithm>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include "Core/Utilities.h"
#include "Camera/CameraComponent.h"
#include "Renderer/Renderer.h"
#include "ECS/Components/TransformComponent.h"
#include "ECS/Components/SpriteComponent.h"
#include "ECS/Components/LightComponent.h"

namespace
{
    // Compose a transform matrix from the ECS component for ImGuizmo consumption.
    glm::mat4 ComposeTransform(const Trident::Transform& transform)
    {
        glm::mat4 l_ModelMatrix{ 1.0f };
        l_ModelMatrix = glm::translate(l_ModelMatrix, transform.Position);
        l_ModelMatrix = glm::rotate(l_ModelMatrix, glm::radians(transform.Rotation.x), glm::vec3{ 1.0f, 0.0f, 0.0f });
        l_ModelMatrix = glm::rotate(l_ModelMatrix, glm::radians(transform.Rotation.y), glm::vec3{ 0.0f, 1.0f, 0.0f });
        l_ModelMatrix = glm::rotate(l_ModelMatrix, glm::radians(transform.Rotation.z), glm::vec3{ 0.0f, 0.0f, 1.0f });
        l_ModelMatrix = glm::scale(l_ModelMatrix, transform.Scale);

        return l_ModelMatrix;
    }

    // Convert an ImGuizmo-manipulated matrix back to the engine's transform component.
    Trident::Transform DecomposeTransform(const glm::mat4& modelMatrix, const Trident::Transform& defaultTransform)
    {
        glm::vec3 l_Scale{};
        glm::quat l_Rotation{};
        glm::vec3 l_Translation{};
        glm::vec3 l_Skew{};
        glm::vec4 l_Perspective{};

        if (!glm::decompose(modelMatrix, l_Scale, l_Rotation, l_Translation, l_Skew, l_Perspective))
        {
            // Preserve the previous values if decomposition fails, avoiding sudden jumps.
            return defaultTransform;
        }

        Trident::Transform l_Result = defaultTransform;
        l_Result.Position = l_Translation;
        l_Result.Scale = l_Scale;
        l_Result.Rotation = glm::degrees(glm::eulerAngles(glm::normalize(l_Rotation)));

        return l_Result;
    }

    // Produce a projection matrix based on the camera component settings and desired viewport aspect ratio.
    glm::mat4 BuildCameraProjectionMatrix(const Trident::CameraComponent& cameraComponent, float viewportAspect)
    {
        float l_Aspect = cameraComponent.OverrideAspectRatio ? cameraComponent.AspectRatio : viewportAspect;
        l_Aspect = std::max(l_Aspect, 0.0001f);

        if (cameraComponent.UseCustomProjection)
        {
            // Advanced users can inject a bespoke matrix; the editor relays it without modification.
            return cameraComponent.CustomProjection;
        }

        if (cameraComponent.Projection == Trident::ProjectionType::Orthographic)
        {
            // Orthographic size represents the vertical span; derive width from the resolved aspect ratio.
            const float l_HalfHeight = cameraComponent.OrthographicSize * 0.5f;
            const float l_HalfWidth = l_HalfHeight * l_Aspect;
            glm::mat4 l_Projection = glm::ortho(-l_HalfWidth, l_HalfWidth, -l_HalfHeight, l_HalfHeight, cameraComponent.NearClip, cameraComponent.FarClip);
            l_Projection[1][1] *= -1.0f;
            
            return l_Projection;
        }

        glm::mat4 l_Projection = glm::perspective(glm::radians(cameraComponent.FieldOfView), l_Aspect, cameraComponent.NearClip, cameraComponent.FarClip);
        l_Projection[1][1] *= -1.0f;
        
        return l_Projection;
    }

    // Dedicated sentinel used when no entity is highlighted inside the inspector.
    constexpr Trident::ECS::Entity s_InvalidEntity = std::numeric_limits<Trident::ECS::Entity>::max();
}

ApplicationLayer::ApplicationLayer()
{
    // Initialize logging and the Forge window
    Trident::Utilities::Log::Init();

    m_Window = std::make_unique<Trident::Window>(1920, 1080, "Trident-Forge");
    m_Engine = std::make_unique<Trident::Application>(*m_Window);

    // Start the engine
    m_Engine->Init();

    // Wire editor panels to the freshly initialised engine and supporting systems.
    m_ContentBrowserPanel.SetEngine(m_Engine.get());
    m_ContentBrowserPanel.SetOnnxRuntime(&m_ONNX);

    // Set up the ImGui layer
    m_ImGuiLayer = std::make_unique<Trident::UI::ImGuiLayer>();
    m_ImGuiLayer->Init(m_Window->GetNativeWindow(), Trident::Application::GetInstance(), Trident::Application::GetPhysicalDevice(), Trident::Application::GetDevice(),
        Trident::Application::GetQueueFamilyIndices().GraphicsFamily.value(), Trident::Application::GetGraphicsQueue(), Trident::Application::GetRenderer().GetRenderPass(),
        Trident::Application::GetRenderer().GetImageCount(), Trident::Application::GetRenderer().GetCommandPool());
    
    Trident::Application::GetRenderer().SetImGuiLayer(m_ImGuiLayer.get());

    // Initialise selection state so panels correctly reflect the lack of focus on startup.
    m_SelectedEntity = s_InvalidEntity;
    m_GizmoOperation = ImGuizmo::TRANSLATE;
    m_GizmoMode = ImGuizmo::LOCAL;

    m_InspectorPanel.SetGizmoState(&m_GizmoOperation, &m_GizmoMode);
}

ApplicationLayer::~ApplicationLayer()
{
    // Gracefully shut down engine and UI
    TR_INFO("-------SHUTTING DOWN APPLICATION-------");

    if (m_ImGuiLayer)
    {
        m_ImGuiLayer->Shutdown();
    }

    if (m_Engine)
    {
        m_Engine->Shutdown();
    }

    TR_INFO("-------APPLICATION SHUTDOWN-------");
}

void ApplicationLayer::Run()
{
    // Main application loop aligning editor windows with Unreal terminology for future docking profiles.
    while (!m_Window->ShouldClose())
    {
        m_Engine->Update();
        m_ImGuiLayer->BeginFrame();

        // Surface layout persistence controls so designers can intentionally snapshot or restore workspace arrangements.
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("Layout"))
            {
                if (ImGui::MenuItem("Save Current Layout"))
                {
                    m_ImGuiLayer->SaveLayoutToDisk();
                }

                if (ImGui::MenuItem("Load Layout From Disk"))
                {
                    if (!m_ImGuiLayer->LoadLayoutFromDisk())
                    {
                        // If loading fails we rebuild the default so the dockspace stays valid.
                        m_ImGuiLayer->ResetLayoutToDefault();
                    }
                }

                if (ImGui::MenuItem("Reset Layout To Default"))
                {
                    m_ImGuiLayer->ResetLayoutToDefault();
                }

                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }

        m_ContentBrowserPanel.Render();

        m_SceneHierarchyPanel.SetSelectedEntity(m_SelectedEntity);
        m_SceneHierarchyPanel.Render();
        m_SelectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();

        m_ViewportPanel.SetSelectedEntity(m_SelectedEntity);
        m_ViewportPanel.Render();

        // Sync the viewport's selection state so downstream panels (e.g., inspector) respect viewport-driven deselection.
        m_SelectedEntity = m_ViewportPanel.GetSelectedEntity();

        m_InspectorPanel.SetSelectedEntity(m_SelectedEntity);
        m_InspectorPanel.Render();

        m_OutputPanel.Render();

        // Render the gizmo on top of the viewport once all inspector edits are applied.
        DrawTransformGizmo(m_SelectedEntity);

        m_ImGuiLayer->EndFrame();
        m_Engine->RenderScene();
    }
}

void ApplicationLayer::DrawTransformGizmo(Trident::ECS::Entity a_SelectedEntity)
{
    // Even when no entity is bound we start a frame so ImGuizmo can clear any persistent state.
    ImGuizmo::BeginFrame();

    if (a_SelectedEntity == s_InvalidEntity)
    {
        return;
    }

    Trident::ECS::Registry& l_Registry = Trident::Application::GetRegistry();
    if (!l_Registry.HasComponent<Trident::Transform>(a_SelectedEntity))
    {
        return;
    }

    // Fetch the viewport rectangle published by the renderer so the gizmo aligns with the active scene view.

    const Trident::ViewportInfo l_ViewportInfo = Trident::Application::GetRenderer().GetViewport();
    ImVec2 l_RectPosition{};
    ImVec2 l_RectSize{};

    if (l_ViewportInfo.Size.x > 0.0f && l_ViewportInfo.Size.y > 0.0f)
    {
        // Use the viewport data directly so the gizmo tracks the rendered scene even with multi-viewport enabled.
        l_RectPosition = ImVec2{ l_ViewportInfo.Position.x, l_ViewportInfo.Position.y };
        l_RectSize = ImVec2{ l_ViewportInfo.Size.x, l_ViewportInfo.Size.y };
    }
    else
    {
        // Fall back to the main viewport bounds when the renderer has not published explicit viewport information yet.
        const ImGuiViewport* l_MainViewport = ImGui::GetMainViewport();
        l_RectPosition = l_MainViewport->Pos;
        l_RectSize = l_MainViewport->Size;
    }

    // Mirror the Scene panel camera selection logic so the gizmo uses whichever camera the user targeted.
    glm::mat4 l_ViewMatrix{ 1.0f };
    glm::mat4 l_ProjectionMatrix{ 1.0f };
    bool l_UseOrthographicGizmo = false;

    const Trident::ECS::Entity l_SelectedViewportCamera = m_ViewportPanel.GetSelectedCamera();

    if (l_SelectedViewportCamera != s_InvalidEntity
        && l_Registry.HasComponent<Trident::CameraComponent>(l_SelectedViewportCamera)
        && l_Registry.HasComponent<Trident::Transform>(l_SelectedViewportCamera))
    {
        // A scene camera is actively selected; derive view/projection parameters from the ECS components.
        const Trident::CameraComponent& l_CameraComponent = l_Registry.GetComponent<Trident::CameraComponent>(l_SelectedViewportCamera);
        const Trident::Transform& l_CameraTransform = l_Registry.GetComponent<Trident::Transform>(l_SelectedViewportCamera);

        const glm::mat4 l_ModelMatrix = ComposeTransform(l_CameraTransform);
        l_ViewMatrix = glm::inverse(l_ModelMatrix);
        const float l_AspectRatio = l_RectSize.y > 0.0f ? l_RectSize.x / l_RectSize.y : 1.0f;
        l_ProjectionMatrix = BuildCameraProjectionMatrix(l_CameraComponent, l_AspectRatio);
        l_UseOrthographicGizmo = !l_CameraComponent.UseCustomProjection && l_CameraComponent.Projection == Trident::ProjectionType::Orthographic;
    }
    else
    {
        // Fall back to the editor camera when no ECS-driven viewport camera is active.
        Trident::Camera& l_Camera = Trident::Application::GetRenderer().GetCamera();
        l_ViewMatrix = l_Camera.GetViewMatrix();
        const float l_AspectRatio = l_RectSize.y > 0.0f ? l_RectSize.x / l_RectSize.y : 1.0f;
        l_ProjectionMatrix = glm::perspective(glm::radians(l_Camera.GetFOV()), l_AspectRatio, l_Camera.GetNearClip(), l_Camera.GetFarClip());
        l_ProjectionMatrix[1][1] *= -1.0f;
        l_UseOrthographicGizmo = false;
    }

    Trident::Transform& l_EntityTransform = l_Registry.GetComponent<Trident::Transform>(a_SelectedEntity);
    glm::mat4 l_ModelMatrix = ComposeTransform(l_EntityTransform);

    ImGuizmo::SetOrthographic(l_UseOrthographicGizmo);
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(l_RectPosition.x, l_RectPosition.y, l_RectSize.x, l_RectSize.y);

    if (ImGuizmo::Manipulate(glm::value_ptr(l_ViewMatrix), glm::value_ptr(l_ProjectionMatrix), m_GizmoOperation, m_GizmoMode, glm::value_ptr(l_ModelMatrix)))
    {
        // Sync the manipulated matrix back into the ECS so gameplay systems stay authoritative.
        Trident::Transform l_UpdatedTransform = DecomposeTransform(l_ModelMatrix, l_EntityTransform);
        l_EntityTransform = l_UpdatedTransform;
        Trident::Application::GetRenderer().SetTransform(l_EntityTransform);
    }

    // Potential enhancement: expose snapping increments for translation/rotation/scale so artists can toggle grid alignment.
}