#include "Renderer/RenderCommand.h"

#include "Application.h"

namespace Trident
{
    void RenderCommand::Init()
    {
        Application::GetRenderer().Init();
    }

    void RenderCommand::Shutdown()
    {
        Application::GetRenderer().Shutdown();
    }

    void RenderCommand::DrawFrame()
    {
        Application::GetRenderer().DrawFrame();
    }

    void RenderCommand::RecreateSwapchain()
    {
        Application::GetRenderer().RecreateSwapchain();
    }

    void RenderCommand::SetTransform(const Transform& props)
    {
        Application::GetRenderer().SetTransform(props);
    }

    void RenderCommand::SetViewport(const ViewportInfo& info)
    {
        Application::GetRenderer().SetViewport(info);
    }

    void RenderCommand::SetClearColor(const glm::vec4& color)
    {
        // Forward the request so the renderer updates its cached clear colour immediately.
        Application::GetRenderer().SetClearColor(color);
    }

    Transform RenderCommand::GetTransform()
    {
        return Application::GetRenderer().GetTransform();
    }

    ViewportInfo RenderCommand::GetViewport()
    {
        return Application::GetRenderer().GetViewport();
    }

    VkDescriptorSet RenderCommand::GetViewportTexture()
    {
        return Application::GetRenderer().GetViewportTexture();
    }

    uint32_t RenderCommand::GetCurrentFrame()
    {
        return Application::GetRenderer().GetCurrentFrame();
    }

    glm::vec4 RenderCommand::GetClearColor()
    {
        // Provide callers with the renderer's clear colour so UI widgets can display the current setting.
        return Application::GetRenderer().GetClearColor();
    }
}