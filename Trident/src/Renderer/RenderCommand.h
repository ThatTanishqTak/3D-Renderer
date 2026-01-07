#pragma once

#include "Renderer.h"

#include <string_view>

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
        static void AppendMeshes(std::vector<Geometry::Mesh> meshes, std::vector<Geometry::Material> materials, std::vector<std::string> textures);
        static void SetEditorCamera(Camera* camera);
        static void SetRuntimeCamera(Camera* camera);
        static void SetRuntimeCameraReady(bool cameraReady);
        static void SetActiveRegistry(ECS::Registry* registry);
        static void SetViewportCamera(ECS::Entity entity);
        static void SubmitText(uint32_t viewportId, const glm::vec2& position, const glm::vec4& color, std::string_view text);
        static bool HasRuntimeCamera();

        // Allow tooling to query whether a capture session is currently active for status displays.
        static bool IsPerformanceCaptureEnabled();
        // Surface the current capture sample count so UI overlays can visualise progress to artists.
        static size_t GetPerformanceCaptureSampleCount();
        // Toggle the renderer's capture mode so the application layer can start or end sessions.
        static void SetPerformanceCaptureEnabled(bool enabled);

        static Transform GetTransform();
        static glm::mat4 GetWorldTransform(ECS::Entity entity);
        static void SetWorldTransform(ECS::Entity entity, const glm::mat4& worldTransform);
        static ViewportInfo GetViewport();
        static VkDescriptorSet GetViewportTexture(uint32_t viewportId);
        static glm::mat4 GetViewportViewMatrix(uint32_t viewportId);
        static glm::mat4 GetViewportProjectionMatrix(uint32_t viewportId);
        static glm::mat4 GetEditorCameraViewMatrix();
        static glm::mat4 GetEditorCameraProjectionMatrix();
        static std::vector<CameraOverlayInstance> GetCameraOverlayInstances(uint32_t viewportId);
        static size_t GetCurrentFrame();
        // Expose the active clear colour so UI panels can stay in sync with renderer preferences.
        static glm::vec4 GetClearColor();
        // Provide averaged timing statistics so editor overlays can surface FPS without touching renderer internals.
        static Renderer::FrameTimingStats GetFrameTimingStats();
        static size_t GetModelCount();
        // Allow editor tooling to resolve texture slots on demand when authors request explicit reloads.
        static int32_t ResolveTextureSlot(const std::string& texturePath);
        // Provide mesh indices for primitives so authoring actions can spawn immediately renderable shapes.
        static size_t GetOrCreatePrimitiveMeshIndex(MeshComponent::PrimitiveType primitiveType);
        // Mirror the AI debug stats so UI panels can surface runtime information without poking renderer internals.
        static Renderer::AiDebugStats GetAiDebugStats();
        // Allow tooling to adjust the AI blend strength without reaching into the renderer singleton directly.
        static void SetAiBlendStrength(float blendStrength);
        static float GetAiBlendStrength();
        // Placeholder hook for future UI that will surface the AI texture preview.
        static ImTextureID GetAiTextureDescriptor();
        // Toggle dataset capture at runtime without relying on environment variables.
        static void SetFrameDatasetCaptureEnabled(bool enabled);
        // Surface the current dataset capture flag for UI panels.
        static bool IsFrameDatasetCaptureEnabled();
        // Update the dataset capture directory from configuration panels.
        static void SetFrameDatasetCaptureDirectory(const std::filesystem::path& directory);
        static std::filesystem::path GetFrameDatasetCaptureDirectory();
        // Adjust how frequently frames are captured to reduce I/O pressure.
        static void SetFrameDatasetCaptureInterval(uint32_t interval);
        static uint32_t GetFrameDatasetCaptureInterval();
        // Toggle viewport recording so UI panels can export animation clips.
        static bool SetViewportRecordingEnabled(bool enabled, uint32_t viewportId, VkExtent2D extent, const std::filesystem::path& outputPath);
        // Submit the latest frame to the recording path when readback completes.
        static void SubmitViewportFrame(uint32_t imageIndex, std::chrono::system_clock::time_point captureTimestamp);
        static bool IsViewportRecording();
        static const std::vector<VideoEncoder::RecordedFrame>& GetViewportFrameBuffer();
    };
}