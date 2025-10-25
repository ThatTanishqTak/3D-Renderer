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
        static void SetViewport(uint32_t viewportId, const ViewportInfo& info);
        // Allow tooling to update the renderer's selected entity so gizmos operate on the expected transform.
        static void SetSelectedEntity(ECS::Entity entity);
        // Mirror Renderer::SetClearColor so editor widgets can adjust the background tone live.
        static void SetClearColor(const glm::vec4& color);
        static void AppendMeshes(std::vector<Geometry::Mesh> meshes, std::vector<Geometry::Material> materials);
        static void SetEditorCamera(Camera* camera);
        static void SetRuntimeCamera(Camera* camera);
        static bool HasRuntimeCamera();

        static Transform GetTransform();
        static ViewportInfo GetViewport();
        static VkDescriptorSet GetViewportTexture(uint32_t viewportId);
        static glm::mat4 GetViewportViewMatrix(uint32_t viewportId);
        static glm::mat4 GetViewportProjectionMatrix(uint32_t viewportId);
        static size_t GetCurrentFrame();
        // Expose the active clear colour so UI panels can stay in sync with renderer preferences.
        static glm::vec4 GetClearColor();
        static size_t GetModelCount();
    };
}