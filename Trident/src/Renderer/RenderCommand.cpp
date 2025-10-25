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

    void RenderCommand::SetViewport(uint32_t viewportId, const ViewportInfo& info)
    {
        Startup::GetRenderer().SetViewport(viewportId, info);
    }

    void RenderCommand::SetSelectedEntity(ECS::Entity entity)
    {
        // Forward the selection so the renderer tracks the same entity as the editor panels when drawing gizmos.
        Startup::GetRenderer().SetSelectedEntity(entity);
    }

    void RenderCommand::SetClearColor(const glm::vec4& color)
    {
        // Forward the request so the renderer updates its cached clear colour immediately.
        Startup::GetRenderer().SetClearColor(color);
    }

    void RenderCommand::AppendMeshes(std::vector<Geometry::Mesh> meshes, std::vector<Geometry::Material> materials)
    {
        // Hand the mesh data off to the renderer so it can merge GPU buffers with existing geometry.
        Startup::GetRenderer().AppendMeshes(std::move(meshes), std::move(materials));
    }

    void RenderCommand::SetEditorCamera(Camera* camera)
    {
        Startup::GetRenderer().SetEditorCamera(camera);
    }

    Transform RenderCommand::GetTransform()
    {
        return Startup::GetRenderer().GetTransform();
    }

    ViewportInfo RenderCommand::GetViewport()
    {
        return Startup::GetRenderer().GetViewport();
    }

    VkDescriptorSet RenderCommand::GetViewportTexture(uint32_t viewportId)
    {
        return Startup::GetRenderer().GetViewportTexture(viewportId);
    }

    glm::mat4 RenderCommand::GetViewportViewMatrix(uint32_t viewportId)
    {
        return Startup::GetRenderer().GetViewportViewMatrix(viewportId);
    }

    glm::mat4 RenderCommand::GetViewportProjectionMatrix(uint32_t viewportId)
    {
        return Startup::GetRenderer().GetViewportProjectionMatrix(viewportId);
    }

    size_t RenderCommand::GetCurrentFrame()
    {
        return Startup::GetRenderer().GetCurrentFrame();
    }

    glm::vec4 RenderCommand::GetClearColor()
    {
        // Provide callers with the renderer's clear colour so UI widgets can display the current setting.
        return Startup::GetRenderer().GetClearColor();
    }

    size_t RenderCommand::GetModelCount()
    {
        return Startup::GetRenderer().GetModelCount();
    }
}