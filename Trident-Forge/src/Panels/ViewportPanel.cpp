#include "ViewportPanel.h"

#include <ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>

#include <utility>

namespace
{
    glm::mat4 ComposeMatrixFromTransform(const Trident::Transform& a_Transform)
    {
        // Translate the component vectors into a matrix ImGuizmo understands.
        float l_Translation[3]{ a_Transform.Position.x, a_Transform.Position.y, a_Transform.Position.z };
        float l_Rotation[3]{ a_Transform.Rotation.x, a_Transform.Rotation.y, a_Transform.Rotation.z };
        float l_Scale[3]{ a_Transform.Scale.x, a_Transform.Scale.y, a_Transform.Scale.z };

        glm::mat4 l_Model{ 1.0f };
        ImGuizmo::RecomposeMatrixFromComponents(l_Translation, l_Rotation, l_Scale, glm::value_ptr(l_Model));

        return l_Model;
    }

    Trident::Transform DecomposeMatrixToTransform(const glm::mat4& a_ModelMatrix)
    {
        // Break down the manipulated matrix so we can feed the renderer component data again.
        float l_Translation[3]{};
        float l_Rotation[3]{};
        float l_Scale[3]{};
        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(a_ModelMatrix), l_Translation, l_Rotation, l_Scale);

        Trident::Transform l_Result{};
        l_Result.Position = { l_Translation[0], l_Translation[1], l_Translation[2] };
        l_Result.Rotation = { l_Rotation[0], l_Rotation[1], l_Rotation[2] };
        l_Result.Scale = { l_Scale[0], l_Scale[1], l_Scale[2] };

        return l_Result;
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

    // The viewport only wants exclusive controls when the window is focused/hovered and
    // ImGui is not already consuming mouse or keyboard input. Additional tools can use
    // m_IsCameraControlEnabled to decide if they should respond this frame.
    m_IsCameraControlEnabled = m_IsFocused && m_IsHovered && !l_IO.WantCaptureMouse && !l_IO.WantCaptureKeyboard;

    // Forward the active camera entity to the renderer so off-screen render targets stay in sync.
    if (m_ActiveCameraEntity != 0)
    {
        Trident::RenderCommand::SetViewportCamera(m_ActiveCameraEntity);
    }

    // TODO: Hook editor-specific camera controllers here so they can react to focus changes.
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

    // Determine how much space is available for the scene texture.
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
            // Draw the viewport texture. UVs are flipped vertically so the render target appears correct.
            ImGui::Image(reinterpret_cast<ImTextureID>(l_Descriptor), l_ContentRegion, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
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

                // Retrieve the active selection transform and hand it to ImGuizmo for manipulation.
                const Trident::Transform l_CurrentTransform = Trident::RenderCommand::GetTransform();
                glm::mat4 l_ModelMatrix = ComposeMatrixFromTransform(l_CurrentTransform);

                // Drive the gizmo and push edits back into the renderer when the user drags the handles.
                if (ImGuizmo::Manipulate(glm::value_ptr(l_ViewMatrix), glm::value_ptr(l_ProjectionMatrix), m_GizmoState->GetOperation(), m_GizmoState->GetMode(), glm::value_ptr(l_ModelMatrix)))
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

// SetCameraEntity stores the active camera and notifies the renderer immediately so the viewport updates.
void ViewportPanel::SetCameraEntity(Trident::ECS::Entity cameraEntity)
{
    m_ActiveCameraEntity = cameraEntity;

    if (m_ActiveCameraEntity != 0)
    {
        Trident::RenderCommand::SetViewportCamera(m_ActiveCameraEntity);
    }
    // TODO: Allow clearing the camera when switching to runtime play mode.
}

void ViewportPanel::SetGizmoState(GizmoState* gizmoState)
{
    // Cache the shared gizmo configuration so the viewport can react to inspector changes.
    m_GizmoState = gizmoState;
}

void ViewportPanel::SetAssetDropHandler(std::function<void(const std::vector<std::string>&)> assetDropHandler)
{
    // Store the callback so the application layer can process accepted payloads.
    m_OnAssetDrop = std::move(assetDropHandler);
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