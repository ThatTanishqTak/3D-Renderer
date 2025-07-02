#include "Renderer/Renderer.h"

#include "Application.h"
#include "Geometry/Mesh.h"
#include "UI/ImGuiLayer.h"

#include <stdexcept>

#include <glm/gtc/matrix_transform.hpp>

namespace
{
    glm::mat4 ComposeTransform(const Trident::Transform& transform)
    {
        glm::mat4 l_Mat{ 1.0f };
        l_Mat = glm::translate(l_Mat, transform.Position);
        l_Mat = glm::rotate(l_Mat, glm::radians(transform.Rotation.x), glm::vec3{ 1.0f, 0.0f, 0.0f });
        l_Mat = glm::rotate(l_Mat, glm::radians(transform.Rotation.y), glm::vec3{ 0.0f, 1.0f, 0.0f });
        l_Mat = glm::rotate(l_Mat, glm::radians(transform.Rotation.z), glm::vec3{ 0.0f, 0.0f, 1.0f });
        l_Mat = glm::scale(l_Mat, transform.Scale);

        return l_Mat;
    }
}

namespace Trident
{
    Renderer::~Renderer()
    {
        if (!m_Shutdown)
        {
            Shutdown();
        }
    }

    void Renderer::Init()
    {
        TR_CORE_INFO("-------INITIALIZING RENDERER-------");

        m_Swapchain.Init();
        m_Pipeline.Init(m_Swapchain);
        m_Commands.Init(m_Swapchain.GetImageCount());

        m_Buffers.CreateUniformBuffers(m_Swapchain.GetImageCount(), m_UniformBuffers, m_UniformBuffersMemory);

        CreateDescriptorPool();
        CreateDefaultTexture();
        CreateDefaultSkybox();
        CreateDescriptorSets();

        m_Camera = Camera(Application::GetWindow().GetNativeWindow());

        VkFenceCreateInfo l_FenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        l_FenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (vkCreateFence(Application::GetDevice(), &l_FenceInfo, nullptr, &m_ResourceFence) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create resource fence");
        }

        TR_CORE_INFO("-------RENDERER INITIALIZED-------");
    }

    void Renderer::Shutdown()
    {
        TR_CORE_TRACE("Shutting Down Renderer");

        m_Commands.Cleanup();

        if (m_DescriptorPool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(Application::GetDevice(), m_DescriptorPool, nullptr);

            m_DescriptorPool = VK_NULL_HANDLE;
        }

        m_DescriptorSets.clear();
        m_Pipeline.Cleanup();
        m_Swapchain.Cleanup();
        m_Skybox.Cleanup(m_Buffers);
        m_Buffers.Cleanup();
        m_UniformBuffers.clear();
        m_UniformBuffersMemory.clear();

        if (m_TextureSampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(Application::GetDevice(), m_TextureSampler, nullptr);

            m_TextureSampler = VK_NULL_HANDLE;
        }

        if (m_TextureImageView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(Application::GetDevice(), m_TextureImageView, nullptr);

            m_TextureImageView = VK_NULL_HANDLE;
        }

        if (m_TextureImage != VK_NULL_HANDLE)
        {
            vkDestroyImage(Application::GetDevice(), m_TextureImage, nullptr);

            m_TextureImage = VK_NULL_HANDLE;
        }

        if (m_TextureImageMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(Application::GetDevice(), m_TextureImageMemory, nullptr);

            m_TextureImageMemory = VK_NULL_HANDLE;
        }

        if (m_OffscreenSampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(Application::GetDevice(), m_OffscreenSampler, nullptr);

            m_OffscreenSampler = VK_NULL_HANDLE;
        }

        if (m_ResourceFence != VK_NULL_HANDLE)
        {
            vkDestroyFence(Application::GetDevice(), m_ResourceFence, nullptr);

            m_ResourceFence = VK_NULL_HANDLE;
        }

        m_Shutdown = true;

        TR_CORE_TRACE("Renderer Shutdown Complete");
    }

    void Renderer::DrawFrame()
    {
        m_Camera.Update(Utilities::Time::GetDeltaTime());

        VkFence l_InFlightFence = m_Commands.GetInFlightFence(m_Commands.CurrentFrame());
        vkWaitForFences(Application::GetDevice(), 1, &l_InFlightFence, VK_TRUE, UINT64_MAX);

        uint32_t l_ImageIndex = 0;
        if (!AcquireNextImage(l_ImageIndex, l_InFlightFence))
        {
            TR_CORE_CRITICAL("Failed to acquire the next image");

            return;
        }

        UpdateUniformBuffer(l_ImageIndex);

        vkResetFences(Application::GetDevice(), 1, &l_InFlightFence);

        if (!RecordCommandBuffer(l_ImageIndex))
        {
            TR_CORE_CRITICAL("Failed to record command buffer");
            
            return;
        }

        if (!SubmitFrame(l_ImageIndex, l_InFlightFence))
        {
            TR_CORE_CRITICAL("Failed to submit frame");

            return;
        }

        PresentFrame(l_ImageIndex);

        m_Commands.CurrentFrame() = (m_Commands.CurrentFrame() + 1) % m_Commands.GetFrameCount();
    }

    void Renderer::UploadMesh(const std::vector<Geometry::Mesh>& meshes)
    {
        // Ensure no GPU operations are using the old buffers
        vkWaitForFences(Application::GetDevice(), 1, &m_ResourceFence, VK_TRUE, UINT64_MAX);

        if (m_VertexBuffer != VK_NULL_HANDLE)
        {
            m_Buffers.DestroyBuffer(m_VertexBuffer, m_VertexBufferMemory);
            m_VertexBuffer = VK_NULL_HANDLE;
            m_VertexBufferMemory = VK_NULL_HANDLE;
        }

        if (m_IndexBuffer != VK_NULL_HANDLE)
        {
            m_Buffers.DestroyBuffer(m_IndexBuffer, m_IndexBufferMemory);
            m_IndexBuffer = VK_NULL_HANDLE;
            m_IndexBufferMemory = VK_NULL_HANDLE;
            m_IndexCount = 0;
        }

        std::vector<Vertex> l_AllVertices;
        std::vector<uint32_t> l_AllIndices;
        uint32_t l_Offset = 0;

        for (const auto& l_Mesh : meshes)
        {
            l_AllVertices.insert(l_AllVertices.end(), l_Mesh.Vertices.begin(), l_Mesh.Vertices.end());
            for (auto index : l_Mesh.Indices)
            {
                l_AllIndices.push_back(index + l_Offset);
            }
            l_Offset += static_cast<uint32_t>(l_Mesh.Vertices.size());
        }

        m_Buffers.CreateVertexBuffer(l_AllVertices, m_Commands.GetCommandPool(), m_VertexBuffer, m_VertexBufferMemory);
        m_Buffers.CreateIndexBuffer(l_AllIndices, m_Commands.GetCommandPool(), m_IndexBuffer, m_IndexBufferMemory, m_IndexCount);
    }

    void Renderer::UploadTexture(const Loader::TextureData& texture)
    {
        TR_CORE_TRACE("Uploading texture ({}x{})", texture.Width, texture.Height);

        if (texture.Pixels.empty())
        {
            TR_CORE_WARN("Texture has no data");

            return;
        }

        VkDevice l_Device = Application::GetDevice();

        vkWaitForFences(l_Device, 1, &m_ResourceFence, VK_TRUE, UINT64_MAX);

        if (m_TextureSampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(l_Device, m_TextureSampler, nullptr);
            m_TextureSampler = VK_NULL_HANDLE;
        }
        
        if (m_TextureImageView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(l_Device, m_TextureImageView, nullptr);
            m_TextureImageView = VK_NULL_HANDLE;
        }
        
        if (m_TextureImage != VK_NULL_HANDLE)
        {
            vkDestroyImage(l_Device, m_TextureImage, nullptr);
            m_TextureImage = VK_NULL_HANDLE;
        }

        if (m_TextureImageMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(l_Device, m_TextureImageMemory, nullptr);
            m_TextureImageMemory = VK_NULL_HANDLE;
        }

        VkDeviceSize l_ImageSize = static_cast<VkDeviceSize>(texture.Pixels.size());

        VkBuffer l_StagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory l_StagingMemory = VK_NULL_HANDLE;
        m_Buffers.CreateBuffer(l_ImageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, l_StagingBuffer, l_StagingMemory);

        void* l_Data = nullptr;
        vkMapMemory(l_Device, l_StagingMemory, 0, l_ImageSize, 0, &l_Data);
        memcpy(l_Data, texture.Pixels.data(), static_cast<size_t>(l_ImageSize));
        vkUnmapMemory(l_Device, l_StagingMemory);

        VkImageCreateInfo l_ImageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        l_ImageInfo.imageType = VK_IMAGE_TYPE_2D;
        l_ImageInfo.extent.width = static_cast<uint32_t>(texture.Width);
        l_ImageInfo.extent.height = static_cast<uint32_t>(texture.Height);
        l_ImageInfo.extent.depth = 1;
        l_ImageInfo.mipLevels = 1;
        l_ImageInfo.arrayLayers = 1;
        l_ImageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        l_ImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        l_ImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        l_ImageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        l_ImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        l_ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

        if (vkCreateImage(l_Device, &l_ImageInfo, nullptr, &m_TextureImage) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create texture image");
        }

        VkMemoryRequirements l_MemReq{};
        vkGetImageMemoryRequirements(l_Device, m_TextureImage, &l_MemReq);

        VkMemoryAllocateInfo l_AllocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        l_AllocInfo.allocationSize = l_MemReq.size;
        l_AllocInfo.memoryTypeIndex = m_Buffers.FindMemoryType(l_MemReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(l_Device, &l_AllocInfo, nullptr, &m_TextureImageMemory) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to allocate texture memory");
        }

        vkBindImageMemory(l_Device, m_TextureImage, m_TextureImageMemory, 0);

        VkCommandBuffer l_CommandBuffer = m_Commands.BeginSingleTimeCommands();

        VkImageMemoryBarrier l_BarrierToTransfer{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        l_BarrierToTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        l_BarrierToTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        l_BarrierToTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_BarrierToTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_BarrierToTransfer.image = m_TextureImage;
        l_BarrierToTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_BarrierToTransfer.subresourceRange.baseMipLevel = 0;
        l_BarrierToTransfer.subresourceRange.levelCount = 1;
        l_BarrierToTransfer.subresourceRange.baseArrayLayer = 0;
        l_BarrierToTransfer.subresourceRange.layerCount = 1;
        l_BarrierToTransfer.srcAccessMask = 0;
        l_BarrierToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(l_CommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &l_BarrierToTransfer);

        VkBufferImageCopy l_Region{};
        l_Region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_Region.imageSubresource.mipLevel = 0;
        l_Region.imageSubresource.baseArrayLayer = 0;
        l_Region.imageSubresource.layerCount = 1;
        l_Region.imageOffset = { 0, 0, 0 };
        l_Region.imageExtent = { static_cast<uint32_t>(texture.Width), static_cast<uint32_t>(texture.Height), 1 };

        vkCmdCopyBufferToImage(l_CommandBuffer, l_StagingBuffer, m_TextureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &l_Region);

        VkImageMemoryBarrier l_BarrierToShader{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        l_BarrierToShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        l_BarrierToShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        l_BarrierToShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_BarrierToShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_BarrierToShader.image = m_TextureImage;
        l_BarrierToShader.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_BarrierToShader.subresourceRange.baseMipLevel = 0;
        l_BarrierToShader.subresourceRange.levelCount = 1;
        l_BarrierToShader.subresourceRange.baseArrayLayer = 0;
        l_BarrierToShader.subresourceRange.layerCount = 1;
        l_BarrierToShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        l_BarrierToShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(l_CommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &l_BarrierToShader);

        m_Commands.EndSingleTimeCommands(l_CommandBuffer);

        m_Buffers.DestroyBuffer(l_StagingBuffer, l_StagingMemory);

        VkImageViewCreateInfo l_ViewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        l_ViewInfo.image = m_TextureImage;
        l_ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        l_ViewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        l_ViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_ViewInfo.subresourceRange.baseMipLevel = 0;
        l_ViewInfo.subresourceRange.levelCount = 1;
        l_ViewInfo.subresourceRange.baseArrayLayer = 0;
        l_ViewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(l_Device, &l_ViewInfo, nullptr, &m_TextureImageView) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create texture image view");
        }

        VkSamplerCreateInfo l_SamplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        l_SamplerInfo.magFilter = VK_FILTER_LINEAR;
        l_SamplerInfo.minFilter = VK_FILTER_LINEAR;
        l_SamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        l_SamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        l_SamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        l_SamplerInfo.anisotropyEnable = VK_FALSE;
        l_SamplerInfo.maxAnisotropy = 1.0f;
        l_SamplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE;
        l_SamplerInfo.unnormalizedCoordinates = VK_FALSE;
        l_SamplerInfo.compareEnable = VK_FALSE;
        l_SamplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        l_SamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        l_SamplerInfo.mipLodBias = 0.0f;
        l_SamplerInfo.minLod = 0.0f;
        l_SamplerInfo.maxLod = 0.0f;

        if (vkCreateSampler(l_Device, &l_SamplerInfo, nullptr, &m_TextureSampler) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create texture sampler");
        }

        for (size_t i = 0; i < m_DescriptorSets.size(); ++i)
        {
            VkDescriptorImageInfo l_ImageInfo{};
            l_ImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            l_ImageInfo.imageView = m_TextureImageView;
            l_ImageInfo.sampler = m_TextureSampler;

            VkWriteDescriptorSet l_ImageWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            l_ImageWrite.dstSet = m_DescriptorSets[i];
            l_ImageWrite.dstBinding = 1;
            l_ImageWrite.dstArrayElement = 0;
            l_ImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            l_ImageWrite.descriptorCount = 1;
            l_ImageWrite.pImageInfo = &l_ImageInfo;

            vkUpdateDescriptorSets(l_Device, 1, &l_ImageWrite, 0, nullptr);
        }

        TR_CORE_TRACE("Texture uploaded");
    }

    void Renderer::SetImGuiLayer(UI::ImGuiLayer* layer)
    {
        m_ImGuiLayer = layer;
    }

    void Renderer::RecreateSwapchain()
    {
        TR_CORE_TRACE("Recreating Swapchain");

        uint32_t l_Width = 0;
        uint32_t l_Height = 0;

        Application::GetWindow().GetFramebufferSize(l_Width, l_Height);

        while (l_Width == 0 || l_Height == 0)
        {
            glfwWaitEvents();

            Application::GetWindow().GetFramebufferSize(l_Width, l_Height);
        }

        vkDeviceWaitIdle(Application::GetDevice());

        m_Pipeline.CleanupFramebuffers();

        m_Swapchain.Cleanup();
        m_Swapchain.Init();

        uint32_t l_ImageCount = m_Swapchain.GetImageCount();
        if (l_ImageCount != m_UniformBuffers.size())
        {
            for (size_t i = 0; i < m_UniformBuffers.size(); ++i)
            {
                m_Buffers.DestroyBuffer(m_UniformBuffers[i], m_UniformBuffersMemory[i]);
            }

            if (!m_DescriptorSets.empty())
            {
                vkFreeDescriptorSets(Application::GetDevice(), m_DescriptorPool, static_cast<uint32_t>(m_DescriptorSets.size()), m_DescriptorSets.data());
                m_DescriptorSets.clear();
            }

            m_UniformBuffers.clear();
            m_UniformBuffersMemory.clear();

            m_Buffers.CreateUniformBuffers(l_ImageCount, m_UniformBuffers, m_UniformBuffersMemory);
            CreateDescriptorSets();
        }

        m_Pipeline.CreateFramebuffers(m_Swapchain);

        m_Commands.Recreate(m_Swapchain.GetImageCount());

        TR_CORE_TRACE("Swapchain Recreated");
    }

    //------------------------------------------------------------------------------------------------------------------------------------------------------//

    void Renderer::CreateDescriptorPool()
    {
        TR_CORE_TRACE("Creating Descriptor Pool");

        VkDescriptorPoolSize l_PoolSizes[2]{};
        l_PoolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        l_PoolSizes[0].descriptorCount = m_Swapchain.GetImageCount();
        l_PoolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        l_PoolSizes[1].descriptorCount = m_Swapchain.GetImageCount();

        VkDescriptorPoolCreateInfo l_PoolInfo{};
        l_PoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        l_PoolInfo.poolSizeCount = 2;
        l_PoolInfo.pPoolSizes = l_PoolSizes;
        l_PoolInfo.maxSets = m_Swapchain.GetImageCount();

        if (vkCreateDescriptorPool(Application::GetDevice(), &l_PoolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create descriptor pool");
        }

        TR_CORE_TRACE("Descriptor Pool Created (MaxSets = {})", l_PoolInfo.maxSets);
    }

    void Renderer::CreateDefaultTexture()
    {
        TR_CORE_TRACE("Creating Default Texture");

        const uint32_t l_Pixel = 0xffffffff;
        VkDeviceSize l_ImageSize = sizeof(l_Pixel);

        VkBuffer l_StagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory l_StagingMemory = VK_NULL_HANDLE;
        m_Buffers.CreateBuffer(l_ImageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, l_StagingBuffer, l_StagingMemory);

        void* l_Data = nullptr;
        vkMapMemory(Application::GetDevice(), l_StagingMemory, 0, l_ImageSize, 0, &l_Data);
        memcpy(l_Data, &l_Pixel, static_cast<size_t>(l_ImageSize));
        vkUnmapMemory(Application::GetDevice(), l_StagingMemory);

        VkImageCreateInfo l_ImageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        l_ImageInfo.imageType = VK_IMAGE_TYPE_2D;
        l_ImageInfo.extent.width = 1;
        l_ImageInfo.extent.height = 1;
        l_ImageInfo.extent.depth = 1;
        l_ImageInfo.mipLevels = 1;
        l_ImageInfo.arrayLayers = 1;
        l_ImageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        l_ImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        l_ImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        l_ImageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        l_ImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        l_ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

        if (vkCreateImage(Application::GetDevice(), &l_ImageInfo, nullptr, &m_TextureImage) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create default texture image");
        }

        VkMemoryRequirements l_MemRequirements{};
        vkGetImageMemoryRequirements(Application::GetDevice(), m_TextureImage, &l_MemRequirements);

        VkMemoryAllocateInfo l_AllocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        l_AllocInfo.allocationSize = l_MemRequirements.size;
        l_AllocInfo.memoryTypeIndex = m_Buffers.FindMemoryType(l_MemRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(Application::GetDevice(), &l_AllocInfo, nullptr, &m_TextureImageMemory) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to allocate default texture memory");
        }

        vkBindImageMemory(Application::GetDevice(), m_TextureImage, m_TextureImageMemory, 0);

        VkCommandBuffer l_CommandBuffer = m_Commands.BeginSingleTimeCommands();

        VkImageMemoryBarrier l_BarrierToTransfer{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        l_BarrierToTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        l_BarrierToTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        l_BarrierToTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_BarrierToTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_BarrierToTransfer.image = m_TextureImage;
        l_BarrierToTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_BarrierToTransfer.subresourceRange.baseMipLevel = 0;
        l_BarrierToTransfer.subresourceRange.levelCount = 1;
        l_BarrierToTransfer.subresourceRange.baseArrayLayer = 0;
        l_BarrierToTransfer.subresourceRange.layerCount = 1;
        l_BarrierToTransfer.srcAccessMask = 0;
        l_BarrierToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(l_CommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &l_BarrierToTransfer);

        VkBufferImageCopy l_Region{};
        l_Region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_Region.imageSubresource.mipLevel = 0;
        l_Region.imageSubresource.baseArrayLayer = 0;
        l_Region.imageSubresource.layerCount = 1;
        l_Region.imageOffset = { 0, 0, 0 };
        l_Region.imageExtent = { 1, 1, 1 };

        vkCmdCopyBufferToImage(l_CommandBuffer, l_StagingBuffer, m_TextureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &l_Region);

        VkImageMemoryBarrier l_BarrierToShader{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        l_BarrierToShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        l_BarrierToShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        l_BarrierToShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_BarrierToShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_BarrierToShader.image = m_TextureImage;
        l_BarrierToShader.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_BarrierToShader.subresourceRange.baseMipLevel = 0;
        l_BarrierToShader.subresourceRange.levelCount = 1;
        l_BarrierToShader.subresourceRange.baseArrayLayer = 0;
        l_BarrierToShader.subresourceRange.layerCount = 1;
        l_BarrierToShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        l_BarrierToShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(l_CommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &l_BarrierToShader);

        m_Commands.EndSingleTimeCommands(l_CommandBuffer);

        m_Buffers.DestroyBuffer(l_StagingBuffer, l_StagingMemory);

        VkImageViewCreateInfo l_ViewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        l_ViewInfo.image = m_TextureImage;
        l_ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        l_ViewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        l_ViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_ViewInfo.subresourceRange.baseMipLevel = 0;
        l_ViewInfo.subresourceRange.levelCount = 1;
        l_ViewInfo.subresourceRange.baseArrayLayer = 0;
        l_ViewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(Application::GetDevice(), &l_ViewInfo, nullptr, &m_TextureImageView) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create texture image view");
        }

        VkSamplerCreateInfo l_SamplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        l_SamplerInfo.magFilter = VK_FILTER_LINEAR;
        l_SamplerInfo.minFilter = VK_FILTER_LINEAR;
        l_SamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        l_SamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        l_SamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        l_SamplerInfo.anisotropyEnable = VK_FALSE;
        l_SamplerInfo.maxAnisotropy = 1.0f;
        l_SamplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE;
        l_SamplerInfo.unnormalizedCoordinates = VK_FALSE;
        l_SamplerInfo.compareEnable = VK_FALSE;
        l_SamplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        l_SamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        l_SamplerInfo.mipLodBias = 0.0f;
        l_SamplerInfo.minLod = 0.0f;
        l_SamplerInfo.maxLod = 0.0f;

        if (vkCreateSampler(Application::GetDevice(), &l_SamplerInfo, nullptr, &m_TextureSampler) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create texture sampler");
        }

        TR_CORE_TRACE("Default Texture Created");
    }

    void Renderer::CreateDefaultSkybox()
    {
        TR_CORE_TRACE("Creating Default Skybox");

        m_Skybox.Init(m_Buffers, m_Commands.GetCommandPool());

        TR_CORE_TRACE("Default Skybox Created");
    }

    void Renderer::CreateDescriptorSets()
    {
        TR_CORE_TRACE("Allocating Descriptor Sets");

        size_t l_ImageCount = m_Swapchain.GetImageCount();

        std::vector<VkDescriptorSetLayout> l_Layouts(l_ImageCount, m_Pipeline.GetDescriptorSetLayout());

        VkDescriptorSetAllocateInfo l_AllocateInfo{};
        l_AllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        l_AllocateInfo.descriptorPool = m_DescriptorPool;
        l_AllocateInfo.descriptorSetCount = l_ImageCount;
        l_AllocateInfo.pSetLayouts = l_Layouts.data();

        m_DescriptorSets.resize(l_ImageCount);
        if (vkAllocateDescriptorSets(Application::GetDevice(), &l_AllocateInfo, m_DescriptorSets.data()) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to allocate descriptor sets");
        }

        for (size_t i = 0; i < l_ImageCount; ++i)
        {
            VkDescriptorBufferInfo l_BufferInfo{};
            l_BufferInfo.buffer = m_UniformBuffers[i];
            l_BufferInfo.offset = 0;
            l_BufferInfo.range = sizeof(UniformBufferObject);

            VkDescriptorImageInfo l_ImageInfo{};
            l_ImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            l_ImageInfo.imageView = m_TextureImageView;
            l_ImageInfo.sampler = m_TextureSampler;

            VkWriteDescriptorSet l_DescriptorWrite{};
            l_DescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            l_DescriptorWrite.dstSet = m_DescriptorSets[i];
            l_DescriptorWrite.dstBinding = 0;
            l_DescriptorWrite.dstArrayElement = 0;
            l_DescriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            l_DescriptorWrite.descriptorCount = 1;
            l_DescriptorWrite.pBufferInfo = &l_BufferInfo;

            VkWriteDescriptorSet l_ImageWrite{};
            l_ImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            l_ImageWrite.dstSet = m_DescriptorSets[i];
            l_ImageWrite.dstBinding = 1;
            l_ImageWrite.dstArrayElement = 0;
            l_ImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            l_ImageWrite.descriptorCount = 1;
            l_ImageWrite.pImageInfo = &l_ImageInfo;

            VkWriteDescriptorSet l_Writes[] = { l_DescriptorWrite, l_ImageWrite };
            vkUpdateDescriptorSets(Application::GetDevice(), 2, l_Writes, 0, nullptr);
        }

        TR_CORE_TRACE("Descriptor Sets Allocated ({})", l_ImageCount);
    }

    bool Renderer::AcquireNextImage(uint32_t& imageIndex, VkFence inFlightFence)
    {
        VkResult l_Result = vkAcquireNextImageKHR(Application::GetDevice(), m_Swapchain.GetSwapchain(), UINT64_MAX,
            m_Commands.GetImageAvailableSemaphorePerImage(m_Commands.CurrentFrame()), VK_NULL_HANDLE, &imageIndex);

        if (l_Result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            RecreateSwapchain();
        }

        else if (l_Result != VK_SUCCESS && l_Result != VK_SUBOPTIMAL_KHR)
        {
            TR_CORE_CRITICAL("Failed to acquire swap chain image!");

            return EXIT_FAILURE;
        }

        VkFence& l_ImageFence = m_Commands.GetImageInFlight(imageIndex);
        if (l_ImageFence != VK_NULL_HANDLE)
        {
            vkWaitForFences(Application::GetDevice(), 1, &l_ImageFence, VK_TRUE, UINT64_MAX);
        }

        m_Commands.SetImageInFlight(imageIndex, inFlightFence);

        return true;
    }

    bool Renderer::RecordCommandBuffer(uint32_t imageIndex)
    {
        VkCommandBuffer l_CommandBuffer = m_Commands.GetCommandBuffer(imageIndex);

        VkCommandBufferBeginInfo l_BeginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        vkBeginCommandBuffer(l_CommandBuffer, &l_BeginInfo);

        VkRenderPassBeginInfo l_RenderPassInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        l_RenderPassInfo.renderPass = m_Pipeline.GetRenderPass();
        l_RenderPassInfo.framebuffer = m_Pipeline.GetFramebuffers()[imageIndex];
        l_RenderPassInfo.renderArea.offset = { 0, 0 };
        l_RenderPassInfo.renderArea.extent = m_Swapchain.GetExtent();

        VkClearValue l_ClearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
        l_RenderPassInfo.clearValueCount = 1;
        l_RenderPassInfo.pClearValues = &l_ClearColor;

        vkCmdBeginRenderPass(l_CommandBuffer, &l_RenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport l_Viewport{};
        l_Viewport.x = 0.0f;
        l_Viewport.y = 0.0f;
        l_Viewport.width = static_cast<float>(m_Swapchain.GetExtent().width);
        l_Viewport.height = static_cast<float>(m_Swapchain.GetExtent().height);
        l_Viewport.minDepth = 0.0f;
        l_Viewport.maxDepth = 1.0f;
        vkCmdSetViewport(l_CommandBuffer, 0, 1, &l_Viewport);

        VkRect2D l_Scissor{};
        l_Scissor.offset = { 0, 0 };
        l_Scissor.extent = m_Swapchain.GetExtent();
        vkCmdSetScissor(l_CommandBuffer, 0, 1, &l_Scissor);

        vkCmdBindPipeline(l_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline.GetPipeline());
        
        // Draw skybox first
        m_Skybox.Record(l_CommandBuffer, m_Pipeline.GetPipelineLayout(), m_DescriptorSets.data(), imageIndex);

        if (m_VertexBuffer != VK_NULL_HANDLE && m_IndexBuffer != VK_NULL_HANDLE && m_IndexCount > 0)
        {
            VkBuffer l_VertexBuffers[] = { m_VertexBuffer };
            VkDeviceSize l_Offsets[] = { 0 };

            vkCmdBindVertexBuffers(l_CommandBuffer, 0, 1, l_VertexBuffers, l_Offsets);
            vkCmdBindIndexBuffer(l_CommandBuffer, m_IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdBindDescriptorSets(l_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline.GetPipelineLayout(), 0, 1, &m_DescriptorSets[imageIndex], 0, nullptr);

            glm::mat4 l_Transform = ComposeTransform(m_Transform);
            vkCmdPushConstants(l_CommandBuffer, m_Pipeline.GetPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &l_Transform);

            vkCmdDrawIndexed(l_CommandBuffer, m_IndexCount, 1, 0, 0, 0);
        }

        if (m_ImGuiLayer && IsValidViewport())
        {
            m_ImGuiLayer->Render(l_CommandBuffer);
        }

        vkCmdEndRenderPass(l_CommandBuffer);

        VkImageMemoryBarrier l_BarrierEnd{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        l_BarrierEnd.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_BarrierEnd.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_BarrierEnd.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_BarrierEnd.subresourceRange.baseMipLevel = 0;
        l_BarrierEnd.subresourceRange.levelCount = 1;
        l_BarrierEnd.subresourceRange.baseArrayLayer = 0;
        l_BarrierEnd.subresourceRange.layerCount = 1;
        l_BarrierEnd.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        if (IsValidViewport())
        {
            l_BarrierEnd.image = m_OffscreenImage;
            l_BarrierEnd.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            l_BarrierEnd.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            l_BarrierEnd.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(l_CommandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &l_BarrierEnd);

            l_RenderPassInfo.framebuffer = m_Pipeline.GetFramebuffers()[imageIndex];
            l_RenderPassInfo.renderArea.offset = { 0, 0 };
            l_RenderPassInfo.renderArea.extent = m_Swapchain.GetExtent();
            vkCmdBeginRenderPass(l_CommandBuffer, &l_RenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

            VkViewport l_UIViewport{};
            l_UIViewport.x = 0.0f;
            l_UIViewport.y = 0.0f;
            l_UIViewport.width = static_cast<float>(m_Swapchain.GetExtent().width);
            l_UIViewport.height = static_cast<float>(m_Swapchain.GetExtent().height);
            l_UIViewport.minDepth = 0.0f;
            l_UIViewport.maxDepth = 1.0f;
            vkCmdSetViewport(l_CommandBuffer, 0, 1, &l_UIViewport);

            VkRect2D l_UIScissor{};
            l_UIScissor.offset = { 0, 0 };
            l_UIScissor.extent = m_Swapchain.GetExtent();
            vkCmdSetScissor(l_CommandBuffer, 0, 1, &l_UIScissor);

            if (m_ImGuiLayer)
            {
                m_ImGuiLayer->Render(l_CommandBuffer);
            }

            vkCmdEndRenderPass(l_CommandBuffer);

            l_BarrierEnd.image = m_Swapchain.GetImages()[imageIndex];
            l_BarrierEnd.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            l_BarrierEnd.dstAccessMask = 0;
            l_BarrierEnd.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            vkCmdPipelineBarrier(l_CommandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &l_BarrierEnd);
        }
        else
        {
            l_BarrierEnd.image = m_Swapchain.GetImages()[imageIndex];
            l_BarrierEnd.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            l_BarrierEnd.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            l_BarrierEnd.dstAccessMask = 0;
            vkCmdPipelineBarrier(l_CommandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &l_BarrierEnd);
        }

        if (vkEndCommandBuffer(l_CommandBuffer) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to record command buffer!");

            return EXIT_FAILURE;
        }

        return true;
    }

    bool Renderer::SubmitFrame(uint32_t imageIndex, VkFence inFlightFence)
    {
        VkCommandBuffer l_CommandBuffer = m_Commands.GetCommandBuffer(imageIndex);

        VkSemaphore l_WaitSemaphores[] = { m_Commands.GetImageAvailableSemaphorePerImage(m_Commands.CurrentFrame()) };
        VkPipelineStageFlags l_WaitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        VkSemaphore l_SignalSemaphores[] = { m_Commands.GetRenderFinishedSemaphorePerImage(imageIndex) };

        VkSubmitInfo l_SubmitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        l_SubmitInfo.waitSemaphoreCount = 1;
        l_SubmitInfo.pWaitSemaphores = l_WaitSemaphores;
        l_SubmitInfo.pWaitDstStageMask = l_WaitStages;
        l_SubmitInfo.commandBufferCount = 1;
        l_SubmitInfo.pCommandBuffers = &l_CommandBuffer;
        l_SubmitInfo.signalSemaphoreCount = 1;
        l_SubmitInfo.pSignalSemaphores = l_SignalSemaphores;

        if (vkQueueSubmit(Application::GetGraphicsQueue(), 1, &l_SubmitInfo, inFlightFence) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to submit draw command buffer!");

            return EXIT_FAILURE;
        }

        if (vkGetFenceStatus(Application::GetDevice(), m_ResourceFence) == VK_NOT_READY)
        {
            vkWaitForFences(Application::GetDevice(), 1, &m_ResourceFence, VK_TRUE, UINT64_MAX);
        }

        vkResetFences(Application::GetDevice(), 1, &m_ResourceFence);
        VkSubmitInfo l_FenceSubmit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        l_FenceSubmit.commandBufferCount = 0;
        if (vkQueueSubmit(Application::GetGraphicsQueue(), 1, &l_FenceSubmit, m_ResourceFence) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to submit resource fence");
        }

        return true;
    }

    void Renderer::PresentFrame(uint32_t imageIndex)
    {
        VkSemaphore l_SignalSemaphores[] = { m_Commands.GetRenderFinishedSemaphorePerImage(imageIndex) };

        VkPresentInfoKHR l_PresentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        l_PresentInfo.waitSemaphoreCount = 1;
        l_PresentInfo.pWaitSemaphores = l_SignalSemaphores;

        VkSwapchainKHR l_Swapchains[] = { m_Swapchain.GetSwapchain() };
        l_PresentInfo.swapchainCount = 1;
        l_PresentInfo.pSwapchains = l_Swapchains;
        l_PresentInfo.pImageIndices = &imageIndex;

        VkResult l_PresentResult = vkQueuePresentKHR(Application::GetPresentQueue(), &l_PresentInfo);

        if (l_PresentResult == VK_ERROR_OUT_OF_DATE_KHR || l_PresentResult == VK_SUBOPTIMAL_KHR)
        {
            RecreateSwapchain();
        }

        else if (l_PresentResult != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to present swap chain image!");
        }
    }

    void Renderer::UpdateUniformBuffer(uint32_t currentImage)
    {
        UniformBufferObject l_UBO{};

        l_UBO.View = m_Camera.GetViewMatrix();

        float l_AspectRatio = static_cast<float>(m_Swapchain.GetExtent().width) / static_cast<float>(m_Swapchain.GetExtent().height);
        l_UBO.Projection = glm::perspective(glm::radians(m_Camera.GetFOV()), l_AspectRatio, m_Camera.GetNearClip(), m_Camera.GetFarClip());
        l_UBO.Projection[1][1] *= -1.0f;

        void* l_Data = nullptr;
        vkMapMemory(Application::GetDevice(), m_UniformBuffersMemory[currentImage], 0, sizeof(l_UBO), 0, &l_Data);
        memcpy(l_Data, &l_UBO, sizeof(l_UBO));
        vkUnmapMemory(Application::GetDevice(), m_UniformBuffersMemory[currentImage]);
    }
}