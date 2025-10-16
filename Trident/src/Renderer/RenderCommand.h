#pragma once

#include "Renderer.h"

#include "ECS/Components/TransformComponent.h"

namespace Trident
{
    class RenderCommand
    {
    public:
        static void Init();
        static void Shutdown();
        static void DrawFrame();
        static void RecreateSwapchain();

        static void SetTransform(const Transform& props);
        static void SetViewport(const ViewportInfo& info);
        static void SetViewportCamera(ECS::Entity cameraEntity);
        // Allow tooling to update the renderer's selected entity so gizmos operate on the expected transform.
        static void SetSelectedEntity(ECS::Entity entity);
        // Mirror Renderer::SetClearColor so editor widgets can adjust the background tone live.
        static void SetClearColor(const glm::vec4& color);
        static void AppendMeshes(std::vector<Geometry::Mesh> meshes, std::vector<Geometry::Material> materials);

        static Transform GetTransform();
        static ViewportInfo GetViewport();
        static VkDescriptorSet GetViewportTexture();
        static glm::mat4 GetViewportViewMatrix();
        static glm::mat4 GetViewportProjectionMatrix();
        static size_t GetCurrentFrame();
        // Expose the active clear colour so UI panels can stay in sync with renderer preferences.
        static glm::vec4 GetClearColor();
        static size_t GetModelCount();
    };
}