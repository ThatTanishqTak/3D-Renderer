#pragma once

#include "Core/Utilities.h"

#include "Renderer/RenderData.h"
#include "Renderer/Camera/Camera.h"
#include "Renderer/Vertex.h"
#include "Renderer/UniformBuffer.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Pipeline.h"
#include "Renderer/Buffers.h"
#include "Renderer/Commands.h"
#include "Renderer/Skybox.h"
#include "Renderer/TextRenderer.h"
#include "Renderer/VideoEncoder.h"
#include "AI/FrameGenerator.h"
#include "AI/FrameDatasetRecorder.h"

#include "Geometry/Mesh.h"
#include "Geometry/Material.h"
#include "Loader/TextureLoader.h"

#include "ECS/Entity.h"
#include "ECS/Components/MeshComponent.h"
#include "ECS/Components/SpriteComponent.h"
#include "ECS/Components/TransformComponent.h"
#include "ECS/Components/LightComponent.h"
#include "ECS/Components/TextureComponent.h"
#include "ECS/Components/AnimationComponent.h"

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <imgui.h>

#include <vector>
#include <array>
#include <memory>
#include <functional>
#include <chrono>
#include <unordered_map>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <filesystem>

namespace Trident
{
    namespace UI { class ImGuiLayer; }
    namespace ECS { class Registry; }

    struct ViewportInfo
    {
        uint32_t ViewportID = 0;
        glm::vec2 Position{ 0.0f };
        glm::vec2 Size{ 0.0f };
    };

    /**
     * @brief Describes a projected camera overlay used by viewport UI layers.
     *
     * Each instance stores the screen-space location of an entity driven camera along
     * with metadata that allows tooling to highlight primary or active viewpoints.
     */
    struct CameraOverlayInstance
    {
        ECS::Entity m_Entity = std::numeric_limits<ECS::Entity>::max(); // Entity owning the camera component.
        glm::vec2 m_ScreenPosition{ 0.0f, 0.0f };                     // Position inside the viewport in pixels.
        float m_Depth = 1.0f;                                         // Normalised device depth used for front-to-back sorting.
        bool m_IsPrimary = false;                                     // True when the camera component is flagged as primary.
        bool m_IsViewportCamera = false;                              // True when the camera currently drives the viewport.
        std::array<glm::vec2, 4> m_FrustumCorners{};                  // Screen-space quad describing the preview frustum.
        std::array<bool, 4> m_FrustumCornerVisible{};                 // Flags indicating which projected corners remain onscreen.
        bool m_HasFrustum = false;                                    // Cached visibility state for overlay rendering code.
    };

    class Renderer
    {
    public:
        // Stores a single frame timing measurement captured from the render loop.
        struct FrameTimingSample
        {
            double FrameMilliseconds = 0.0;
            double FramesPerSecond = 0.0;
            VkExtent2D Extent{ 0, 0 };
            std::chrono::system_clock::time_point CaptureTime{};
        };

        // Aggregated statistics calculated from the ring buffer for quick heads-up display consumption.
        struct FrameTimingStats
        {
            double AverageMilliseconds = 0.0;
            double MinimumMilliseconds = 0.0;
            double MaximumMilliseconds = 0.0;
            double AverageFPS = 0.0;
        };

        // Surface AI pipeline metrics so editor tooling can reason about queue depth and timing behaviour.
        struct AiDebugStats
        {
            bool m_ModelInitialised = false;                 // True when the frame generator has successfully loaded a model.
            size_t m_PendingJobCount = 0;                    // Number of frames sitting in the queue awaiting inference.
            uint64_t m_CompletedInferenceCount = 0;          // Total number of jobs that produced an output tensor.
            double m_LastInferenceMilliseconds = 0.0;        // Duration of the most recent inference in milliseconds.
            double m_AverageInferenceMilliseconds = 0.0;     // Average duration across all completed runs.
            bool m_TextureReady = false;                     // Signals whether the AI texture is bound for sampling.
            float m_BlendStrength = 0.0f;                    // Current blend factor applied during compositing.
            VkExtent2D m_TextureExtent{ 0, 0 };              // Resolution of the uploaded AI texture.
            double m_ReservedMetric = 0.0;                   // Placeholder slot for future statistics without breaking ABI.
        };

        Renderer();
        ~Renderer();

        void Init();
        void Shutdown();
        void DrawFrame();

        void RecreateSwapchain();
        void UploadMesh(const std::vector<Geometry::Mesh>& meshes, const std::vector<Geometry::Material>& materials, const std::vector<std::string>& textures);
        void AppendMeshes(std::vector<Geometry::Mesh> meshes, std::vector<Geometry::Material> materials, std::vector<std::string> textures);
        void UploadTexture(const std::string& texturePath, const Loader::TextureData& texture);
        void SetImGuiLayer(UI::ImGuiLayer* layer);
        void SetEditorCamera(Camera* camera);
        void SetRuntimeCamera(Camera* camera);
        void SetRuntimeCameraReady(bool ready);
        void SetActiveRegistry(ECS::Registry* registry);
        bool HasRuntimeCamera() const { return m_RuntimeCamera != nullptr && m_RuntimeCameraReady; }

        /**
         * @brief Control whether AI dataset capture runs during rendering.
         */
        void SetFrameDatasetCaptureEnabled(bool enabled);

        /**
         * @brief Update the folder that stores captured dataset artefacts.
         */
        void SetFrameDatasetCaptureDirectory(const std::filesystem::path& directory);

        /**
         * @brief Inspect the currently configured dataset capture directory.
         */
        std::filesystem::path GetFrameDatasetCaptureDirectory() const { return m_FrameDatasetCaptureDirectory; }

        /**
         * @brief Adjust how frequently frames are sampled for dataset capture.
         */
        void SetFrameDatasetCaptureInterval(uint32_t interval);

        /**
         * @brief Query the sampling interval currently applied to dataset capture.
         */
        uint32_t GetFrameDatasetCaptureInterval() const { return m_FrameDatasetCaptureInterval; }

        /**
         * @brief Expose whether dataset capture is currently active.
         */
        bool IsFrameDatasetCaptureEnabled() const { return m_FrameDatasetCaptureEnabled; }

        /**
         * @brief Draw an ImGui panel that exposes dataset capture configuration.
         */
        void DrawDatasetCaptureUI();

        // Resolve a texture path to a renderer-managed slot, loading GPU resources and updating descriptor bindings
        // when necessary. This keeps editor tooling responsive when authors tweak materials.
        int32_t ResolveTextureSlot(const std::string& texturePath);

        // Lightweight wrapper describing an ImGui-ready texture along with the Vulkan
        // resources required to keep it alive for the duration of the renderer.
        struct ImGuiTexture
        {
            VkImage m_Image = VK_NULL_HANDLE;              // GPU image storing the texels.
            VkDeviceMemory m_ImageMemory = VK_NULL_HANDLE; // Device memory bound to the image.
            VkImageView m_ImageView = VK_NULL_HANDLE;      // View consumed by ImGui shaders.
            VkSampler m_Sampler = VK_NULL_HANDLE;          // Sampler describing filtering and addressing.
            ImTextureID m_Descriptor = 0;                  // Descriptor passed directly to ImGui::Image.
            VkExtent2D m_Extent{ 0, 0 };                   // Dimensions used for sizing the widget.
        };

        // Creates a texture that can be consumed by ImGui widgets. The renderer keeps
        // ownership of the Vulkan resources so UI code can focus on presentation.
        ImGuiTexture* CreateImGuiTexture(const Loader::TextureData& texture);
        // Releases a previously created ImGui texture. UI systems rarely need to call
        // this directly because the renderer clears any registered textures on shutdown.
        void DestroyImGuiTexture(ImGuiTexture& texture);

        // Allow external systems to adjust the render target clear colour while keeping editor and runtime in sync.
        void SetClearColor(const glm::vec4& color);
        // Returns the colour applied when clearing render targets so UI layers can present the current state.
        glm::vec4 GetClearColor() const { return m_ClearColor; }

        size_t GetCurrentFrame() const { return m_Commands.CurrentFrame(); }
        size_t GetLastFrameAllocationCount() const { return m_FrameAllocationCount; }
        size_t GetModelCount() const { return m_ModelCount; }
        size_t GetTriangleCount() const { return m_TriangleCount; }
        const FrameTimingStats& GetFrameTimingStats() const { return m_PerformanceStats; }
        size_t GetFrameTimingHistoryCount() const { return m_PerformanceSampleCount; }
        const std::vector<FrameTimingSample>& GetFrameTimingHistory() const { return m_PerformanceHistory; }
        size_t GetPerformanceCaptureSampleCount() const { return m_PerformanceCaptureBuffer.size(); }
        bool IsPerformanceCaptureEnabled() const { return m_PerformanceCaptureEnabled; }
        void SetPerformanceCaptureEnabled(bool enabled);

        /**
         * @brief Toggle viewport recording so panels can export rendered frames.
         */
        bool SetViewportRecordingEnabled(bool enabled, uint32_t viewportId, VkExtent2D extent, const std::filesystem::path& outputPath);

        /**
         * @brief Submit the latest readback buffer as a recorded frame when available.
         */
        void SubmitViewportFrame(uint32_t imageIndex, std::chrono::system_clock::time_point captureTimestamp);

        /**
         * @brief Query whether the renderer is actively recording viewport frames.
         */
        bool IsViewportRecording() const { return m_ViewportRecordingEnabled; }

        /**
         * @brief Inspect buffered viewport frames so tooling can visualise progress.
         */
        const std::vector<VideoEncoder::RecordedFrame>& GetViewportFrameBuffer() const { return m_ViewportFrameBuffer; }

        void SetTransform(const Transform& props);
        void SetViewport(uint32_t viewportId, const ViewportInfo& info);
        // Cache the inspector's selection so gizmo transforms lock onto the same entity as the editor UI.
        void SetSelectedEntity(ECS::Entity entity);
        // Track which ECS camera currently drives the viewport so overlays can highlight it for designers.
        void SetViewportCamera(ECS::Entity entity);
        // Allow callers to submit screen-space text that will be composited after the main scene pass.
        void SubmitText(uint32_t viewportId, const glm::vec2& position, const glm::vec4& color, std::string_view text);

        Transform GetTransform() const;
        ViewportInfo GetViewport() const;
        VkDescriptorSet GetViewportTexture(uint32_t viewportId) const;
        ECS::Entity GetViewportCamera() const { return m_ViewportCamera; }

        // Provide tooling with the matrices required for gizmo overlay composition.
        glm::mat4 GetViewportViewMatrix(uint32_t viewportId) const;
        glm::mat4 GetViewportProjectionMatrix(uint32_t viewportId) const;
        // Gather the projected positions of ECS cameras so UI overlays can render icons in 2D space.
        std::vector<CameraOverlayInstance> GetCameraOverlayInstances(uint32_t viewportId) const;
        const Camera* GetActiveCamera() const;

        // Access to the CPU-side material cache so editor widgets can tweak shading values.
        std::vector<Geometry::Material>& GetMaterials() { return m_Materials; }
        const std::vector<Geometry::Material>& GetMaterials() const { return m_Materials; }

        VkRenderPass GetRenderPass() const { return m_Pipeline.GetRenderPass(); }
        uint32_t GetImageCount() const { return m_Swapchain.GetImageCount(); }

        VkDescriptorSetLayout GetDescriptorSetLayout() const { return m_Pipeline.GetDescriptorSetLayout(); }
        VkCommandPool GetCommandPool() const { return m_Commands.GetCommandPool(); }
        std::vector<VkCommandBuffer> GetCommandBuffer() const { return m_Commands.GetCommandBuffers(); }

        /**
         * @brief Return cached debug statistics describing the AI frame generator's current state.
         */
        AiDebugStats GetAiDebugStats() const { return m_AiDebugStats; }

        /**
         * @brief Set the strength used when blending AI generated pixels with the rasterised frame.
         *
         * Values outside the [0,1] range are clamped so caller mistakes do not produce undefined blends.
         */
        void SetAiBlendStrength(float blendStrength);

        /**
         * @brief Query the blend strength applied when compositing AI pixels.
         */
        float GetAiBlendStrength() const { return m_AiBlendStrength; }

        /**
         * @brief Placeholder for tooling that wishes to display the AI texture in UI panels.
         *
         * The renderer does not yet register an ImGui descriptor for the AI output. The stub keeps the plumbing ready so a
         * future change can wire the descriptor pool without touching the rest of the renderer again.
         */
        ImTextureID GetAiTextureDescriptor() const;

        /**
         * @brief Query whether the renderer has already completed shutdown.
         */
        bool IsShutdown() const { return m_Shutdown; }

        bool m_Shutdown = false;

    private:
        static constexpr uint32_t s_MaxBonesPerSkeleton = 128; // Enough for Mixamo rigs with headroom for future assets.

        struct MeshDrawInfo
        {
            uint32_t m_FirstIndex = 0;            // First index in the shared buffer for the mesh.
            uint32_t m_IndexCount = 0;            // Number of indices the draw call should submit.
            int32_t m_BaseVertex = 0;             // Base vertex offset applied during drawing.
            int32_t m_MaterialIndex = -1;         // Material resolved at upload time.
        };

        struct MeshDrawCommand
        {
            glm::mat4 m_ModelMatrix{ 1.0f };      // Cached transform ready for the GPU.
            const MeshComponent* m_Component = nullptr; // Source component describing the draw parameters.
            const TextureComponent* m_TextureComponent = nullptr; // Optional texture binding supplied by the entity.
            const AnimationComponent* m_AnimationComponent = nullptr; // Optional animation data driving skinning.
            uint32_t m_BoneOffset = 0;            // Offset into the bone palette buffer assigned during batching.
            uint32_t m_BoneCount = 0;             // Number of matrices contributing to this palette.
            ECS::Entity m_Entity = 0;             // Owning entity for debugging and picking hooks.
        };

        struct SpriteDrawCommand
        {
            glm::mat4 m_ModelMatrix{ 1.0f };        // Cached transform ready for GPU submission.
            const SpriteComponent* m_Component = nullptr; // Pointer into ECS storage for sprite properties.
            const TextureComponent* m_TextureComponent = nullptr; // Optional texture binding supplied by the entity.
            ECS::Entity m_Entity = 0;               // Owning entity for debugging and future sorting.
        };

        struct TextSubmission
        {
            uint32_t m_ViewportId = 0; // Target viewport that should receive the overlay text.
            glm::vec2 m_Position{ 0.0f }; // Top-left text anchor in viewport pixels.
            glm::vec4 m_Color{ 1.0f };   // RGBA tint applied to every glyph.
            std::string m_Text;          // UTF-8 encoded message queued for rendering.
        };

        void GatherMeshDraws();
        void BuildSpriteGeometry();
        void DestroySpriteGeometry();
        void GatherSpriteDraws();
        void DrawSprites(VkCommandBuffer commandBuffer, uint32_t imageIndex);
        void EnsureSkinningBufferCapacity(size_t requiredMatrices);
        void RefreshBonePaletteDescriptors();
        void PrepareBonePaletteBuffer(uint32_t imageIndex);

        // Swapchain
        Swapchain m_Swapchain;

        // Cache the swapchain image layouts so we can transition from the correct state each frame and keep validation quiet.
        std::vector<VkImageLayout> m_SwapchainImageLayouts;

        // Buffers
        VkBuffer m_VertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_VertexBufferMemory = VK_NULL_HANDLE;
        VkBuffer m_IndexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_IndexBufferMemory = VK_NULL_HANDLE;
        uint32_t m_IndexCount = 0;

        std::vector<VkBuffer> m_BonePaletteBuffers;             // Storage buffers holding per-draw skinning palettes.
        std::vector<VkDeviceMemory> m_BonePaletteMemory;        // Device memory backing the bone palette buffers.
        VkDeviceSize m_BonePaletteBufferSize = 0;               // Size in bytes of each bone palette buffer.
        size_t m_BonePaletteMatrixCapacity = 0;                 // Number of matrices allocated per swapchain image.
        std::vector<glm::mat4> m_BonePaletteScratch;            // CPU staging area populated before uploading to the GPU.

        // Pipeline
        Pipeline m_Pipeline;

        // Command pool, buffers and sync objects
        Commands m_Commands;
        VkFence m_ResourceFence = VK_NULL_HANDLE;

        // Descriptor sets & uniform buffers
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_DescriptorSets;
        std::vector<VkBuffer> m_GlobalUniformBuffers;
        std::vector<VkDeviceMemory> m_GlobalUniformBuffersMemory;
        std::vector<VkBuffer> m_MaterialBuffers;               // Per-frame GPU-visible cache of material records.
        std::vector<VkDeviceMemory> m_MaterialBuffersMemory;    // Backing memory for the material storage buffers.
        std::vector<bool> m_MaterialBufferDirty;                // Tracks which per-frame material uploads still need refreshing.
        size_t m_MaterialBufferElementCount = 0;                // Number of MaterialUniformBuffer records resident on the GPU.
        struct TextureSlot
        {
            VkImage m_Image = VK_NULL_HANDLE;                    // Backing image containing the texture pixels.
            VkDeviceMemory m_Memory = VK_NULL_HANDLE;            // Device memory bound to the image.
            VkImageView m_View = VK_NULL_HANDLE;                 // View used for sampling.
            VkSampler m_Sampler = VK_NULL_HANDLE;                // Sampler describing filtering/wrapping.
            VkDescriptorImageInfo m_Descriptor{};                // Cached descriptor info for descriptor writes.
            std::string m_SourcePath{};                          // Normalized path of the source asset.
        };

        std::vector<TextureSlot> m_TextureSlots;                 // GPU texture slots shared across materials.
        std::unordered_map<std::string, uint32_t> m_TextureSlotLookup; // Maps normalized texture paths to slot indices.
        std::vector<VkDescriptorImageInfo> m_TextureDescriptorCache;   // Scratch buffer used when updating descriptor arrays.
        VkBuffer m_SpriteVertexBuffer = VK_NULL_HANDLE;      // Shared quad geometry for batched sprites.
        VkDeviceMemory m_SpriteVertexMemory = VK_NULL_HANDLE;// Memory backing the sprite vertex buffer.
        VkBuffer m_SpriteIndexBuffer = VK_NULL_HANDLE;       // Index buffer referencing the shared quad.
        VkDeviceMemory m_SpriteIndexMemory = VK_NULL_HANDLE; // Memory backing the sprite index buffer.
        uint32_t m_SpriteIndexCount = 0;                    // Number of indices issued per sprite draw.
        std::vector<MeshDrawInfo> m_MeshDrawInfo;           // Cached draw metadata for each uploaded mesh.
        std::vector<MeshDrawCommand> m_MeshDrawCommands;    // Mesh draw list gathered per-frame from the ECS registry.
        std::vector<Geometry::Mesh> m_GeometryCache;        // CPU-side copy of uploaded meshes for incremental rebuilds.

        // Storage for ImGui textures (such as file icons) so their Vulkan resources
        // remain valid until the renderer explicitly destroys them.
        std::vector<std::unique_ptr<ImGuiTexture>> m_ImGuiTexturePool;

        struct OffscreenTarget
        {
            // Vulkan handles owned by the renderer; lifetime is managed explicitly via DestroyOffscreenResources.
            VkImage m_Image = VK_NULL_HANDLE;
            VkDeviceMemory m_Memory = VK_NULL_HANDLE;
            VkImageView m_ImageView = VK_NULL_HANDLE;
            VkImage m_DepthImage = VK_NULL_HANDLE;
            VkDeviceMemory m_DepthMemory = VK_NULL_HANDLE;
            VkImageView m_DepthView = VK_NULL_HANDLE;
            VkFramebuffer m_Framebuffer = VK_NULL_HANDLE;
            VkDescriptorSet m_TextureID = VK_NULL_HANDLE;
            VkSampler m_Sampler = VK_NULL_HANDLE;
            VkExtent2D m_Extent{ 0, 0 };
            VkImageLayout m_CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            VkImageLayout m_DepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        };

        // Offscreen rendering resources keyed by viewport identifier so multiple panels can co-exist.
        struct ViewportContext
        {
            ViewportInfo m_Info{};                     // Latest position/size reported by the owning panel.
            VkExtent2D m_CachedExtent{ 0, 0 };         // Cached Vulkan extent used to avoid redundant resizes.
            OffscreenTarget m_Target{};                // Offscreen render target backing the viewport.
        };

        std::unordered_map<uint32_t, ViewportContext> m_ViewportContexts;
        uint32_t m_ActiveViewportId = 0;
        static constexpr uint32_t s_InvalidViewportId = std::numeric_limits<uint32_t>::max();
        ECS::Entity m_ViewportCamera = std::numeric_limits<ECS::Entity>::max();

        Buffers m_Buffers;

        TextRenderer m_TextRenderer;
        std::unordered_map<uint32_t, std::vector<TextSubmission>> m_TextSubmissionQueue; // Per-viewport text queued this frame.

        size_t m_MaxVertexCount = 0;
        size_t m_MaxIndexCount = 0;
        std::unique_ptr<Vertex[]> m_StagingVertices;
        std::unique_ptr<uint32_t[]> m_StagingIndices;
        std::vector<Geometry::Material> m_Materials; // CPU copy of the material table used during shading
        std::vector<SpriteDrawCommand> m_SpriteDrawList;    // Cached list of sprites visible for the current frame.

        ECS::Entity m_Entity = 0;
        ECS::Registry* m_Registry = nullptr;
        Skybox m_Skybox{};
        VkImage m_SkyboxTextureImage = VK_NULL_HANDLE;
        VkDeviceMemory m_SkyboxTextureImageMemory = VK_NULL_HANDLE;
        VkImageView m_SkyboxTextureView = VK_NULL_HANDLE;
        VkSampler m_SkyboxTextureSampler = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_SkyboxDescriptorSets;

        UI::ImGuiLayer* m_ImGuiLayer = nullptr;
        Camera* m_EditorCamera = nullptr;          // Camera used while authoring scenes in the viewport.
        Camera* m_RuntimeCamera = nullptr;         // Camera representing runtime/gameplay output routed to the game viewport.
        bool m_RuntimeCameraReady = false;         // Tracks whether the runtime camera currently points at a valid scene entity.
        size_t m_FrameAllocationCount = 0;

        size_t m_ModelCount = 0;
        size_t m_TriangleCount = 0;

        static constexpr uint32_t s_MaxPointLights = kMaxPointLights; // Mirror uniform buffer light budget.
        static constexpr glm::vec3 s_DefaultDirectionalDirection{ -0.5f, -1.0f, -0.3f }; // Fallback sun direction.
        static constexpr glm::vec3 s_DefaultDirectionalColor{ 1.0f, 0.98f, 0.92f }; // Warm sunlight tint.
        static constexpr float s_DefaultDirectionalIntensity = 5.0f; // Brightness used when no lights exist.
        glm::vec3 m_AmbientColor{ 0.03f };  // Ambient tint simulating image-based lighting
        float m_AmbientIntensity = 1.0f;     // Scalar multiplier for ambient contribution
        glm::vec4 m_ClearColor{ 0.005f, 0.005f, 0.005f, 1.0f }; // Default background colour used for both offscreen and swapchain clears

        // Performance metrics
        static constexpr size_t s_PerformanceHistorySize = 240;
        std::vector<FrameTimingSample> m_PerformanceHistory;
        size_t m_PerformanceHistoryNextIndex = 0;
        size_t m_PerformanceSampleCount = 0;
        FrameTimingStats m_PerformanceStats{};
        bool m_PerformanceCaptureEnabled = false;
        std::vector<FrameTimingSample> m_PerformanceCaptureBuffer;
        std::chrono::system_clock::time_point m_PerformanceCaptureStartTime{};
        std::vector<VkImageLayout> m_SwapchainDepthLayouts;

        AI::FrameGenerator m_FrameGenerator;                   // Helper that owns the ONNX runtime bindings.
        std::vector<float> m_AiInterpolationBuffer;            // Latest AI output available for dependent passes.
        std::vector<float> m_PendingFrameReadback;             // Staging buffer populated once GPU readback hooks are ready.
        std::vector<VkBuffer> m_FrameReadbackBuffers;          // CPU-visible buffers receiving colour copies per swapchain image.
        std::vector<VkDeviceMemory> m_FrameReadbackMemory;     // Host-visible allocations backing the staging buffers.
        std::vector<bool> m_FrameReadbackPending;              // Flags indicating which buffers contain fresh GPU data.
        VkExtent2D m_FrameReadbackExtent{ 0, 0 };              // Cached extent used to validate copy/readback paths.
        VkExtent2D m_LastReadbackExtent{ 0, 0 };               // Tracks the last requested readback extent to avoid redundant resizes.
        VkExtent2D m_PendingReadbackExtent{ 0, 0 };            // Extent staged for the next resize operation.
        bool m_ReadbackResizePending = false;                  // Signals that readback resources should be rebuilt when safe.
        VkDeviceSize m_FrameReadbackBufferSize = 0;            // Expected byte size for each staging buffer.
        uint32_t m_FrameReadbackBytesPerPixel = 0;             // Cached pixel stride derived from the swapchain format.
        uint32_t m_FrameReadbackChannelCount = 0;              // Number of colour channels copied into the staging buffer.
        std::array<uint32_t, 4> m_FrameReadbackChannelMapping{ { 0, 1, 2, 3 } }; // Mapping from source format into RGBA order.
        bool m_ReadbackConfigurationWarningIssued = false;     // Tracks whether configuration warnings have already been issued.
        VkImage m_AiTextureImage = VK_NULL_HANDLE;             // GPU image sampling the AI generated frame.
        VkDeviceMemory m_AiTextureMemory = VK_NULL_HANDLE;     // Device local memory backing the AI texture.
        VkImageView m_AiTextureView = VK_NULL_HANDLE;          // View bound to descriptor sets for sampling.
        VkSampler m_AiTextureSampler = VK_NULL_HANDLE;         // Sampler used when shading blends the AI output.
        VkImageLayout m_AiTextureLayout = VK_IMAGE_LAYOUT_UNDEFINED; // Cached layout used when transitioning the AI texture.
        VkExtent2D m_AiTextureExtent{ 0, 0 };                  // Resolution of the GPU AI texture for descriptor updates.
        VkBuffer m_AiUploadBuffer = VK_NULL_HANDLE;            // Host-visible staging buffer for AI uploads.
        VkDeviceMemory m_AiUploadMemory = VK_NULL_HANDLE;      // Memory backing the staging buffer.
        VkDeviceSize m_AiUploadBufferSize = 0;                 // Size of the host-visible staging allocation.
        bool m_AiTextureDirty = false;                         // Signals that a fresh AI frame should be uploaded to the GPU.
        bool m_AiTextureReady = false;                         // Tracks whether the descriptor can point at the GPU texture.
        float m_AiBlendStrength = 0.35f;                       // Blend factor used to mix the AI and rasterised frames.
        AiDebugStats m_AiDebugStats{};                         // Snapshot of AI metrics surfaced to debug tooling.
        bool m_AiInputLayoutVerified = false;                  // Ensures tensor layout validation logs only trigger once.
        bool m_AiOutputLayoutVerified = false;                 // Ensures output layout validation does not spam the console.
        inline static constexpr std::chrono::milliseconds s_AiModelSearchInterval{ 1000 }; // Interval between AI model search attempts.
        std::chrono::steady_clock::time_point m_AiNextModelSearchTime{ std::chrono::steady_clock::time_point::min() }; // Time when the next model search should be attempted.
        bool m_AiModelMissingWarningIssued = false;            // Prevents the missing model log from repeating every frame.
        bool m_AiModelInitialiseWarningIssued = false;         // Prevents repeated warnings when a supplied model fails to load.
        AI::FrameDatasetRecorder m_FrameDatasetRecorder;       // Helper that persists AI training samples for offline pipelines.
        bool m_FrameDatasetCaptureEnabled = false;             // Indicates whether dataset capture is currently running.
        uint32_t m_FrameDatasetCaptureInterval = 1;            // Frequency at which frames are sampled for dataset capture.
        std::filesystem::path m_FrameDatasetCaptureDirectory;  // Target directory for captured dataset artefacts.
        std::array<char, 512> m_FrameDatasetDirectoryBuffer{}; // Mutable buffer used by the dataset capture UI.

        bool m_ReadbackEnabled = false;                       // Indicates whether CPU readback is currently required by AI or recording.
        bool m_ViewportRecordingEnabled = false;               // Tracks whether viewport capture is active.
        uint32_t m_RecordingViewportId = s_InvalidViewportId;  // Active viewport being recorded.
        VkExtent2D m_RecordingExtent{ 0, 0 };                  // Resolution locked for the recording session.
        std::filesystem::path m_RecordingOutputPath{};         // Destination file path for the recording session.
        std::unique_ptr<VideoEncoder> m_VideoEncoder;          // Helper that streams recorded frames to disk.
        bool m_ViewportRecordingSessionActive = false;         // Tracks whether the encoder session is ready to accept frames.
        std::vector<uint8_t> m_PendingFrameReadbackBytes;      // Raw RGBA data copied from the GPU for capture.
        std::vector<VideoEncoder::RecordedFrame> m_ViewportFrameBuffer; // Buffered frames retained for status displays.
        std::chrono::system_clock::time_point m_LastReadbackTimestamp{}; // Timestamp captured alongside the last readback.

    private:
        // Core setup
        void ProcessAiFrame();
        bool TryInitialiseAiModel();
        void SetReadbackEnabled(bool enabled, VkExtent2D resizeTarget);
        bool TryAcquireRenderedFrame(std::vector<float>& outPixels);
        std::optional<std::filesystem::path> ResolveAiModelPath() const;
        void RequestReadbackResize(VkExtent2D targetExtent, bool force = false);
        void ApplyPendingReadbackResize();
        void CreateOrResizeReadbackResources();
        void CreateOrResizeReadbackResources(VkExtent2D targetExtent);
        void DestroyReadbackResources();
        void ResolvePendingReadback(uint32_t imageIndex, std::chrono::system_clock::time_point captureTimestamp);
        bool EnsureAiTextureResources(VkExtent2D extent);
        void DestroyAiResources();
        void UploadAiInterpolationToGpu();
        void UpdateAiDescriptorBinding();

        void CreateDescriptorPool();
        void CreateDefaultTexture();
        void CreateDefaultSkybox();
        void CreateDescriptorSets();
        void CreateSkyboxDescriptorSets();
        void DestroySkyboxDescriptorSets();
        void CreateSkyboxCubemap();
        void DestroySkyboxCubemap();
        void UpdateSkyboxBindingOnMainSets();

        void DestroyTextureSlot(TextureSlot& slot);
        bool PopulateTextureSlot(TextureSlot& slot, const Loader::TextureData& textureData);
        void EnsureTextureDescriptorCapacity();
        void RefreshTextureDescriptorBindings();
        uint32_t AcquireTextureSlot(const std::string& normalizedPath, const Loader::TextureData& textureData);
        void ResolveMaterialTextureSlots(const std::vector<std::string>& textures, size_t materialOffset, size_t materialCount);
        std::string NormalizeTexturePath(const std::string& texturePath) const;

        void EnsureMaterialBufferCapacity(size_t materialCount);
        void UpdateMaterialDescriptorBindings();
        void MarkMaterialBuffersDirty();

        void UpdateUniformBuffer(uint32_t currentImage, const Camera* cameraOverride = nullptr, VkCommandBuffer commandBuffer = VK_NULL_HANDLE);
        void UploadMeshFromCache();

        bool AcquireNextImage(uint32_t& imageIndex, VkFence inFlightFence);
        bool RecordCommandBuffer(uint32_t imageIndex);
        bool SubmitFrame(uint32_t imageIndex, VkFence inFlightFence);
        void PresentFrame(uint32_t imageIndex);

        bool IsValidViewport(const ViewportInfo& info) const { return info.Size.x > 0 && info.Size.y > 0; }
        void ProcessReloadEvents();
        void AccumulateFrameTiming(double frameMilliseconds, double framesPerSecond, VkExtent2D extent, std::chrono::system_clock::time_point captureTimestamp);
        void UpdateFrameTimingStats();
        void ExportPerformanceCapture();
        void DestroyOffscreenResources(uint32_t viewportId);
        void DestroyAllOffscreenResources();
        ViewportContext& GetOrCreateViewportContext(uint32_t viewportId);
        const ViewportContext* FindViewportContext(uint32_t viewportId) const;
        ViewportContext* FindViewportContext(uint32_t viewportId);
        void CreateOrResizeOffscreenResources(OffscreenTarget& target, VkExtent2D extent);
        const Camera* GetActiveCamera(const ViewportContext& context) const;
        void SyncFrameDatasetRecorder();
        void UpdateDatasetDirectoryBuffer();
    };
}