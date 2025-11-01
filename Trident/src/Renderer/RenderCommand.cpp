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

    void RenderCommand::AppendMeshes(std::vector<Geometry::Mesh> meshes, std::vector<Geometry::Material> materials, std::vector<std::string> textures)
    {
        // Hand the mesh data off to the renderer so it can merge GPU buffers with existing geometry.
        Startup::GetRenderer().AppendMeshes(std::move(meshes), std::move(materials), std::move(textures));
    }

    void RenderCommand::SetEditorCamera(Camera* camera)
    {
        Startup::GetRenderer().SetEditorCamera(camera);
    }

    void RenderCommand::SetRuntimeCamera(Camera* camera)
    {
        // Forward runtime camera ownership so gameplay and editor views can coexist without fighting over transforms.
        Startup::GetRenderer().SetRuntimeCamera(camera);
    }

    void RenderCommand::SetRuntimeCameraReady(bool cameraReady)
    {
        // Allow callers to flag when the runtime camera contains valid scene data so viewports can display helpful guidance.
        Startup::GetRenderer().SetRuntimeCameraReady(cameraReady);
    }

    void RenderCommand::SetActiveRegistry(ECS::Registry* registry)
    {
        // Forward registry swaps so the renderer queries the correct dataset when gathering draw calls.
        Startup::GetRenderer().SetActiveRegistry(registry);
    }

    void RenderCommand::SetViewportCamera(ECS::Entity entity)
    {
        // Tell the renderer which ECS camera currently feeds the viewport so overlays can highlight it.
        Startup::GetRenderer().SetViewportCamera(entity);
    }

    void RenderCommand::SubmitText(uint32_t viewportId, const glm::vec2& position, const glm::vec4& color, std::string_view text)
    {
        // Forward overlay submissions so runtime/editor panels can schedule screen-space labels.
        Startup::GetRenderer().SubmitText(viewportId, position, color, text);
    }

    bool RenderCommand::HasRuntimeCamera()
    {
        // Expose whether a valid, ready runtime camera is bound so panels can surface helpful overlays or fallbacks.
        return Startup::GetRenderer().HasRuntimeCamera();
    }

    bool RenderCommand::IsPerformanceCaptureEnabled()
    {
        // Surface the renderer flag so the editor UI can mirror the active capture state to the user.
        return Startup::GetRenderer().IsPerformanceCaptureEnabled();
    }

    size_t RenderCommand::GetPerformanceCaptureSampleCount()
    {
        // Provide the capture sample count so UI elements can display how much data the session has collected so far.
        return Startup::GetRenderer().GetPerformanceCaptureSampleCount();
    }

    void RenderCommand::SetPerformanceCaptureEnabled(bool enabled)
    {
        // Allow application-level widgets to start or finish a capture session without touching renderer internals.
        Startup::GetRenderer().SetPerformanceCaptureEnabled(enabled);
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
        // Renderer chooses the appropriate camera based on viewport ID; editor (1U) and runtime (2U) remain isolated.
        return Startup::GetRenderer().GetViewportViewMatrix(viewportId);
    }

    glm::mat4 RenderCommand::GetViewportProjectionMatrix(uint32_t viewportId)
    {
        // Gameplay tooling can query either viewport knowing the renderer will select the matching camera feed.
        return Startup::GetRenderer().GetViewportProjectionMatrix(viewportId);
    }

    std::vector<CameraOverlayInstance> RenderCommand::GetCameraOverlayInstances(uint32_t viewportId)
    {
        // Retrieve the projected overlay list so UI code can draw camera icons in screen-space.
        return Startup::GetRenderer().GetCameraOverlayInstances(viewportId);
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

    Renderer::FrameTimingStats RenderCommand::GetFrameTimingStats()
    {
        // Surface the aggregated frame timing data so overlays can present FPS and frame times succinctly.
        return Startup::GetRenderer().GetFrameTimingStats();
    }

    size_t RenderCommand::GetModelCount()
    {
        return Startup::GetRenderer().GetModelCount();
    }


    int32_t RenderCommand::ResolveTextureSlot(const std::string& texturePath)
    {
        // Forward the request to the renderer so tooling can trigger reloads after editing component properties.
        return Startup::GetRenderer().ResolveTextureSlot(texturePath);
    }

    void RenderCommand::SetAIFrameGenerationEnabled(bool enabled)
    {
        // Allow the application layer to toggle expensive inference work as scenes or profiles change.
        Startup::GetRenderer().SetAIFrameGenerationEnabled(enabled);
    }

    bool RenderCommand::IsAIFrameGenerationEnabled()
    {
        // Surface the current enablement flag so UI elements can stay in sync with runtime preferences.
        return Startup::GetRenderer().IsAIFrameGenerationEnabled();
    }

    void RenderCommand::ConfigureAIFrameGeneration(const AIFrameGenerationSettings& settings)
    {
        // Forward configuration down to the renderer so it can load models and choose providers.
        Startup::GetRenderer().ApplyAISettings(settings);
    }

    AIFrameGenerationStatus RenderCommand::GetAIFrameGenerationStatus()
    {
        // Allow diagnostics widgets to inspect provider state without direct renderer dependencies.
        return Startup::GetRenderer().GetAIFrameGenerationStatus();
    }

    bool RenderCommand::HasAIResultTexture()
    {
        // Quickly report whether a finished AI frame is ready before fetching descriptor data.
        return Startup::GetRenderer().HasAIResultTexture();
    }

    bool RenderCommand::TryGetAIResultTexture(VkDescriptorImageInfo& outDescriptor, VkExtent2D& outExtent)
    {
        // Fetch the descriptor/extent pair so overlays can draw the inference result without poking renderer internals.
        return Startup::GetRenderer().TryGetAIResultTexture(outDescriptor, outExtent);
    }

    bool RenderCommand::IsAIFramePending()
    {
        // Let diagnostics widgets show when the AI queue is busy to help explain input/output latency.
        return Startup::GetRenderer().IsAIFramePending();
    }

    double RenderCommand::GetAIExpectedLatencyMilliseconds()
    {
        // Bubble up the measured latency budget so future tuning can keep presentation and inference aligned.
        return Startup::GetRenderer().GetAIExpectedLatencyMilliseconds();
    }

    double RenderCommand::GetAILastInferenceMilliseconds()
    {
        // Expose the most recent inference duration to help spot spikes in model execution.
        return Startup::GetRenderer().GetAILastInferenceMilliseconds();
    }

    double RenderCommand::GetAIQueueLatencyMilliseconds()
    {
        // Provide queue latency so tooling can differentiate between scheduling and execution delays.
        return Startup::GetRenderer().GetAIQueueLatencyMilliseconds();
    }
}