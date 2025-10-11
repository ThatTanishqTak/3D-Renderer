#include "ImGuizmoLayer.h"

#include "Application.h"
#include "Panels/InspectorPanel.h"
#include "Panels/ViewportPanel.h"

#include "Camera/CameraComponent.h"
#include "ECS/Components/TransformComponent.h"
#include "Renderer/Renderer.h"

#include <imgui.h>

#include <algorithm>
#include <limits>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace
{
    /**
     * @brief Compose a model matrix from a transform component for ImGuizmo.
     */
    glm::mat4 ComposeTransform(const Trident::Transform& a_Transform)
    {
        glm::mat4 l_ModelMatrix{ 1.0f };
        l_ModelMatrix = glm::translate(l_ModelMatrix, a_Transform.Position);
        l_ModelMatrix = glm::rotate(l_ModelMatrix, glm::radians(a_Transform.Rotation.x), glm::vec3{ 1.0f, 0.0f, 0.0f });
        l_ModelMatrix = glm::rotate(l_ModelMatrix, glm::radians(a_Transform.Rotation.y), glm::vec3{ 0.0f, 1.0f, 0.0f });
        l_ModelMatrix = glm::rotate(l_ModelMatrix, glm::radians(a_Transform.Rotation.z), glm::vec3{ 0.0f, 0.0f, 1.0f });
        l_ModelMatrix = glm::scale(l_ModelMatrix, a_Transform.Scale);

        return l_ModelMatrix;
    }

    /**
     * @brief Convert a manipulated model matrix back into the engine transform structure.
     */
    Trident::Transform DecomposeTransform(const glm::mat4& a_ModelMatrix, const Trident::Transform& a_DefaultTransform)
    {
        glm::vec3 l_Scale{};
        glm::quat l_Rotation{};
        glm::vec3 l_Translation{};
        glm::vec3 l_Skew{};
        glm::vec4 l_Perspective{};

        if (!glm::decompose(a_ModelMatrix, l_Scale, l_Rotation, l_Translation, l_Skew, l_Perspective))
        {
            // Preserve the previous values if decomposition fails, avoiding sudden jumps.
            return a_DefaultTransform;
        }

        Trident::Transform l_Result = a_DefaultTransform;
        l_Result.Position = l_Translation;
        l_Result.Scale = l_Scale;
        l_Result.Rotation = glm::degrees(glm::eulerAngles(glm::normalize(l_Rotation)));

        return l_Result;
    }

    /**
     * @brief Build a projection matrix that mirrors the camera used in the viewport.
     */
    glm::mat4 BuildCameraProjectionMatrix(const Trident::CameraComponent& a_CameraComponent, float a_ViewportAspect)
    {
        float l_Aspect = a_CameraComponent.OverrideAspectRatio ? a_CameraComponent.AspectRatio : a_ViewportAspect;
        l_Aspect = std::max(l_Aspect, 0.0001f);

        if (a_CameraComponent.UseCustomProjection)
        {
            // Advanced users can inject a bespoke matrix; the editor relays it without modification.
            return a_CameraComponent.CustomProjection;
        }

        if (a_CameraComponent.Projection == Trident::ProjectionType::Orthographic)
        {
            // Orthographic size represents the vertical span; derive width from the resolved aspect ratio.
            const float l_HalfHeight = a_CameraComponent.OrthographicSize * 0.5f;
            const float l_HalfWidth = l_HalfHeight * l_Aspect;
            glm::mat4 l_Projection = glm::ortho(-l_HalfWidth, l_HalfWidth, -l_HalfHeight, l_HalfHeight, a_CameraComponent.NearClip, a_CameraComponent.FarClip);
            l_Projection[1][1] *= -1.0f;

            return l_Projection;
        }

        glm::mat4 l_Projection = glm::perspective(glm::radians(a_CameraComponent.FieldOfView), l_Aspect, a_CameraComponent.NearClip, a_CameraComponent.FarClip);
        l_Projection[1][1] *= -1.0f;

        return l_Projection;
    }

    /**
     * @brief Dedicated sentinel used when no entity is highlighted inside the inspector.
     */
    constexpr Trident::ECS::Entity s_InvalidEntity = std::numeric_limits<Trident::ECS::Entity>::max();
}

ImGuizmoLayer::ImGuizmoLayer()
    : m_GizmoOperation(ImGuizmo::TRANSLATE)
    , m_GizmoMode(ImGuizmo::LOCAL)
{
    // Default to translate/local so the gizmo feels familiar on startup.
}

void ImGuizmoLayer::Initialize(UI::InspectorPanel& a_InspectorPanel)
{
    // Provide the inspector with live pointers so its radio buttons can update the gizmo mode/state.
    a_InspectorPanel.SetGizmoState(&m_GizmoOperation, &m_GizmoMode);
}

void ImGuizmoLayer::Render(Trident::ECS::Entity a_SelectedEntity, UI::ViewportPanel& a_ViewportPanel)
{
    // Always kick off a frame so ImGuizmo clears stale state even when nothing is selected.
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

    const Trident::ECS::Entity l_SelectedViewportCamera = a_ViewportPanel.GetSelectedCamera();

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

    // Future work: expose snapping increments via the inspector so artists can align to grids precisely.
}