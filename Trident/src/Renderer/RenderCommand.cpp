#include "Renderer/RenderCommand.h"

#include "Application/Startup.h"

namespace Trident
{
    void RenderCommand::Init()
    {
        Startup::GetRenderer().Init();
    }

    void RenderCommand::Shutdown()
    {
        Startup::GetRenderer().Shutdown();
    }

    void RenderCommand::DrawFrame()
    {
        Startup::GetRenderer().DrawFrame();
    }

    void RenderCommand::RecreateSwapchain()
    {
        Startup::GetRenderer().RecreateSwapchain();
    }

    void RenderCommand::SetTransform(const Transform& props)
    {
        Startup::GetRenderer().SetTransform(props);
    }

    void RenderCommand::SetViewport(const ViewportInfo& info)
    {
        Startup::GetRenderer().SetViewport(info);
    }

    void RenderCommand::SetViewportCamera(ECS::Entity cameraEntity)
    {
        // Preserve a clear UI -> RenderCommand -> Renderer flow so viewport textures reflect the chosen camera entity.
        Startup::GetRenderer().SetViewportCamera(cameraEntity);
    }

    void RenderCommand::SetClearColor(const glm::vec4& color)
    {
        // Forward the request so the renderer updates its cached clear colour immediately.
        Startup::GetRenderer().SetClearColor(color);
    }

    Transform RenderCommand::GetTransform()
    {
        return Startup::GetRenderer().GetTransform();
    }

    ViewportInfo RenderCommand::GetViewport()
    {
        return Startup::GetRenderer().GetViewport();
    }

    VkDescriptorSet RenderCommand::GetViewportTexture()
    {
        return Startup::GetRenderer().GetViewportTexture();
    }

    uint32_t RenderCommand::GetCurrentFrame()
    {
        return Startup::GetRenderer().GetCurrentFrame();
    }

    glm::vec4 RenderCommand::GetClearColor()
    {
        // Provide callers with the renderer's clear colour so UI widgets can display the current setting.
        return Startup::GetRenderer().GetClearColor();
    }
}