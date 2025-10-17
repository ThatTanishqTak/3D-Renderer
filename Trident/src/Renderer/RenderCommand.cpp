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

    void RenderCommand::SetViewportProjection(ProjectionType projection, float orthographicSize)
    {
        // Forward the requested projection so the renderer's editor camera mirrors UI state immediately.
        Startup::GetRenderer().SetViewportProjection(projection, orthographicSize);
    }

    void RenderCommand::UpdateEditorCamera(const glm::vec3& position, float yawDegrees, float pitchDegrees, float fieldOfViewDegrees)
    {
        // Copy the provided transform into the renderer's built-in editor camera so gizmos and viewports stay aligned.
        Renderer& l_Renderer = Startup::GetRenderer();
        Camera& l_Camera = l_Renderer.GetCamera();
        l_Camera.SetPosition(position);
        l_Camera.SetYaw(yawDegrees);
        l_Camera.SetPitch(pitchDegrees);
        l_Camera.SetFOV(fieldOfViewDegrees);
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