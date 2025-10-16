#include "ViewportPanel.h"

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
        // Reconfigure the renderer whenever the viewport size changes.
        if (l_NewViewportSize != m_CachedViewportSize)
        {
            const ImVec2 l_ViewportPos = ImGui::GetCursorScreenPos();

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
            // Draw the viewport texture. UVs are flipped vertically so the render target appears correct.
            ImGui::Image(reinterpret_cast<ImTextureID>(l_Descriptor), l_ContentRegion, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
        }
        else
        {
            // TODO: Provide a debug overlay here to explain missing render data.
        }
    }
    else
    {
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