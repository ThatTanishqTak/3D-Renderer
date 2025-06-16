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

    void RenderCommand::SetCubeProperties(const CubeProperties& props)
    {
        Application::GetRenderer().SetCubeProperties(props);
    }

    void RenderCommand::SetViewport(const ViewportInfo& info)
    {
        Application::GetRenderer().SetViewport(info);
    }

    CubeProperties RenderCommand::GetCubeProperties()
    {
        return Application::GetRenderer().GetCubeProperties();
    }

    ViewportInfo RenderCommand::GetViewport()
    {
        return Application::GetRenderer().GetViewport();
    }

    uint32_t RenderCommand::GetCurrentFrame()
    {
        return Application::GetRenderer().GetCurrentFrame();
    }
}