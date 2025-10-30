#include "Renderer/TextRenderer.h"

#include "Renderer/Buffers.h"
#include "Renderer/Commands.h"
#include "Renderer/CommandBufferPool.h"
#include "Application/Startup.h"
#include "Core/Utilities.h"

#include <stb_truetype.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <system_error>

namespace Trident
{
    namespace
    {
        constexpr uint32_t s_FirstCodepoint = 32;
        constexpr uint32_t s_CodepointCount = 96;
        constexpr uint32_t s_DefaultAtlasSize = 1024;

        struct TextPushConstants
        {
            glm::vec2 m_ViewportSize{ 1.0f, 1.0f };
        };
    }

    TextRenderer::~TextRenderer()
    {
        Shutdown();
    }

    void TextRenderer::Init(Buffers& buffers, Commands& commands, VkDescriptorPool descriptorPool, VkRenderPass renderPass, uint32_t frameCount)
    {
        if (m_IsInitialised)
        {
            return;
        }

        m_Buffers = &buffers;
        m_Commands = &commands;
        m_DescriptorPool = descriptorPool;
        m_RenderPass = renderPass;

        CreateDescriptorSetLayout();
        EnsurePerFrameBuffers(frameCount);

        if (!LoadDefaultFont())
        {
            TR_CORE_ERROR("TextRenderer failed to load the default font. Editor overlays will be missing text this frame.");
        }

        AllocateDescriptorSets(descriptorPool, frameCount);
        UpdateDescriptorSets();
        CreatePipeline(renderPass);

        m_IsInitialised = true;
        TR_CORE_TRACE("TextRenderer initialised (Frames = {}, Atlas = {}x{})", frameCount, m_AtlasWidth, m_AtlasHeight);
    }

    void TextRenderer::Shutdown()
    {
        if (!m_IsInitialised)
        {
            return;
        }

        if (!m_DescriptorSets.empty() && m_DescriptorPool != VK_NULL_HANDLE)
        {
            vkFreeDescriptorSets(Startup::GetDevice(), m_DescriptorPool, static_cast<uint32_t>(m_DescriptorSets.size()), m_DescriptorSets.data());
        }
        m_DescriptorSets.clear();

        DestroyPipeline();
        DestroyDescriptorSetLayout();
        DestroyFontResources();

        for (PerFrameBuffer& l_Buffer : m_PerFrameBuffers)
        {
            if (l_Buffer.m_Buffer != VK_NULL_HANDLE)
            {
                m_Buffers->DestroyBuffer(l_Buffer.m_Buffer, l_Buffer.m_Memory);
            }
        }
        m_PerFrameBuffers.clear();

        m_Buffers = nullptr;
        m_Commands = nullptr;
        m_DescriptorPool = VK_NULL_HANDLE;
        m_RenderPass = VK_NULL_HANDLE;
        m_IsInitialised = false;
        TR_CORE_TRACE("TextRenderer shutdown complete");
    }

    void TextRenderer::BeginFrame()
    {
        m_PendingCommands.clear();
    }

    void TextRenderer::QueueText(uint32_t viewportId, const glm::vec2& position, const glm::vec4& color, std::string_view text)
    {
        if (!m_IsInitialised)
        {
            return;
        }

        TextCommand l_Command{};
        l_Command.m_Position = position;
        l_Command.m_Color = color;
        l_Command.m_Text = DecodeUtf8(text);
        if (l_Command.m_Text.empty())
        {
            return;
        }

        m_PendingCommands[viewportId].emplace_back(std::move(l_Command));
    }

    void TextRenderer::RecordViewport(VkCommandBuffer commandBuffer, uint32_t frameIndex, uint32_t viewportId, VkExtent2D viewportExtent)
    {
        if (!m_IsInitialised || m_Pipeline == VK_NULL_HANDLE || viewportExtent.width == 0 || viewportExtent.height == 0)
        {
            return;
        }

        auto a_CommandList = m_PendingCommands.find(viewportId);
        if (a_CommandList == m_PendingCommands.end() || a_CommandList->second.empty())
        {
            return;
        }

        const std::vector<TextCommand>& l_Commands = a_CommandList->second;
        std::vector<TextVertex> l_Vertices{};

        for (const TextCommand& l_Command : l_Commands)
        {
            float l_PenX = l_Command.m_Position.x;
            float l_PenY = l_Command.m_Position.y;

            for (char32_t l_Codepoint : l_Command.m_Text)
            {
                if (l_Codepoint == U'\n')
                {
                    l_PenX = l_Command.m_Position.x;
                    l_PenY += m_LineAdvance;
                    continue;
                }

                const Glyph& l_Glyph = ResolveGlyph(l_Codepoint);
                const float l_X0 = l_PenX + l_Glyph.m_Offset.x;
                const float l_Y0 = l_PenY + l_Glyph.m_Offset.y;
                const float l_X1 = l_X0 + l_Glyph.m_Size.x;
                const float l_Y1 = l_Y0 + l_Glyph.m_Size.y;

                TextVertex l_V0{};
                l_V0.m_Position = { l_X0, l_Y0 };
                l_V0.m_UV = l_Glyph.m_UVMin;
                l_V0.m_Color = l_Command.m_Color;

                TextVertex l_V1{};
                l_V1.m_Position = { l_X1, l_Y0 };
                l_V1.m_UV = { l_Glyph.m_UVMax.x, l_Glyph.m_UVMin.y };
                l_V1.m_Color = l_Command.m_Color;

                TextVertex l_V2{};
                l_V2.m_Position = { l_X1, l_Y1 };
                l_V2.m_UV = l_Glyph.m_UVMax;
                l_V2.m_Color = l_Command.m_Color;

                TextVertex l_V3{};
                l_V3.m_Position = { l_X0, l_Y1 };
                l_V3.m_UV = { l_Glyph.m_UVMin.x, l_Glyph.m_UVMax.y };
                l_V3.m_Color = l_Command.m_Color;

                l_Vertices.push_back(l_V0);
                l_Vertices.push_back(l_V1);
                l_Vertices.push_back(l_V2);
                l_Vertices.push_back(l_V0);
                l_Vertices.push_back(l_V2);
                l_Vertices.push_back(l_V3);

                l_PenX += l_Glyph.m_Advance;
            }
        }

        if (l_Vertices.empty())
        {
            return;
        }

        if (frameIndex >= m_PerFrameBuffers.size())
        {
            return;
        }

        EnsureVertexCapacity(frameIndex, l_Vertices.size());
        UploadVertices(frameIndex, l_Vertices);

        const PerFrameBuffer& l_FrameBuffer = m_PerFrameBuffers[frameIndex];
        if (l_FrameBuffer.m_Buffer == VK_NULL_HANDLE)
        {
            return;
        }

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline);

        if (frameIndex < m_DescriptorSets.size())
        {
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout, 0, 1, &m_DescriptorSets[frameIndex], 0, nullptr);
        }

        VkDeviceSize l_Offset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &l_FrameBuffer.m_Buffer, &l_Offset);

        TextPushConstants l_Constants{};
        l_Constants.m_ViewportSize = { static_cast<float>(viewportExtent.width), static_cast<float>(viewportExtent.height) };
        vkCmdPushConstants(commandBuffer, m_PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(TextPushConstants), &l_Constants);

        vkCmdDraw(commandBuffer, static_cast<uint32_t>(l_Vertices.size()), 1, 0, 0);
    }

    void TextRenderer::RecreateDescriptors(VkDescriptorPool descriptorPool, uint32_t frameCount)
    {
        m_DescriptorPool = descriptorPool;
        if (!m_DescriptorSets.empty() && descriptorPool != VK_NULL_HANDLE)
        {
            vkFreeDescriptorSets(Startup::GetDevice(), descriptorPool, static_cast<uint32_t>(m_DescriptorSets.size()), m_DescriptorSets.data());
        }
        m_DescriptorSets.clear();

        EnsurePerFrameBuffers(frameCount);
        AllocateDescriptorSets(descriptorPool, frameCount);
        UpdateDescriptorSets();
    }

    void TextRenderer::RecreatePipeline(VkRenderPass renderPass)
    {
        m_RenderPass = renderPass;
        DestroyPipeline();
        CreatePipeline(renderPass);
    }


    bool TextRenderer::LoadDefaultFont()
    {
        std::array<std::filesystem::path, 2> l_SearchRoots
        {
            std::filesystem::path("Assets") / "Fonts" / "JetBrainsMono-Regular.ttf",
            std::filesystem::path("Trident-Forge") / "Assets" / "Fonts" / "JetBrainsMono-Regular.ttf"
        };

        for (const std::filesystem::path& l_Path : l_SearchRoots)
        {
            std::error_code l_Error{};
            if (std::filesystem::exists(l_Path, l_Error) && !l_Error)
            {
                if (LoadFontFile(l_Path.string(), m_FontPixelHeight))
                {
                    TR_CORE_INFO("Loaded default text font: {}", l_Path.string());
                    return true;
                }
            }
        }

        TR_CORE_ERROR("TextRenderer could not find JetBrainsMono-Regular.ttf in the expected asset folders.");
        return false;
    }

    bool TextRenderer::LoadFontFile(const std::string& path, float pixelHeight)
    {
        std::ifstream l_File(path, std::ios::binary);
        if (!l_File)
        {
            TR_CORE_ERROR("Failed to open font '{}'", path);
            return false;
        }

        std::vector<uint8_t> l_FontBuffer{};
        l_File.seekg(0, std::ios::end);
        const std::streamsize l_Size = l_File.tellg();
        if (l_Size <= 0)
        {
            TR_CORE_ERROR("Font '{}' is empty", path);
            return false;
        }

        l_FontBuffer.resize(static_cast<size_t>(l_Size));
        l_File.seekg(0, std::ios::beg);
        if (!l_File.read(reinterpret_cast<char*>(l_FontBuffer.data()), l_Size))
        {
            TR_CORE_ERROR("Failed to read font '{}'", path);
            return false;
        }

        stbtt_fontinfo l_FontInfo{};
        if (stbtt_InitFont(&l_FontInfo, l_FontBuffer.data(), 0) == 0)
        {
            TR_CORE_ERROR("stbtt_InitFont failed for '{}'", path);
            return false;
        }

        std::vector<uint8_t> l_AtlasPixels(s_DefaultAtlasSize * s_DefaultAtlasSize, 0);
        stbtt_pack_context l_PackContext{};
        if (stbtt_PackBegin(&l_PackContext, l_AtlasPixels.data(), s_DefaultAtlasSize, s_DefaultAtlasSize, 0, 1, nullptr) == 0)
        {
            TR_CORE_ERROR("stbtt_PackBegin failed for '{}'", path);
            return false;
        }

        stbtt_PackSetOversampling(&l_PackContext, 2, 2);
        std::vector<stbtt_packedchar> l_PackedChars(s_CodepointCount);
        if (stbtt_PackFontRange(&l_PackContext, l_FontBuffer.data(), 0, pixelHeight, s_FirstCodepoint, s_CodepointCount, l_PackedChars.data()) == 0)
        {
            TR_CORE_ERROR("stbtt_PackFontRange failed for '{}'", path);
            
            stbtt_PackEnd(&l_PackContext);
            return false;
        }
        stbtt_PackEnd(&l_PackContext);

        m_GlyphCache.clear();
        m_GlyphCache.reserve(s_CodepointCount);

        for (uint32_t it_Index = 0; it_Index < s_CodepointCount; ++it_Index)
        {
            const stbtt_packedchar& l_Packed = l_PackedChars[it_Index];
            Glyph l_Glyph{};
            l_Glyph.m_Offset = { l_Packed.xoff, l_Packed.yoff };
            l_Glyph.m_Size = { l_Packed.xoff2 - l_Packed.xoff, l_Packed.yoff2 - l_Packed.yoff };
            l_Glyph.m_UVMin = { static_cast<float>(l_Packed.x0) / static_cast<float>(s_DefaultAtlasSize), static_cast<float>(l_Packed.y0) / static_cast<float>(s_DefaultAtlasSize) };
            l_Glyph.m_UVMax = { static_cast<float>(l_Packed.x1) / static_cast<float>(s_DefaultAtlasSize), static_cast<float>(l_Packed.y1) / static_cast<float>(s_DefaultAtlasSize) };
            l_Glyph.m_Advance = l_Packed.xadvance;
            const char32_t l_Codepoint = static_cast<char32_t>(s_FirstCodepoint + it_Index);
            m_GlyphCache[l_Codepoint] = l_Glyph;
        }

        if (m_GlyphCache.find(m_FallbackGlyph) == m_GlyphCache.end())
        {
            const auto a_FallbackCandidate = m_GlyphCache.find(U'?');
            if (a_FallbackCandidate != m_GlyphCache.end())
            {
                m_FallbackGlyph = a_FallbackCandidate->first;
            }
            else if (!m_GlyphCache.empty())
            {
                m_FallbackGlyph = m_GlyphCache.begin()->first;
            }
        }

        int l_Ascent = 0;
        int l_Descent = 0;
        int l_LineGap = 0;
        stbtt_GetFontVMetrics(&l_FontInfo, &l_Ascent, &l_Descent, &l_LineGap);
        const float l_Scale = stbtt_ScaleForPixelHeight(&l_FontInfo, pixelHeight);
        m_LineAdvance = (l_Ascent - l_Descent + l_LineGap) * l_Scale;

        std::vector<uint8_t> l_RgbaPixels(s_DefaultAtlasSize * s_DefaultAtlasSize * 4, 255);
        for (size_t it_Pixel = 0; it_Pixel < l_AtlasPixels.size(); ++it_Pixel)
        {
            const uint8_t l_Value = l_AtlasPixels[it_Pixel];
            l_RgbaPixels[it_Pixel * 4 + 3] = l_Value;
        }

        CreateAtlasImage(l_RgbaPixels, s_DefaultAtlasSize, s_DefaultAtlasSize);
        UpdateDescriptorSets();
        return true;
    }

    void TextRenderer::DestroyFontResources()
    {
        DestroyAtlasImage();
        m_GlyphCache.clear();
    }

    void TextRenderer::CreateAtlasImage(const std::vector<uint8_t>& atlasPixels, uint32_t width, uint32_t height)
    {
        DestroyAtlasImage();

        if (atlasPixels.empty() || width == 0 || height == 0 || m_Buffers == nullptr || m_Commands == nullptr)
        {
            return;
        }

        VkDevice l_Device = Startup::GetDevice();

        VkBuffer l_StagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory l_StagingMemory = VK_NULL_HANDLE;
        const VkDeviceSize l_BufferSize = static_cast<VkDeviceSize>(atlasPixels.size());
        m_Buffers->CreateBuffer(l_BufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, l_StagingBuffer, l_StagingMemory);

        void* l_MappedData = nullptr;
        vkMapMemory(l_Device, l_StagingMemory, 0, l_BufferSize, 0, &l_MappedData);
        std::memcpy(l_MappedData, atlasPixels.data(), atlasPixels.size());
        vkUnmapMemory(l_Device, l_StagingMemory);

        VkImageCreateInfo l_ImageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        l_ImageInfo.imageType = VK_IMAGE_TYPE_2D;
        l_ImageInfo.extent.width = width;
        l_ImageInfo.extent.height = height;
        l_ImageInfo.extent.depth = 1;
        l_ImageInfo.mipLevels = 1;
        l_ImageInfo.arrayLayers = 1;
        l_ImageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        l_ImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        l_ImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        l_ImageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        l_ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        l_ImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage(l_Device, &l_ImageInfo, nullptr, &m_AtlasImage) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create text atlas image");
            m_Buffers->DestroyBuffer(l_StagingBuffer, l_StagingMemory);
            return;
        }

        VkMemoryRequirements l_MemoryRequirements{};
        vkGetImageMemoryRequirements(l_Device, m_AtlasImage, &l_MemoryRequirements);

        VkMemoryAllocateInfo l_AllocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        l_AllocInfo.allocationSize = l_MemoryRequirements.size;
        l_AllocInfo.memoryTypeIndex = m_Buffers->FindMemoryType(l_MemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(l_Device, &l_AllocInfo, nullptr, &m_AtlasMemory) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to allocate memory for text atlas image");
            vkDestroyImage(l_Device, m_AtlasImage, nullptr);
            m_AtlasImage = VK_NULL_HANDLE;
            m_Buffers->DestroyBuffer(l_StagingBuffer, l_StagingMemory);
            return;
        }

        vkBindImageMemory(l_Device, m_AtlasImage, m_AtlasMemory, 0);

        VkCommandBuffer l_CommandBuffer = m_Commands->BeginSingleTimeCommands();

        VkImageMemoryBarrier l_TransitionToTransfer{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        l_TransitionToTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_TransitionToTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_TransitionToTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_TransitionToTransfer.subresourceRange.baseMipLevel = 0;
        l_TransitionToTransfer.subresourceRange.levelCount = 1;
        l_TransitionToTransfer.subresourceRange.baseArrayLayer = 0;
        l_TransitionToTransfer.subresourceRange.layerCount = 1;
        l_TransitionToTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        l_TransitionToTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        l_TransitionToTransfer.srcAccessMask = 0;
        l_TransitionToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        l_TransitionToTransfer.image = m_AtlasImage;

        vkCmdPipelineBarrier(l_CommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &l_TransitionToTransfer);

        VkBufferImageCopy l_CopyRegion{};
        l_CopyRegion.bufferOffset = 0;
        l_CopyRegion.bufferRowLength = 0;
        l_CopyRegion.bufferImageHeight = 0;
        l_CopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_CopyRegion.imageSubresource.mipLevel = 0;
        l_CopyRegion.imageSubresource.baseArrayLayer = 0;
        l_CopyRegion.imageSubresource.layerCount = 1;
        l_CopyRegion.imageOffset = { 0, 0, 0 };
        l_CopyRegion.imageExtent = { width, height, 1 };

        vkCmdCopyBufferToImage(l_CommandBuffer, l_StagingBuffer, m_AtlasImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &l_CopyRegion);

        VkImageMemoryBarrier l_TransitionToShader{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        l_TransitionToShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_TransitionToShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_TransitionToShader.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_TransitionToShader.subresourceRange.baseMipLevel = 0;
        l_TransitionToShader.subresourceRange.levelCount = 1;
        l_TransitionToShader.subresourceRange.baseArrayLayer = 0;
        l_TransitionToShader.subresourceRange.layerCount = 1;
        l_TransitionToShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        l_TransitionToShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        l_TransitionToShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        l_TransitionToShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        l_TransitionToShader.image = m_AtlasImage;

        vkCmdPipelineBarrier(l_CommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &l_TransitionToShader);

        m_Commands->EndSingleTimeCommands(l_CommandBuffer);

        VkImageViewCreateInfo l_ViewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        l_ViewInfo.image = m_AtlasImage;
        l_ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        l_ViewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        l_ViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_ViewInfo.subresourceRange.baseMipLevel = 0;
        l_ViewInfo.subresourceRange.levelCount = 1;
        l_ViewInfo.subresourceRange.baseArrayLayer = 0;
        l_ViewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(l_Device, &l_ViewInfo, nullptr, &m_AtlasImageView) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create text atlas view");
            m_Buffers->DestroyBuffer(l_StagingBuffer, l_StagingMemory);
            DestroyAtlasImage();
            return;
        }

        VkSamplerCreateInfo l_SamplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        l_SamplerInfo.magFilter = VK_FILTER_LINEAR;
        l_SamplerInfo.minFilter = VK_FILTER_LINEAR;
        l_SamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        l_SamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        l_SamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        l_SamplerInfo.anisotropyEnable = VK_FALSE;
        l_SamplerInfo.maxAnisotropy = 1.0f;
        l_SamplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        l_SamplerInfo.unnormalizedCoordinates = VK_FALSE;
        l_SamplerInfo.compareEnable = VK_FALSE;
        l_SamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

        if (vkCreateSampler(l_Device, &l_SamplerInfo, nullptr, &m_AtlasSampler) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create text atlas sampler");
            m_Buffers->DestroyBuffer(l_StagingBuffer, l_StagingMemory);
            DestroyAtlasImage();
            return;
        }

        m_AtlasWidth = width;
        m_AtlasHeight = height;
        m_Buffers->DestroyBuffer(l_StagingBuffer, l_StagingMemory);
    }

    void TextRenderer::DestroyAtlasImage()
    {
        VkDevice l_Device = Startup::GetDevice();
        if (m_AtlasSampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(l_Device, m_AtlasSampler, nullptr);
            m_AtlasSampler = VK_NULL_HANDLE;
        }
        if (m_AtlasImageView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(l_Device, m_AtlasImageView, nullptr);
            m_AtlasImageView = VK_NULL_HANDLE;
        }
        if (m_AtlasImage != VK_NULL_HANDLE)
        {
            vkDestroyImage(l_Device, m_AtlasImage, nullptr);
            m_AtlasImage = VK_NULL_HANDLE;
        }
        if (m_AtlasMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(l_Device, m_AtlasMemory, nullptr);
            m_AtlasMemory = VK_NULL_HANDLE;
        }
        m_AtlasWidth = 0;
        m_AtlasHeight = 0;
    }

    void TextRenderer::CreateDescriptorSetLayout()
    {
        VkDescriptorSetLayoutBinding l_Binding{};
        l_Binding.binding = 0;
        l_Binding.descriptorCount = 1;
        l_Binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        l_Binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo l_CreateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        l_CreateInfo.bindingCount = 1;
        l_CreateInfo.pBindings = &l_Binding;

        if (vkCreateDescriptorSetLayout(Startup::GetDevice(), &l_CreateInfo, nullptr, &m_DescriptorSetLayout) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create text descriptor set layout");
            m_DescriptorSetLayout = VK_NULL_HANDLE;
        }
    }

    void TextRenderer::DestroyDescriptorSetLayout()
    {
        if (m_DescriptorSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(Startup::GetDevice(), m_DescriptorSetLayout, nullptr);
            m_DescriptorSetLayout = VK_NULL_HANDLE;
        }
    }

    void TextRenderer::AllocateDescriptorSets(VkDescriptorPool descriptorPool, uint32_t frameCount)
    {
        if (descriptorPool == VK_NULL_HANDLE || m_DescriptorSetLayout == VK_NULL_HANDLE)
        {
            return;
        }

        std::vector<VkDescriptorSetLayout> l_Layouts(frameCount, m_DescriptorSetLayout);
        VkDescriptorSetAllocateInfo l_AllocateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        l_AllocateInfo.descriptorPool = descriptorPool;
        l_AllocateInfo.descriptorSetCount = frameCount;
        l_AllocateInfo.pSetLayouts = l_Layouts.data();

        m_DescriptorSets.resize(frameCount, VK_NULL_HANDLE);
        if (vkAllocateDescriptorSets(Startup::GetDevice(), &l_AllocateInfo, m_DescriptorSets.data()) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to allocate text descriptor sets");
            m_DescriptorSets.clear();
        }
    }

    void TextRenderer::UpdateDescriptorSets()
    {
        if (m_DescriptorSets.empty() || m_AtlasImageView == VK_NULL_HANDLE || m_AtlasSampler == VK_NULL_HANDLE)
        {
            return;
        }

        VkDescriptorImageInfo l_ImageInfo{};
        l_ImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        l_ImageInfo.imageView = m_AtlasImageView;
        l_ImageInfo.sampler = m_AtlasSampler;

        std::vector<VkWriteDescriptorSet> l_Writes;
        l_Writes.reserve(m_DescriptorSets.size());
        for (VkDescriptorSet l_Set : m_DescriptorSets)
        {
            VkWriteDescriptorSet l_Write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            l_Write.dstSet = l_Set;
            l_Write.dstBinding = 0;
            l_Write.descriptorCount = 1;
            l_Write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            l_Write.pImageInfo = &l_ImageInfo;
            l_Writes.push_back(l_Write);
        }

        vkUpdateDescriptorSets(Startup::GetDevice(), static_cast<uint32_t>(l_Writes.size()), l_Writes.data(), 0, nullptr);
    }

    void TextRenderer::CreatePipeline(VkRenderPass renderPass)
    {
        if (renderPass == VK_NULL_HANDLE || m_DescriptorSetLayout == VK_NULL_HANDLE)
        {
            return;
        }

        auto a_LoadBinary = [](const std::filesystem::path& path) -> std::vector<char>
            {
                std::ifstream l_File(path, std::ios::ate | std::ios::binary);
                if (!l_File)
                {
                    return {};
                }
                const std::streamsize l_Size = l_File.tellg();
                std::vector<char> l_Buffer(static_cast<size_t>(l_Size));
                l_File.seekg(0, std::ios::beg);
                l_File.read(l_Buffer.data(), l_Size);
                return l_Buffer;
            };

        auto a_FindCompiler = []() -> std::vector<std::string>
            {
                std::vector<std::string> l_Candidates{};
                if (const char* l_Custom = std::getenv("TRIDENT_GLSL_COMPILER"))
                {
                    l_Candidates.emplace_back(l_Custom);
                }
                l_Candidates.emplace_back("glslc");
                l_Candidates.emplace_back("glslc.exe");
                l_Candidates.emplace_back("glslangValidator");
                l_Candidates.emplace_back("glslangValidator.exe");
                return l_Candidates;
            };

        auto a_EnsureShader = [&](const std::filesystem::path& sourcePath) -> std::filesystem::path
            {
                std::filesystem::path l_SpirvPath = sourcePath;
                l_SpirvPath += ".spv";

                std::error_code l_Error{};
                bool l_ShouldCompile = !std::filesystem::exists(l_SpirvPath, l_Error);
                if (!l_ShouldCompile && std::filesystem::exists(sourcePath, l_Error))
                {
                    const auto a_SpirvTime = std::filesystem::last_write_time(l_SpirvPath, l_Error);
                    const auto a_SourceTime = std::filesystem::last_write_time(sourcePath, l_Error);
                    if (a_SpirvTime < a_SourceTime)
                    {
                        l_ShouldCompile = true;
                    }
                }

                if (l_ShouldCompile)
                {
                    bool l_Compiled = false;
                    for (const std::string& l_Compiler : a_FindCompiler())
                    {
                        std::string l_Command = std::string("\\\"") + l_Compiler + "\\\" -V \\\"" + sourcePath.string() + "\\\" -o \\\"" + l_SpirvPath.string() + "\\\"";
                        const int l_Result = std::system(l_Command.c_str());
                        if (l_Result == 0)
                        {
                            l_Compiled = true;
                            break;
                        }
                    }

                    if (!l_Compiled)
                    {
                        TR_CORE_ERROR("Failed to compile text shader '{}'", sourcePath.string());
                    }
                }

                return l_SpirvPath;
            };

        std::filesystem::path l_ShaderRoot = std::filesystem::path("Assets") / "Shaders";
        const std::filesystem::path l_VertexSource = a_EnsureShader(l_ShaderRoot / "Text.vert");
        const std::filesystem::path l_FragmentSource = a_EnsureShader(l_ShaderRoot / "Text.frag");

        const std::vector<char> l_VertexCode = a_LoadBinary(l_VertexSource);
        const std::vector<char> l_FragmentCode = a_LoadBinary(l_FragmentSource);
        if (l_VertexCode.empty() || l_FragmentCode.empty())
        {
            TR_CORE_ERROR("Failed to load text shaders. Expected '{}.spv' and '{}.spv'", l_VertexSource.string(), l_FragmentSource.string());
            return;
        }

        VkShaderModuleCreateInfo l_VertexModuleInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        l_VertexModuleInfo.codeSize = l_VertexCode.size();
        l_VertexModuleInfo.pCode = reinterpret_cast<const uint32_t*>(l_VertexCode.data());
        VkShaderModule l_VertexModule = VK_NULL_HANDLE;
        if (vkCreateShaderModule(Startup::GetDevice(), &l_VertexModuleInfo, nullptr, &l_VertexModule) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create text vertex shader module");
            return;
        }

        VkShaderModuleCreateInfo l_FragmentModuleInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        l_FragmentModuleInfo.codeSize = l_FragmentCode.size();
        l_FragmentModuleInfo.pCode = reinterpret_cast<const uint32_t*>(l_FragmentCode.data());
        VkShaderModule l_FragmentModule = VK_NULL_HANDLE;
        if (vkCreateShaderModule(Startup::GetDevice(), &l_FragmentModuleInfo, nullptr, &l_FragmentModule) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create text fragment shader module");
            vkDestroyShaderModule(Startup::GetDevice(), l_VertexModule, nullptr);
            return;
        }

        VkPipelineShaderStageCreateInfo l_Stages[2]{};
        l_Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        l_Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        l_Stages[0].module = l_VertexModule;
        l_Stages[0].pName = "main";
        l_Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        l_Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        l_Stages[1].module = l_FragmentModule;
        l_Stages[1].pName = "main";

        VkVertexInputBindingDescription l_Binding{};
        l_Binding.binding = 0;
        l_Binding.stride = sizeof(TextVertex);
        l_Binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 3> l_Attributes{};
        l_Attributes[0].binding = 0;
        l_Attributes[0].location = 0;
        l_Attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
        l_Attributes[0].offset = offsetof(TextVertex, m_Position);
        l_Attributes[1].binding = 0;
        l_Attributes[1].location = 1;
        l_Attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
        l_Attributes[1].offset = offsetof(TextVertex, m_UV);
        l_Attributes[2].binding = 0;
        l_Attributes[2].location = 2;
        l_Attributes[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        l_Attributes[2].offset = offsetof(TextVertex, m_Color);

        VkPipelineVertexInputStateCreateInfo l_VertexInput{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        l_VertexInput.vertexBindingDescriptionCount = 1;
        l_VertexInput.pVertexBindingDescriptions = &l_Binding;
        l_VertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(l_Attributes.size());
        l_VertexInput.pVertexAttributeDescriptions = l_Attributes.data();

        VkPipelineInputAssemblyStateCreateInfo l_InputAssembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        l_InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo l_ViewportState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        l_ViewportState.viewportCount = 1;
        l_ViewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo l_Rasterization{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        l_Rasterization.polygonMode = VK_POLYGON_MODE_FILL;
        l_Rasterization.cullMode = VK_CULL_MODE_NONE;
        l_Rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        l_Rasterization.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo l_Multisample{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        l_Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState l_BlendAttachment{};
        l_BlendAttachment.blendEnable = VK_TRUE;
        l_BlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        l_BlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        l_BlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        l_BlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        l_BlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        l_BlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        l_BlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo l_ColorBlend{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        l_ColorBlend.attachmentCount = 1;
        l_ColorBlend.pAttachments = &l_BlendAttachment;

        VkDynamicState l_DynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo l_Dynamic{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
        l_Dynamic.dynamicStateCount = static_cast<uint32_t>(std::size(l_DynamicStates));
        l_Dynamic.pDynamicStates = l_DynamicStates;

        VkPipelineDepthStencilStateCreateInfo l_DepthStencil{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
        l_DepthStencil.depthTestEnable = VK_FALSE;
        l_DepthStencil.depthWriteEnable = VK_FALSE;

        VkPushConstantRange l_PushRange{};
        l_PushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        l_PushRange.offset = 0;
        l_PushRange.size = sizeof(TextPushConstants);

        VkPipelineLayoutCreateInfo l_LayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        l_LayoutInfo.setLayoutCount = 1;
        l_LayoutInfo.pSetLayouts = &m_DescriptorSetLayout;
        l_LayoutInfo.pushConstantRangeCount = 1;
        l_LayoutInfo.pPushConstantRanges = &l_PushRange;

        if (vkCreatePipelineLayout(Startup::GetDevice(), &l_LayoutInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create text pipeline layout");
            vkDestroyShaderModule(Startup::GetDevice(), l_VertexModule, nullptr);
            vkDestroyShaderModule(Startup::GetDevice(), l_FragmentModule, nullptr);
            return;
        }

        VkGraphicsPipelineCreateInfo l_PipelineInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        l_PipelineInfo.stageCount = 2;
        l_PipelineInfo.pStages = l_Stages;
        l_PipelineInfo.pVertexInputState = &l_VertexInput;
        l_PipelineInfo.pInputAssemblyState = &l_InputAssembly;
        l_PipelineInfo.pViewportState = &l_ViewportState;
        l_PipelineInfo.pRasterizationState = &l_Rasterization;
        l_PipelineInfo.pMultisampleState = &l_Multisample;
        l_PipelineInfo.pDepthStencilState = &l_DepthStencil;
        l_PipelineInfo.pColorBlendState = &l_ColorBlend;
        l_PipelineInfo.pDynamicState = &l_Dynamic;
        l_PipelineInfo.layout = m_PipelineLayout;
        l_PipelineInfo.renderPass = renderPass;
        l_PipelineInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(Startup::GetDevice(), VK_NULL_HANDLE, 1, &l_PipelineInfo, nullptr, &m_Pipeline) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create text graphics pipeline");
        }

        vkDestroyShaderModule(Startup::GetDevice(), l_VertexModule, nullptr);
        vkDestroyShaderModule(Startup::GetDevice(), l_FragmentModule, nullptr);
    }
    void TextRenderer::DestroyPipeline()
    {
        VkDevice l_Device = Startup::GetDevice();
        if (m_Pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(l_Device, m_Pipeline, nullptr);
            m_Pipeline = VK_NULL_HANDLE;
        }
        if (m_PipelineLayout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(l_Device, m_PipelineLayout, nullptr);
            m_PipelineLayout = VK_NULL_HANDLE;
        }
    }

    void TextRenderer::EnsurePerFrameBuffers(uint32_t frameCount)
    {
        if (m_Buffers == nullptr)
        {
            return;
        }

        if (m_PerFrameBuffers.size() > frameCount)
        {
            for (size_t it_Index = frameCount; it_Index < m_PerFrameBuffers.size(); ++it_Index)
            {
                PerFrameBuffer& l_Buffer = m_PerFrameBuffers[it_Index];
                if (l_Buffer.m_Buffer != VK_NULL_HANDLE)
                {
                    m_Buffers->DestroyBuffer(l_Buffer.m_Buffer, l_Buffer.m_Memory);
                    l_Buffer.m_Buffer = VK_NULL_HANDLE;
                    l_Buffer.m_Memory = VK_NULL_HANDLE;
                    l_Buffer.m_Capacity = 0;
                }
            }
            m_PerFrameBuffers.resize(frameCount);
        }
        else if (m_PerFrameBuffers.size() < frameCount)
        {
            const size_t l_OldSize = m_PerFrameBuffers.size();
            m_PerFrameBuffers.resize(frameCount);
            for (size_t it_Index = l_OldSize; it_Index < m_PerFrameBuffers.size(); ++it_Index)
            {
                m_PerFrameBuffers[it_Index] = {};
            }
        }
    }

    void TextRenderer::EnsureVertexCapacity(uint32_t frameIndex, size_t requiredVertexCount)
    {
        if (frameIndex >= m_PerFrameBuffers.size() || m_Buffers == nullptr)
        {
            return;
        }

        PerFrameBuffer& l_Buffer = m_PerFrameBuffers[frameIndex];
        if (requiredVertexCount <= l_Buffer.m_Capacity && l_Buffer.m_Buffer != VK_NULL_HANDLE)
        {
            return;
        }

        size_t l_NewCapacity = std::max<size_t>(requiredVertexCount, l_Buffer.m_Capacity == 0 ? 512 : l_Buffer.m_Capacity * 2);
        if (l_Buffer.m_Buffer != VK_NULL_HANDLE)
        {
            m_Buffers->DestroyBuffer(l_Buffer.m_Buffer, l_Buffer.m_Memory);
        }

        const VkDeviceSize l_BufferSize = static_cast<VkDeviceSize>(l_NewCapacity * sizeof(TextVertex));
        m_Buffers->CreateBuffer(l_BufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, l_Buffer.m_Buffer, l_Buffer.m_Memory);
        l_Buffer.m_Capacity = l_NewCapacity;
    }

    void TextRenderer::UploadVertices(uint32_t frameIndex, std::span<const TextVertex> vertices)
    {
        if (frameIndex >= m_PerFrameBuffers.size())
        {
            return;
        }

        PerFrameBuffer& l_Buffer = m_PerFrameBuffers[frameIndex];
        if (l_Buffer.m_Buffer == VK_NULL_HANDLE || vertices.empty())
        {
            return;
        }

        VkDevice l_Device = Startup::GetDevice();
        void* l_Data = nullptr;
        const VkDeviceSize l_Size = static_cast<VkDeviceSize>(vertices.size() * sizeof(TextVertex));
        if (vkMapMemory(l_Device, l_Buffer.m_Memory, 0, l_Size, 0, &l_Data) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to map text vertex buffer");
            return;
        }

        std::memcpy(l_Data, vertices.data(), static_cast<size_t>(l_Size));
        vkUnmapMemory(l_Device, l_Buffer.m_Memory);
    }

    std::u32string TextRenderer::DecodeUtf8(std::string_view text) const
    {
        std::u32string l_Result{};
        size_t l_Index = 0;
        while (l_Index < text.size())
        {
            const uint8_t l_Byte = static_cast<uint8_t>(text[l_Index]);
            char32_t l_Codepoint = 0;
            size_t l_ContinuationCount = 0;

            if ((l_Byte & 0x80u) == 0)
            {
                l_Codepoint = l_Byte;
                l_ContinuationCount = 0;
            }
            else if ((l_Byte & 0xE0u) == 0xC0u)
            {
                l_Codepoint = l_Byte & 0x1Fu;
                l_ContinuationCount = 1;
            }
            else if ((l_Byte & 0xF0u) == 0xE0u)
            {
                l_Codepoint = l_Byte & 0x0Fu;
                l_ContinuationCount = 2;
            }
            else if ((l_Byte & 0xF8u) == 0xF0u)
            {
                l_Codepoint = l_Byte & 0x07u;
                l_ContinuationCount = 3;
            }
            else
            {
                ++l_Index;
                continue;
            }

            if (l_Index + l_ContinuationCount >= text.size())
            {
                break;
            }

            bool l_InvalidSequence = false;
            for (size_t it_Continuation = 0; it_Continuation < l_ContinuationCount; ++it_Continuation)
            {
                const uint8_t l_Next = static_cast<uint8_t>(text[l_Index + 1 + it_Continuation]);
                if ((l_Next & 0xC0u) != 0x80u)
                {
                    l_InvalidSequence = true;
                    break;
                }
                l_Codepoint = (l_Codepoint << 6) | (l_Next & 0x3Fu);
            }

            if (!l_InvalidSequence)
            {
                l_Result.push_back(l_Codepoint);
                l_Index += l_ContinuationCount + 1;
            }
            else
            {
                ++l_Index;
            }
        }

        return l_Result;
    }

    const TextRenderer::Glyph& TextRenderer::ResolveGlyph(char32_t codepoint) const
    {
        auto a_Found = m_GlyphCache.find(codepoint);
        if (a_Found != m_GlyphCache.end())
        {
            return a_Found->second;
        }

        auto a_Fallback = m_GlyphCache.find(m_FallbackGlyph);
        if (a_Fallback != m_GlyphCache.end())
        {
            return a_Fallback->second;
        }

        static Glyph s_EmptyGlyph{};
        return s_EmptyGlyph;
    }
}