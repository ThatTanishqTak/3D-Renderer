#pragma once

#include <vulkan/vulkan.h>

#include <glm/glm.hpp>

#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Trident
{
    class Buffers;
    class Commands;

    /**
     * @brief Handles font loading, atlas generation, and Vulkan resource lifetime for 2D text overlays.
     *
     * The renderer exposes a per-viewport submission API that funnels text into this helper. Each frame the
     * TextRenderer builds a dynamic vertex buffer describing the queued glyph quads, binds a dedicated
     * graphics pipeline configured for alpha blending, and issues draw calls after the main scene pass.
     */
    class TextRenderer
    {
    public:
        TextRenderer() = default;
        ~TextRenderer();

        void Init(Buffers& buffers, Commands& commands, VkDescriptorPool descriptorPool, VkRenderPass renderPass, uint32_t frameCount);
        void Shutdown();

        void BeginFrame();

        void QueueText(uint32_t viewportId, const glm::vec2& position, const glm::vec4& color, std::string_view text);

        void RecordViewport(VkCommandBuffer commandBuffer, uint32_t frameIndex, uint32_t viewportId, VkExtent2D viewportExtent);

        void RecreateDescriptors(VkDescriptorPool descriptorPool, uint32_t frameCount);

        void RecreatePipeline(VkRenderPass renderPass);

    private:
        struct Glyph
        {
            glm::vec2 m_Offset{ 0.0f }; ///< Offset from the pen position to the quad origin in screen-space pixels.
            glm::vec2 m_Size{ 0.0f };   ///< Width/height of the rendered quad in pixels.
            glm::vec2 m_UVMin{ 0.0f };  ///< Normalised atlas coordinates (top-left) used for sampling.
            glm::vec2 m_UVMax{ 0.0f };  ///< Normalised atlas coordinates (bottom-right) used for sampling.
            float m_Advance = 0.0f;     ///< Horizontal advance applied to the pen after emitting this glyph.
        };

        struct TextVertex
        {
            glm::vec2 m_Position{ 0.0f }; ///< Screen-space coordinates relative to the viewport origin.
            glm::vec2 m_UV{ 0.0f };       ///< Atlas texture coordinates.
            glm::vec4 m_Color{ 1.0f };    ///< RGBA colour packed directly into the vertex stream.
        };

        struct TextCommand
        {
            glm::vec2 m_Position{ 0.0f }; ///< Top-left anchor in pixels relative to the viewport.
            glm::vec4 m_Color{ 1.0f };    ///< Base colour applied to every glyph in the command.
            std::u32string m_Text;        ///< UTF-32 sequence so glyph lookup becomes a direct map access.
        };

        struct PerFrameBuffer
        {
            VkBuffer m_Buffer = VK_NULL_HANDLE;
            VkDeviceMemory m_Memory = VK_NULL_HANDLE;
            size_t m_Capacity = 0; ///< Number of vertices the buffer can currently hold.
        };

    private:
        bool LoadDefaultFont();
        bool LoadFontFile(const std::string& path, float pixelHeight);
        void DestroyFontResources();

        void CreateAtlasImage(const std::vector<uint8_t>& atlasPixels, uint32_t width, uint32_t height);
        void DestroyAtlasImage();

        void CreateDescriptorSetLayout();
        void DestroyDescriptorSetLayout();
        void AllocateDescriptorSets(VkDescriptorPool descriptorPool, uint32_t frameCount);
        void UpdateDescriptorSets();

        void CreatePipeline(VkRenderPass renderPass);
        void DestroyPipeline();

        void EnsurePerFrameBuffers(uint32_t frameCount);
        void EnsureVertexCapacity(uint32_t frameIndex, size_t requiredVertexCount);
        void UploadVertices(uint32_t frameIndex, std::span<const TextVertex> vertices);

        std::u32string DecodeUtf8(std::string_view text) const;
        const Glyph& ResolveGlyph(char32_t codepoint) const;

    private:
        Buffers* m_Buffers = nullptr;
        Commands* m_Commands = nullptr;
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        VkRenderPass m_RenderPass = VK_NULL_HANDLE;

        VkImage m_AtlasImage = VK_NULL_HANDLE;
        VkDeviceMemory m_AtlasMemory = VK_NULL_HANDLE;
        VkImageView m_AtlasImageView = VK_NULL_HANDLE;
        VkSampler m_AtlasSampler = VK_NULL_HANDLE;
        uint32_t m_AtlasWidth = 0;
        uint32_t m_AtlasHeight = 0;

        VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
        VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
        VkPipeline m_Pipeline = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_DescriptorSets;

        std::unordered_map<char32_t, Glyph> m_GlyphCache;
        std::unordered_map<uint32_t, std::vector<TextCommand>> m_PendingCommands;
        std::vector<PerFrameBuffer> m_PerFrameBuffers;

        float m_FontPixelHeight = 32.0f;
        float m_LineAdvance = 32.0f;
        char32_t m_FallbackGlyph = U'?';

        bool m_IsInitialised = false;
    };
}