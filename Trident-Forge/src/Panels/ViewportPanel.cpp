#include "ViewportPanel.h"

#include <ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/common.hpp>
#include <glm/vec4.hpp>

#include "Application/Startup.h"
#include "ECS/Registry.h"
#include "ECS/Components/TransformComponent.h"

#include <utility>
#include <algorithm>
#include <cmath>
#include <array>
#include <numeric>
#include <limits>

namespace
{
    glm::mat4 ComposeMatrixFromTransform(const Trident::Transform& transform)
    {
        // Translate the component vectors into a matrix ImGuizmo understands.
        float l_Translation[3]{ transform.Position.x, transform.Position.y, transform.Position.z };
        float l_Rotation[3]{ transform.Rotation.x, transform.Rotation.y, transform.Rotation.z };
        float l_Scale[3]{ transform.Scale.x, transform.Scale.y, transform.Scale.z };

        glm::mat4 l_Model{ 1.0f };
        ImGuizmo::RecomposeMatrixFromComponents(l_Translation, l_Rotation, l_Scale, glm::value_ptr(l_Model));

        return l_Model;
    }

    Trident::Transform DecomposeMatrixToTransform(const glm::mat4& modelMatrix)
    {
        // Break down the manipulated matrix so we can feed the renderer component data again.
        float l_Translation[3]{};
        float l_Rotation[3]{};
        float l_Scale[3]{};
        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(modelMatrix), l_Translation, l_Rotation, l_Scale);

        Trident::Transform l_Result{};
        l_Result.Position = { l_Translation[0], l_Translation[1], l_Translation[2] };
        l_Result.Rotation = { l_Rotation[0], l_Rotation[1], l_Rotation[2] };
        l_Result.Scale = { l_Scale[0], l_Scale[1], l_Scale[2] };

        return l_Result;
    }

    constexpr std::array<glm::vec3, 8> s_SceneGizmoVertices
    {
        glm::vec3{ -0.5f, -0.5f, -0.5f },
        glm::vec3{ 0.5f, -0.5f, -0.5f },
        glm::vec3{ 0.5f, 0.5f, -0.5f },
        glm::vec3{ -0.5f, 0.5f, -0.5f },
        glm::vec3{ -0.5f, -0.5f, 0.5f },
        glm::vec3{ 0.5f, -0.5f, 0.5f },
        glm::vec3{ 0.5f, 0.5f, 0.5f },
        glm::vec3{ -0.5f, 0.5f, 0.5f }
    };

    struct SceneGizmoFace
    {
        std::array<int32_t, 4> Indices{};
        glm::vec3 Normal{ 0.0f };
        glm::vec3 Direction{ 0.0f };
        ImU32 Color = IM_COL32_WHITE;
        const char* Label = nullptr;
    };

    constexpr std::array<SceneGizmoFace, 6> s_SceneGizmoFaces
    {
        SceneGizmoFace{ { 4, 5, 6, 7 }, glm::vec3{ 0.0f, 1.0f, 0.0f }, glm::vec3{ 0.0f, 1.0f, 0.0f }, IM_COL32(129, 199, 132, 255), "+Y" },
        SceneGizmoFace{ { 0, 1, 2, 3 }, glm::vec3{ 0.0f, -1.0f, 0.0f }, glm::vec3{ 0.0f, -1.0f, 0.0f }, IM_COL32(56, 142, 60, 200), "-Y" },
        SceneGizmoFace{ { 1, 5, 6, 2 }, glm::vec3{ 1.0f, 0.0f, 0.0f }, glm::vec3{ 1.0f, 0.0f, 0.0f }, IM_COL32(229, 115, 115, 255), "+X" },
        SceneGizmoFace{ { 0, 3, 7, 4 }, glm::vec3{ -1.0f, 0.0f, 0.0f }, glm::vec3{ -1.0f, 0.0f, 0.0f }, IM_COL32(183, 28, 28, 220), "-X" },
        SceneGizmoFace{ { 3, 2, 6, 7 }, glm::vec3{ 0.0f, 0.0f, 1.0f }, glm::vec3{ 0.0f, 0.0f, 1.0f }, IM_COL32(100, 181, 246, 255), "+Z" },
        SceneGizmoFace{ { 0, 1, 5, 4 }, glm::vec3{ 0.0f, 0.0f, -1.0f }, glm::vec3{ 0.0f, 0.0f, -1.0f }, IM_COL32(21, 101, 192, 220), "-Z" }
    };

    struct SceneGizmoCorner
    {
        int32_t VertexIndex = 0;
        glm::vec3 Direction{ 0.0f };
    };

    // Precompute the corner directions for the scene gizmo cube. Calling glm::normalize
    // inside a constexpr context fails with MSVC, so we manually expand the normalised
    // components for the axis-aligned diagonals.
    constexpr float s_CornerAxisComponent = 0.57735026919f; // 1 / sqrt(3).
    constexpr std::array<SceneGizmoCorner, 8> s_SceneGizmoCorners
    {
        SceneGizmoCorner{ 6, glm::vec3{ s_CornerAxisComponent, s_CornerAxisComponent, s_CornerAxisComponent } },
        SceneGizmoCorner{ 5, glm::vec3{ s_CornerAxisComponent, -s_CornerAxisComponent, s_CornerAxisComponent } },
        SceneGizmoCorner{ 7, glm::vec3{ -s_CornerAxisComponent, s_CornerAxisComponent, s_CornerAxisComponent } },
        SceneGizmoCorner{ 4, glm::vec3{ -s_CornerAxisComponent, -s_CornerAxisComponent, s_CornerAxisComponent } },
        SceneGizmoCorner{ 2, glm::vec3{ s_CornerAxisComponent, s_CornerAxisComponent, -s_CornerAxisComponent } },
        SceneGizmoCorner{ 1, glm::vec3{ s_CornerAxisComponent, -s_CornerAxisComponent, -s_CornerAxisComponent } },
        SceneGizmoCorner{ 3, glm::vec3{ -s_CornerAxisComponent, s_CornerAxisComponent, -s_CornerAxisComponent } },
        SceneGizmoCorner{ 0, glm::vec3{ -s_CornerAxisComponent, -s_CornerAxisComponent, -s_CornerAxisComponent } }
    };

    bool IsPointInsideTriangle(const ImVec2& point, const ImVec2& a, const ImVec2& b, const ImVec2& c)
    {
        const float l_Denominator = ((b.y - c.y) * (a.x - c.x)) + ((c.x - b.x) * (a.y - c.y));
        const float l_MinArea = std::numeric_limits<float>::epsilon();
        if (std::abs(l_Denominator) <= l_MinArea)
        {
            return false;
        }

        const float l_W1 = ((b.y - c.y) * (point.x - c.x) + (c.x - b.x) * (point.y - c.y)) / l_Denominator;
        const float l_W2 = ((c.y - a.y) * (point.x - c.x) + (a.x - c.x) * (point.y - c.y)) / l_Denominator;
        const float l_W3 = 1.0f - l_W1 - l_W2;

        const float l_Epsilon = -0.0001f;
        return (l_W1 >= l_Epsilon) && (l_W2 >= l_Epsilon) && (l_W3 >= l_Epsilon);
    }
}

ViewportPanel::ViewportPanel()
{
    // Gizmo configuration is now shared, so default setup occurs when the inspector connects.
}

// Update prepares the viewport for rendering by reading ImGui input state and
// pushing the currently selected camera down to the renderer.
void ViewportPanel::Update()
{
    // Query ImGui for the current IO state so we can decide if camera controls should run.
    ImGuiIO& l_IO = ImGui::GetIO();
}

// Render draws the ImGui viewport window and displays the renderer-provided texture.
void ViewportPanel::Render()
{
    // Build the main viewport window. Additional dockspace integration can rename this later.
    if (!ImGui::Begin("Scene"))
    {
        // If the window is collapsed we still need to clear state so camera controls disable correctly.
        m_IsFocused = false;
        m_IsHovered = false;
        ImGui::End();

        return;
    }

    // Record whether the viewport wants mouse/keyboard focus so other editor systems stay polite.
    m_IsFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
    m_IsHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

    // Determine how much space remains for the scene texture after toolbar controls are rendered.
    const ImVec2 l_ContentRegion = ImGui::GetContentRegionAvail();
    const glm::vec2 l_NewViewportSize{ l_ContentRegion.x, l_ContentRegion.y };

    // Skip rendering when the panel is hidden or sized to zero (docking transitions, layout edits, etc.).
    if (l_NewViewportSize.x > 0.0f && l_NewViewportSize.y > 0.0f)
    {
        const ImVec2 l_ViewportPos = ImGui::GetCursorScreenPos();
        
        // Persist the on-screen bounds of the viewport so drag-and-drop handlers can
        // later determine whether a file drop landed inside the rendered image.
        m_ViewportBoundsMin = l_ViewportPos;
        m_ViewportBoundsMax = ImVec2(l_ViewportPos.x + l_ContentRegion.x, l_ViewportPos.y + l_ContentRegion.y);

        // Reconfigure the renderer whenever the viewport size changes.
        if (l_NewViewportSize != m_CachedViewportSize)
        {
            Trident::ViewportInfo l_Info{};
            l_Info.ViewportID = m_ViewportID;
            l_Info.Position = { l_ViewportPos.x, l_ViewportPos.y };
            l_Info.Size = l_NewViewportSize;
            Trident::RenderCommand::SetViewport(l_Info);

            m_CachedViewportSize = l_NewViewportSize;
        }

        // Pull the Vulkan descriptor that ImGui understands so we can blit the scene into the panel.
        const VkDescriptorSet l_Descriptor = Trident::RenderCommand::GetViewportTexture();
        if (l_Descriptor != VK_NULL_HANDLE)
        {
            // Reset ImGuizmo's per-frame state before drawing the viewport so overlays start cleanly.
            ImGuizmo::BeginFrame();
            // Draw the viewport texture using standard UV orientation. The renderer already flips the projection
            // matrix for Vulkan, so the off-screen image is stored upright and does not require an additional flip.
            ImGui::Image(reinterpret_cast<ImTextureID>(l_Descriptor), l_ContentRegion, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
            // Surface the drawn image bounds immediately so the application layer can attach contextual menus.
            if (m_OnViewportContextMenu)
            {
                m_OnViewportContextMenu(m_ViewportBoundsMin, m_ViewportBoundsMax);
            }
            if (ImGui::BeginDragDropTarget())
            {
                const ImGuiPayload* l_Payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM");
                if (l_Payload != nullptr && l_Payload->Data != nullptr && l_Payload->DataSize > 0)
                {
                    const char* l_PathData = static_cast<const char*>(l_Payload->Data);
                    std::string l_PathString{ l_PathData, l_PathData + (l_Payload->DataSize - 1) };

                    if (m_OnAssetDrop)
                    {
                        std::vector<std::string> l_DroppedPaths{};
                        l_DroppedPaths.emplace_back(std::move(l_PathString));
                        m_OnAssetDrop(l_DroppedPaths);
                    }
                    // TODO: Support batched payloads once the content browser exposes multi-selection drags.
                }
                ImGui::EndDragDropTarget();
            }
            // Configure ImGuizmo so it renders directly on top of the viewport image.
            ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
            ImGuizmo::SetRect(l_ViewportPos.x, l_ViewportPos.y, l_ContentRegion.x, l_ContentRegion.y);

            const bool l_CanDisplayGizmo = (m_GizmoState != nullptr) && m_GizmoState->HasSelection();
            if (l_CanDisplayGizmo)
            {
                // Fetch the camera matrices used for the rendered image so the gizmo aligns perfectly.
                const glm::mat4 l_ViewMatrix = Trident::RenderCommand::GetViewportViewMatrix();
                const glm::mat4 l_ProjectionMatrix = Trident::RenderCommand::GetViewportProjectionMatrix();
                glm::mat4 l_GizmoProjectionMatrix = l_ProjectionMatrix;
                // ImGuizmo assumes a standard clip space where the Y axis points upwards. The renderer already flips
                // the projection matrix to satisfy Vulkan's coordinate system, so we unflip the matrix here to ensure
                // gizmo translation directions remain intuitive for artists.
                l_GizmoProjectionMatrix[1][1] *= -1.0f;

                // Retrieve the active selection transform and hand it to ImGuizmo for manipulation.
                const Trident::Transform l_CurrentTransform = Trident::RenderCommand::GetTransform();
                glm::mat4 l_ModelMatrix = ComposeMatrixFromTransform(l_CurrentTransform);

                // Drive the gizmo and push edits back into the renderer when the user drags the handles.
                if (ImGuizmo::Manipulate(glm::value_ptr(l_ViewMatrix), glm::value_ptr(l_GizmoProjectionMatrix), m_GizmoState->GetOperation(), m_GizmoState->GetMode(),
                    glm::value_ptr(l_ModelMatrix)))
                {
                    const Trident::Transform l_UpdatedTransform = DecomposeMatrixToTransform(l_ModelMatrix);
                    Trident::RenderCommand::SetTransform(l_UpdatedTransform);
                }

                // While the gizmo is active we suspend camera controls so inputs are not double-consumed.
                if (ImGuizmo::IsUsing())
                {
                    m_IsCameraControlEnabled = false;
                }
            }
        }
        else
        {
            // TODO: Provide a debug overlay here to explain missing render data.
        }
    }
    else
    {
        // When the viewport is hidden or collapsed we clear the cached bounds so stale
        // rectangles do not report false positives for drag-and-drop hit tests.
        m_ViewportBoundsMin = ImVec2(0.0f, 0.0f);
        m_ViewportBoundsMax = ImVec2(0.0f, 0.0f);
        // TODO: Account for multi-viewport/HiDPI scaling adjustments before submitting viewport info.
    }

    // Future contributors can add overlay or metrics widgets below before closing the window.
    ImGui::End();
}

void ViewportPanel::SetGizmoState(GizmoState* gizmoState)
{
    // Cache the shared gizmo configuration so the viewport can react to inspector changes.
    m_GizmoState = gizmoState;
}

void ViewportPanel::SetSelectedEntity(Trident::ECS::Entity selectedEntity)
{
    // Store the hierarchy/inspector selection so navigation can track the same pivot.
    m_SelectedEntity = selectedEntity;
}

void ViewportPanel::SetAssetDropHandler(std::function<void(const std::vector<std::string>&)> assetDropHandler)
{
    // Store the callback so the application layer can process accepted payloads.
    m_OnAssetDrop = std::move(assetDropHandler);
}

void ViewportPanel::SetContextMenuHandler(std::function<void(const ImVec2&, const ImVec2&)> contextMenuHandler)
{
    // Mirror the handler provided by the application layer so viewport rendering can trigger context menus.
    m_OnViewportContextMenu = std::move(contextMenuHandler);
}

bool ViewportPanel::ContainsPoint(const ImVec2& point) const
{
    // Use the cached ImGui rectangle to verify if a screen-space point lies within the viewport.
    const bool l_HasValidBounds = (m_ViewportBoundsMax.x > m_ViewportBoundsMin.x) && (m_ViewportBoundsMax.y > m_ViewportBoundsMin.y);
    if (!l_HasValidBounds)
    {
        return false;
    }

    const bool l_WithinHorizontal = (point.x >= m_ViewportBoundsMin.x) && (point.x <= m_ViewportBoundsMax.x);
    const bool l_WithinVertical = (point.y >= m_ViewportBoundsMin.y) && (point.y <= m_ViewportBoundsMax.y);

    return l_WithinHorizontal && l_WithinVertical;
}

void ViewportPanel::FrameSelection()
{
    glm::vec3 l_Target{ 0.0f, 0.0f, 0.0f };
    float l_Distance = 5.0f;

    if (m_SelectedEntity != std::numeric_limits<Trident::ECS::Entity>::max())
    {
        Trident::ECS::Registry& l_Registry = Trident::Startup::GetRegistry();
        const bool l_HasTransform = l_Registry.HasComponent<Trident::Transform>(m_SelectedEntity);
        if (l_HasTransform)
        {
            const Trident::Transform& l_Transform = l_Registry.GetComponent<Trident::Transform>(m_SelectedEntity);
            l_Target = l_Transform.Position;

            const float l_MaxScale = std::max(std::max(std::abs(l_Transform.Scale.x), std::abs(l_Transform.Scale.y)), std::abs(l_Transform.Scale.z));
            const float l_NormalizedScale = std::max(l_MaxScale, 1.0f);
            l_Distance = std::max(l_NormalizedScale * 2.5f, 0.5f);
        }
    }
}