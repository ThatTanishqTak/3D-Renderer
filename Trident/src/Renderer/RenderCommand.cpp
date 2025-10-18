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

    void RenderCommand::SetRuntimeCamera(Camera* camera)
    {
        Startup::GetRenderer().SetRuntimeCamera(camera);
    }

    void RenderCommand::SetRuntimeCameraActive(bool active)
    {
        Startup::GetRenderer().SetRuntimeCameraActive(active);
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

    glm::mat4 RenderCommand::GetViewportViewMatrix()
    {
        return Startup::GetRenderer().GetViewportViewMatrix();
    }

    glm::mat4 RenderCommand::GetViewportProjectionMatrix()
    {
        return Startup::GetRenderer().GetViewportProjectionMatrix();
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