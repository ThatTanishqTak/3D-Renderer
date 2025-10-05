#include "Renderer/Renderer.h"

#include "Application.h"

#include "Geometry/Mesh.h"
#include "UI/ImGuiLayer.h"
#include "Core/Utilities.h"
#include "Loader/ModelLoader.h"
#include "Loader/TextureLoader.h"

#include <stdexcept>
#include <algorithm>
#include <string>
#include <chrono>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <limits>
#include <ctime>
#include <system_error>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <imgui_impl_vulkan.h>

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

    std::tm ToLocalTime(std::time_t a_Time)
    {
        std::tm l_LocalTime{};
#ifdef _WIN32
        localtime_s(&l_LocalTime, &a_Time);
#else
        localtime_r(&a_Time, &l_LocalTime);
#endif
        return l_LocalTime;
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

        m_Registry = &Application::GetRegistry();
        m_Entity = m_Registry->CreateEntity();
        m_Registry->AddComponent<Transform>(m_Entity);

        m_Swapchain.Init();
        // Reset the cached swapchain image layouts so new back buffers start from a known undefined state.
        m_SwapchainImageLayouts.assign(m_Swapchain.GetImageCount(), VK_IMAGE_LAYOUT_UNDEFINED);
        m_SwapchainDepthLayouts.assign(m_Swapchain.GetImageCount(), VK_IMAGE_LAYOUT_UNDEFINED);
        m_Pipeline.Init(m_Swapchain);
        m_Commands.Init(m_Swapchain.GetImageCount());

        // Pre-size the performance history buffer so we can efficiently track frame timings.
        m_PerformanceHistory.clear();
        m_PerformanceHistory.resize(s_PerformanceHistorySize);
        m_PerformanceHistoryNextIndex = 0;
        m_PerformanceSampleCount = 0;
        m_PerformanceStats = {};

        VkDeviceSize l_GlobalSize = sizeof(GlobalUniformBuffer);
        VkDeviceSize l_MaterialSize = sizeof(MaterialUniformBuffer);

        // Allocate per-frame uniform buffers for camera/light and material state.
        m_Buffers.CreateUniformBuffers(m_Swapchain.GetImageCount(), l_GlobalSize, m_GlobalUniformBuffers, m_GlobalUniformBuffersMemory);
        m_Buffers.CreateUniformBuffers(m_Swapchain.GetImageCount(), l_MaterialSize, m_MaterialUniformBuffers, m_MaterialUniformBuffersMemory);

        CreateDescriptorPool();
        CreateDefaultTexture();
        CreateDefaultSkybox();
        CreateDescriptorSets();

        m_Camera = Camera(Application::GetWindow().GetNativeWindow());

        m_Viewport.Position = { 0.0f, 0.0f };
        m_Viewport.Size = { static_cast<float>(m_Swapchain.GetExtent().width), static_cast<float>(m_Swapchain.GetExtent().height) };
        m_Viewport.ViewportID = 0;
        m_ActiveViewportId = 0;

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

        // Tear down any editor viewport resources before the core pipeline disappears.
        DestroyAllOffscreenResources();

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
        m_GlobalUniformBuffers.clear();
        m_GlobalUniformBuffersMemory.clear();
        m_MaterialUniformBuffers.clear();
        m_MaterialUniformBuffersMemory.clear();

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
        const auto l_FrameStartTime = std::chrono::steady_clock::now();
        const auto l_FrameWallClock = std::chrono::system_clock::now();
        VkExtent2D l_FrameExtent{ 0, 0 };

        Utilities::Allocation::ResetFrame();
        ProcessReloadEvents();
        m_Camera.Update(Utilities::Time::GetDeltaTime());

        // Allow developers to tweak GLSL and get instant feedback without restarting the app.
        if (m_Pipeline.ReloadIfNeeded(m_Swapchain))
        {
            TR_CORE_INFO("Graphics pipeline reloaded after shader edit");
        }

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

        // Capture the extent actually used when recording commands so our metrics reflect the final render target dimensions.
        l_FrameExtent = m_Swapchain.GetExtent();

        if (!SubmitFrame(l_ImageIndex, l_InFlightFence))
        {
            TR_CORE_CRITICAL("Failed to submit frame");

            return;
        }

        PresentFrame(l_ImageIndex);

        m_Commands.CurrentFrame() = (m_Commands.CurrentFrame() + 1) % m_Commands.GetFrameCount();
        m_FrameAllocationCount = Utilities::Allocation::GetFrameCount();
        
        //TR_CORE_TRACE("Frame allocations: {}", m_FrameAllocationCount);

        const auto l_FrameEndTime = std::chrono::steady_clock::now();
        const double l_FrameMilliseconds = std::chrono::duration<double, std::milli>(l_FrameEndTime - l_FrameStartTime).count();
        const double l_FrameFPS = l_FrameMilliseconds > 0.0 ? 1000.0 / l_FrameMilliseconds : 0.0;
        AccumulateFrameTiming(l_FrameMilliseconds, l_FrameFPS, l_FrameExtent, l_FrameWallClock);
    }

    void Renderer::UploadMesh(const std::vector<Geometry::Mesh>& meshes, const std::vector<Geometry::Material>& materials)
    {
        // Ensure no GPU operations are using the old buffers
        vkWaitForFences(Application::GetDevice(), 1, &m_ResourceFence, VK_TRUE, UINT64_MAX);

        // Cache the material table so that future shading passes can evaluate PBR parameters
        m_Materials = materials;

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

        size_t l_VertexCount = 0;
        size_t l_IndexCount = 0;
        for (const auto& l_Mesh : meshes)
        {
            l_VertexCount += l_Mesh.Vertices.size();
            l_IndexCount += l_Mesh.Indices.size();
        }

        if (l_VertexCount > m_MaxVertexCount)
        {
            m_MaxVertexCount = l_VertexCount;
            m_StagingVertices.reset(new Vertex[m_MaxVertexCount]);
        }
        if (l_IndexCount > m_MaxIndexCount)
        {
            m_MaxIndexCount = l_IndexCount;
            m_StagingIndices.reset(new uint32_t[m_MaxIndexCount]);
        }

        uint32_t l_Offset = 0;
        size_t l_VertOffset = 0;
        size_t l_IndexOffset = 0;
        for (const auto& l_Mesh : meshes)
        {
            std::copy(l_Mesh.Vertices.begin(), l_Mesh.Vertices.end(), m_StagingVertices.get() + l_VertOffset);
            for (auto index : l_Mesh.Indices)
            {
                m_StagingIndices[l_IndexOffset++] = index + l_Offset;
            }
            l_VertOffset += l_Mesh.Vertices.size();
            l_Offset += static_cast<uint32_t>(l_Mesh.Vertices.size());
        }

        std::vector<Vertex> l_AllVertices(m_StagingVertices.get(), m_StagingVertices.get() + l_VertexCount);
        std::vector<uint32_t> l_AllIndices(m_StagingIndices.get(), m_StagingIndices.get() + l_IndexCount);

        // Upload the combined geometry once per load so every mesh in the scene shares a single draw call.
        m_Buffers.CreateVertexBuffer(l_AllVertices, m_Commands.GetOneTimePool(), m_VertexBuffer, m_VertexBufferMemory);
        m_Buffers.CreateIndexBuffer(l_AllIndices, m_Commands.GetOneTimePool(), m_IndexBuffer, m_IndexBufferMemory, m_IndexCount);

        // Record the uploaded index count so the command buffer draw guard can validate pending draws.
        m_IndexCount = static_cast<uint32_t>(l_IndexCount);

        m_ModelCount = meshes.size();
        m_TriangleCount = l_IndexCount / 3;

        TR_CORE_INFO("Scene info - Models: {} Triangles: {} Materials: {}", m_ModelCount, m_TriangleCount, m_Materials.size());
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
            l_ImageWrite.dstBinding = 2;
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

    void Renderer::SetClearColor(const glm::vec4& color)
    {
        // Persist the preferred clear colour so both render passes remain visually consistent.
        m_ClearColor = color;
    }

    VkDescriptorSet Renderer::GetViewportTexture() const
    {
        // Provide the descriptor set that ImGui::Image expects when the viewport is active.
        // The actual colour output inside this texture is driven by the camera selected via RenderCommand::SetViewportCamera.
        if (!IsValidViewport())
        {
            return VK_NULL_HANDLE;
        }

        const uint32_t l_ViewportId = m_Viewport.ViewportID;
        const auto it_Target = m_OffscreenTargets.find(l_ViewportId);
        if (it_Target == m_OffscreenTargets.end())
        {
            return VK_NULL_HANDLE;
        }

        const OffscreenTarget& l_Target = it_Target->second;
        if (l_Target.m_TextureID != VK_NULL_HANDLE)
        {
            return l_Target.m_TextureID;
        }

        return VK_NULL_HANDLE;
    }

    void Renderer::SetViewport(const ViewportInfo& info)
    {
        const uint32_t l_PreviousViewportId = m_ActiveViewportId;
        m_Viewport = info;
        m_ActiveViewportId = info.ViewportID;

        if (!IsValidViewport())
        {
            // The viewport was closed or minimized, so free the auxiliary render target when possible.
            DestroyOffscreenResources(l_PreviousViewportId);

            return;
        }

        VkExtent2D l_RequestedExtent{};
        l_RequestedExtent.width = static_cast<uint32_t>(std::max(info.Size.x, 0.0f));
        l_RequestedExtent.height = static_cast<uint32_t>(std::max(info.Size.y, 0.0f));

        if (l_RequestedExtent.width == 0 || l_RequestedExtent.height == 0)
        {
            DestroyOffscreenResources(m_ActiveViewportId);

            return;
        }

        OffscreenTarget& l_Target = GetOrCreateOffscreenTarget(m_ActiveViewportId);
        if (l_Target.m_Extent.width == l_RequestedExtent.width && l_Target.m_Extent.height == l_RequestedExtent.height)
        {
            // Nothing to do – the backing image already matches the requested size.
            return;
        }

        CreateOrResizeOffscreenResources(l_Target, l_RequestedExtent);

        // Future: consider pooling and recycling detached targets so background viewports can warm-start when reopened.
    }

    void Renderer::SetViewportCamera(ECS::Entity cameraEntity)
    {
        // The UI layer forwards its selection through RenderCommand so the renderer can resolve the correct camera per frame.
        m_ViewportCamera = cameraEntity;
    }

    Renderer::CameraSnapshot Renderer::ResolveViewportCamera() const
    {
        CameraSnapshot l_Snapshot{};
        l_Snapshot.View = m_Camera.GetViewMatrix();
        l_Snapshot.Position = m_Camera.GetPosition();
        l_Snapshot.FieldOfView = m_Camera.GetFOV();
        l_Snapshot.NearClip = m_Camera.GetNearClip();
        l_Snapshot.FarClip = m_Camera.GetFarClip();

        if (m_ViewportCamera == std::numeric_limits<ECS::Entity>::max() || m_Registry == nullptr)
        {
            return l_Snapshot;
        }

        if (!m_Registry->HasComponent<CameraComponent>(m_ViewportCamera) || !m_Registry->HasComponent<Transform>(m_ViewportCamera))
        {
            return l_Snapshot;
        }

        const CameraComponent& l_CameraComponent = m_Registry->GetComponent<CameraComponent>(m_ViewportCamera);
        const Transform& l_Transform = m_Registry->GetComponent<Transform>(m_ViewportCamera);

        const glm::mat4 l_ModelMatrix = ComposeTransform(l_Transform);
        const glm::mat4 l_ViewMatrix = glm::inverse(l_ModelMatrix);

        l_Snapshot.View = l_ViewMatrix;
        l_Snapshot.Position = l_Transform.Position;
        l_Snapshot.FieldOfView = l_CameraComponent.FieldOfView;
        l_Snapshot.NearClip = l_CameraComponent.NearClip;
        l_Snapshot.FarClip = l_CameraComponent.FarClip;

        return l_Snapshot;
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
        // Whenever the swapchain rebuilds, reset the cached layouts because new images arrive in an undefined state.
        m_SwapchainImageLayouts.assign(m_Swapchain.GetImageCount(), VK_IMAGE_LAYOUT_UNDEFINED);
        m_SwapchainDepthLayouts.assign(m_Swapchain.GetImageCount(), VK_IMAGE_LAYOUT_UNDEFINED);

        // Rebuild the swapchain-backed framebuffers so that they point at the freshly created images.
        m_Pipeline.RecreateFramebuffers(m_Swapchain);

        uint32_t l_ImageCount = m_Swapchain.GetImageCount();
        if (m_Commands.GetFrameCount() != l_ImageCount)
        {
            // Command buffers and synchronization objects are per swapchain image, so rebuild them if the count changed.
            TR_CORE_TRACE("Resizing command resources (Old = {}, New = {})", m_Commands.GetFrameCount(), l_ImageCount);
            m_Commands.Recreate(l_ImageCount);
        }

        if (l_ImageCount != m_GlobalUniformBuffers.size())
        {
            // We have a different swapchain image count, so destroy and rebuild any per-frame resources.
            for (size_t i = 0; i < m_GlobalUniformBuffers.size(); ++i)
            {
                m_Buffers.DestroyBuffer(m_GlobalUniformBuffers[i], m_GlobalUniformBuffersMemory[i]);
            }

            for (size_t i = 0; i < m_MaterialUniformBuffers.size(); ++i)
            {
                m_Buffers.DestroyBuffer(m_MaterialUniformBuffers[i], m_MaterialUniformBuffersMemory[i]);
            }

            if (!m_DescriptorSets.empty())
            {
                // Free descriptor sets from the old pool so we can rebuild them cleanly.
                vkFreeDescriptorSets(Application::GetDevice(), m_DescriptorPool, static_cast<uint32_t>(m_DescriptorSets.size()), m_DescriptorSets.data());
                m_DescriptorSets.clear();
            }

            if (m_DescriptorPool != VK_NULL_HANDLE)
            {
                // Tear down the descriptor pool so that we can rebuild it with the new descriptor counts.
                vkDestroyDescriptorPool(Application::GetDevice(), m_DescriptorPool, nullptr);
                m_DescriptorPool = VK_NULL_HANDLE;
            }

            m_GlobalUniformBuffers.clear();
            m_GlobalUniformBuffersMemory.clear();
            m_MaterialUniformBuffers.clear();
            m_MaterialUniformBuffersMemory.clear();

            VkDeviceSize l_GlobalSize = sizeof(GlobalUniformBuffer);
            VkDeviceSize l_MaterialSize = sizeof(MaterialUniformBuffer);
            
            m_Buffers.CreateUniformBuffers(l_ImageCount, l_GlobalSize, m_GlobalUniformBuffers, m_GlobalUniformBuffersMemory);
            m_Buffers.CreateUniformBuffers(l_ImageCount, l_MaterialSize, m_MaterialUniformBuffers, m_MaterialUniformBuffersMemory);
            
            // Recreate the descriptor pool before allocating descriptor sets so the pool matches the new swapchain image count.
            CreateDescriptorPool();
            CreateDescriptorSets();
            
            TR_CORE_TRACE("Descriptor resources recreated (SwapchainImages = {}, GlobalUBOs = {}, MaterialUBOs = {}, CombinedSamplers = {}, DescriptorSets = {})",
                l_ImageCount, m_GlobalUniformBuffers.size(), m_MaterialUniformBuffers.size(), l_ImageCount, m_DescriptorSets.size());
        }

        if (IsValidViewport() && m_ActiveViewportId != 0)
        {
            VkExtent2D l_ViewportExtent{};
            l_ViewportExtent.width = static_cast<uint32_t>(std::max(m_Viewport.Size.x, 0.0f));
            l_ViewportExtent.height = static_cast<uint32_t>(std::max(m_Viewport.Size.y, 0.0f));

            if (l_ViewportExtent.width > 0 && l_ViewportExtent.height > 0)
            {
                OffscreenTarget& l_Target = GetOrCreateOffscreenTarget(m_ActiveViewportId);
                CreateOrResizeOffscreenResources(l_Target, l_ViewportExtent);
            }
            else
            {
                DestroyOffscreenResources(m_ActiveViewportId);
            }
        }
        else if (m_ActiveViewportId != 0)
        {
            DestroyOffscreenResources(m_ActiveViewportId);
        }
    }

    //------------------------------------------------------------------------------------------------------------------------------------------------------//

    void Renderer::CreateDescriptorPool()
    {
        TR_CORE_TRACE("Creating Descriptor Pool");

        VkDescriptorPoolSize l_PoolSizes[3]{};
        l_PoolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        l_PoolSizes[0].descriptorCount = m_Swapchain.GetImageCount();
        l_PoolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        l_PoolSizes[1].descriptorCount = m_Swapchain.GetImageCount();
        l_PoolSizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        l_PoolSizes[2].descriptorCount = m_Swapchain.GetImageCount();

        VkDescriptorPoolCreateInfo l_PoolInfo{};
        l_PoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        // We free and recreate descriptor sets whenever the swapchain is resized, so enable free-descriptor support.
        l_PoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        l_PoolInfo.poolSizeCount = 3;
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

        m_Skybox.Init(m_Buffers, m_Commands.GetOneTimePool());

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
            VkDescriptorBufferInfo l_GlobalBufferInfo{};
            l_GlobalBufferInfo.buffer = m_GlobalUniformBuffers[i];
            l_GlobalBufferInfo.offset = 0;
            l_GlobalBufferInfo.range = sizeof(GlobalUniformBuffer);

            VkDescriptorBufferInfo l_MaterialBufferInfo{};
            l_MaterialBufferInfo.buffer = m_MaterialUniformBuffers[i];
            l_MaterialBufferInfo.offset = 0;
            l_MaterialBufferInfo.range = sizeof(MaterialUniformBuffer);

            VkDescriptorImageInfo l_ImageInfo{};
            l_ImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            l_ImageInfo.imageView = m_TextureImageView;
            l_ImageInfo.sampler = m_TextureSampler;

            VkWriteDescriptorSet l_GlobalWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            l_GlobalWrite.dstSet = m_DescriptorSets[i];
            l_GlobalWrite.dstBinding = 0;
            l_GlobalWrite.dstArrayElement = 0;
            l_GlobalWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            l_GlobalWrite.descriptorCount = 1;
            l_GlobalWrite.pBufferInfo = &l_GlobalBufferInfo;

            VkWriteDescriptorSet l_MaterialWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            l_MaterialWrite.dstSet = m_DescriptorSets[i];
            l_MaterialWrite.dstBinding = 1;
            l_MaterialWrite.dstArrayElement = 0;
            l_MaterialWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            l_MaterialWrite.descriptorCount = 1;
            l_MaterialWrite.pBufferInfo = &l_MaterialBufferInfo;

            VkWriteDescriptorSet l_ImageWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            l_ImageWrite.dstSet = m_DescriptorSets[i];
            l_ImageWrite.dstBinding = 2;
            l_ImageWrite.dstArrayElement = 0;
            l_ImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            l_ImageWrite.descriptorCount = 1;
            l_ImageWrite.pImageInfo = &l_ImageInfo;

            VkWriteDescriptorSet l_Writes[] = { l_GlobalWrite, l_MaterialWrite, l_ImageWrite };
            vkUpdateDescriptorSets(Application::GetDevice(), 3, l_Writes, 0, nullptr);
        }

        TR_CORE_TRACE("Descriptor Sets Allocated ({})", l_ImageCount);
    }

    void Renderer::DestroyOffscreenResources(uint32_t viewportId)
    {
        if (viewportId == 0)
        {
            return;
        }

        const auto it_Target = m_OffscreenTargets.find(viewportId);
        if (it_Target == m_OffscreenTargets.end())
        {
            return;
        }

        VkDevice l_Device = Application::GetDevice();
        OffscreenTarget& l_Target = it_Target->second;

        // The renderer owns these handles; releasing them here avoids dangling ImGui descriptors or image memory leaks.
        if (l_Target.m_TextureID != VK_NULL_HANDLE)
        {
            ImGui_ImplVulkan_RemoveTexture(l_Target.m_TextureID);
            l_Target.m_TextureID = VK_NULL_HANDLE;
        }

        if (l_Target.m_Framebuffer != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(l_Device, l_Target.m_Framebuffer, nullptr);
            l_Target.m_Framebuffer = VK_NULL_HANDLE;
        }

        if (l_Target.m_DepthView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(l_Device, l_Target.m_DepthView, nullptr);
            l_Target.m_DepthView = VK_NULL_HANDLE;
        }

        if (l_Target.m_ImageView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(l_Device, l_Target.m_ImageView, nullptr);
            l_Target.m_ImageView = VK_NULL_HANDLE;
        }

        if (l_Target.m_DepthImage != VK_NULL_HANDLE)
        {
            vkDestroyImage(l_Device, l_Target.m_DepthImage, nullptr);
            l_Target.m_DepthImage = VK_NULL_HANDLE;
        }

        if (l_Target.m_Image != VK_NULL_HANDLE)
        {
            vkDestroyImage(l_Device, l_Target.m_Image, nullptr);
            l_Target.m_Image = VK_NULL_HANDLE;
        }
        if (l_Target.m_DepthMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(l_Device, l_Target.m_DepthMemory, nullptr);
            l_Target.m_DepthMemory = VK_NULL_HANDLE;
        }

        if (l_Target.m_Memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(l_Device, l_Target.m_Memory, nullptr);
            l_Target.m_Memory = VK_NULL_HANDLE;
        }

        if (l_Target.m_Sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(l_Device, l_Target.m_Sampler, nullptr);
            l_Target.m_Sampler = VK_NULL_HANDLE;
        }

        l_Target.m_Extent = { 0, 0 };
        l_Target.m_CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        l_Target.m_DepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        m_OffscreenTargets.erase(it_Target);
    }

    void Renderer::DestroyAllOffscreenResources()
    {
        // Iterate carefully so erasing entries mid-loop remains valid across MSVC/STL implementations.
        auto it_Target = m_OffscreenTargets.begin();
        while (it_Target != m_OffscreenTargets.end())
        {
            const uint32_t l_ViewportId = it_Target->first;
            ++it_Target;
            DestroyOffscreenResources(l_ViewportId);
        }

        // Future: consider retaining unused targets in a pool so secondary editor panels can resume instantly.
        m_ActiveViewportId = 0;
    }

    Renderer::OffscreenTarget& Renderer::GetOrCreateOffscreenTarget(uint32_t viewportId)
    {
        auto [it_Target, l_Inserted] = m_OffscreenTargets.try_emplace(viewportId);
        OffscreenTarget& l_Target = it_Target->second;

        if (l_Inserted)
        {
            // New viewport render targets default to a clean state until the first resize allocates GPU memory.
            l_Target.m_Extent = { 0, 0 };
        }

        return l_Target;
    }

    void Renderer::CreateOrResizeOffscreenResources(OffscreenTarget& target, VkExtent2D extent)
    {
        VkDevice l_Device = Application::GetDevice();

        // Ensure the GPU is idle before we reuse or release any image memory.
        vkDeviceWaitIdle(l_Device);

        auto a_ResetTarget = [l_Device](OffscreenTarget& a_Target)
            {
                if (a_Target.m_TextureID != VK_NULL_HANDLE)
                {
                    ImGui_ImplVulkan_RemoveTexture(a_Target.m_TextureID);
                    a_Target.m_TextureID = VK_NULL_HANDLE;
                }

                if (a_Target.m_Framebuffer != VK_NULL_HANDLE)
                {
                    vkDestroyFramebuffer(l_Device, a_Target.m_Framebuffer, nullptr);
                    a_Target.m_Framebuffer = VK_NULL_HANDLE;
                }

                if (a_Target.m_DepthView != VK_NULL_HANDLE)
                {
                    vkDestroyImageView(l_Device, a_Target.m_DepthView, nullptr);
                    a_Target.m_DepthView = VK_NULL_HANDLE;
                }

                if (a_Target.m_ImageView != VK_NULL_HANDLE)
                {
                    vkDestroyImageView(l_Device, a_Target.m_ImageView, nullptr);
                    a_Target.m_ImageView = VK_NULL_HANDLE;
                }

                if (a_Target.m_DepthImage != VK_NULL_HANDLE)
                {
                    vkDestroyImage(l_Device, a_Target.m_DepthImage, nullptr);
                    a_Target.m_DepthImage = VK_NULL_HANDLE;
                }

                if (a_Target.m_Image != VK_NULL_HANDLE)
                {
                    vkDestroyImage(l_Device, a_Target.m_Image, nullptr);
                    a_Target.m_Image = VK_NULL_HANDLE;
                }

                if (a_Target.m_DepthMemory != VK_NULL_HANDLE)
                {
                    vkFreeMemory(l_Device, a_Target.m_DepthMemory, nullptr);
                    a_Target.m_DepthMemory = VK_NULL_HANDLE;
                }

                if (a_Target.m_Memory != VK_NULL_HANDLE)
                {
                    vkFreeMemory(l_Device, a_Target.m_Memory, nullptr);
                    a_Target.m_Memory = VK_NULL_HANDLE;
                }

                if (a_Target.m_Sampler != VK_NULL_HANDLE)
                {
                    vkDestroySampler(l_Device, a_Target.m_Sampler, nullptr);
                    a_Target.m_Sampler = VK_NULL_HANDLE;
                }

                a_Target.m_Extent = { 0, 0 };
                a_Target.m_CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                a_Target.m_DepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            };

        a_ResetTarget(target);

        if (extent.width == 0 || extent.height == 0)
        {
            return;
        }

        VkImageCreateInfo l_ImageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        l_ImageInfo.imageType = VK_IMAGE_TYPE_2D;
        l_ImageInfo.extent.width = extent.width;
        l_ImageInfo.extent.height = extent.height;
        l_ImageInfo.extent.depth = 1;
        l_ImageInfo.mipLevels = 1;
        l_ImageInfo.arrayLayers = 1;
        l_ImageInfo.format = m_Swapchain.GetImageFormat();
        l_ImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        l_ImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        l_ImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        l_ImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        l_ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

        if (vkCreateImage(l_Device, &l_ImageInfo, nullptr, &target.m_Image) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create offscreen image");

            a_ResetTarget(target);

            return;
        }

        VkMemoryRequirements l_MemoryRequirements{};
        vkGetImageMemoryRequirements(l_Device, target.m_Image, &l_MemoryRequirements);

        VkMemoryAllocateInfo l_AllocateInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        l_AllocateInfo.allocationSize = l_MemoryRequirements.size;
        l_AllocateInfo.memoryTypeIndex = m_Buffers.FindMemoryType(l_MemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(l_Device, &l_AllocateInfo, nullptr, &target.m_Memory) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to allocate offscreen image memory");

            a_ResetTarget(target);

            return;
        }

        vkBindImageMemory(l_Device, target.m_Image, target.m_Memory, 0);

        VkImageViewCreateInfo l_ViewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        l_ViewInfo.image = target.m_Image;
        l_ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        l_ViewInfo.format = l_ImageInfo.format;
        l_ViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_ViewInfo.subresourceRange.baseMipLevel = 0;
        l_ViewInfo.subresourceRange.levelCount = 1;
        l_ViewInfo.subresourceRange.baseArrayLayer = 0;
        l_ViewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(l_Device, &l_ViewInfo, nullptr, &target.m_ImageView) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create offscreen image view");

            a_ResetTarget(target);

            return;
        }

        // Mirror the swapchain depth handling so editor viewports respect the same occlusion rules.
        VkImageCreateInfo l_DepthInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        l_DepthInfo.imageType = VK_IMAGE_TYPE_2D;
        l_DepthInfo.extent.width = extent.width;
        l_DepthInfo.extent.height = extent.height;
        l_DepthInfo.extent.depth = 1;
        l_DepthInfo.mipLevels = 1;
        l_DepthInfo.arrayLayers = 1;
        l_DepthInfo.format = m_Pipeline.GetDepthFormat();
        l_DepthInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        l_DepthInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        l_DepthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        l_DepthInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        l_DepthInfo.samples = VK_SAMPLE_COUNT_1_BIT;

        if (vkCreateImage(l_Device, &l_DepthInfo, nullptr, &target.m_DepthImage) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create offscreen depth image");

            a_ResetTarget(target);

            return;
        }

        VkMemoryRequirements l_DepthRequirements{};
        vkGetImageMemoryRequirements(l_Device, target.m_DepthImage, &l_DepthRequirements);

        VkMemoryAllocateInfo l_DepthAllocate{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        l_DepthAllocate.allocationSize = l_DepthRequirements.size;
        l_DepthAllocate.memoryTypeIndex = m_Buffers.FindMemoryType(l_DepthRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(l_Device, &l_DepthAllocate, nullptr, &target.m_DepthMemory) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to allocate offscreen depth memory");

            a_ResetTarget(target);

            return;
        }

        vkBindImageMemory(l_Device, target.m_DepthImage, target.m_DepthMemory, 0);

        VkImageViewCreateInfo l_DepthViewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        l_DepthViewInfo.image = target.m_DepthImage;
        l_DepthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        l_DepthViewInfo.format = l_DepthInfo.format;
        l_DepthViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        l_DepthViewInfo.subresourceRange.baseMipLevel = 0;
        l_DepthViewInfo.subresourceRange.levelCount = 1;
        l_DepthViewInfo.subresourceRange.baseArrayLayer = 0;
        l_DepthViewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(l_Device, &l_DepthViewInfo, nullptr, &target.m_DepthView) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create offscreen depth view");

            a_ResetTarget(target);

            return;
        }

        std::array<VkImageView, 2> l_FramebufferAttachments{ target.m_ImageView, target.m_DepthView };
        VkFramebufferCreateInfo l_FramebufferInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        l_FramebufferInfo.renderPass = m_Pipeline.GetRenderPass();
        l_FramebufferInfo.attachmentCount = static_cast<uint32_t>(l_FramebufferAttachments.size());
        l_FramebufferInfo.pAttachments = l_FramebufferAttachments.data();
        l_FramebufferInfo.width = extent.width;
        l_FramebufferInfo.height = extent.height;
        l_FramebufferInfo.layers = 1;

        if (vkCreateFramebuffer(l_Device, &l_FramebufferInfo, nullptr, &target.m_Framebuffer) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create offscreen framebuffer");

            a_ResetTarget(target);

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
        l_SamplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        l_SamplerInfo.unnormalizedCoordinates = VK_FALSE;
        l_SamplerInfo.compareEnable = VK_FALSE;
        l_SamplerInfo.compareOp = VK_COMPARE_OP_NEVER;
        l_SamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        l_SamplerInfo.mipLodBias = 0.0f;
        l_SamplerInfo.minLod = 0.0f;
        l_SamplerInfo.maxLod = 0.0f;

        if (vkCreateSampler(l_Device, &l_SamplerInfo, nullptr, &target.m_Sampler) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create offscreen sampler");

            a_ResetTarget(target);

            return;
        }

        VkCommandBuffer l_BootstrapCommandBuffer = m_Commands.GetOneTimePool().Acquire();
        VkCommandBufferBeginInfo l_BootstrapBeginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        l_BootstrapBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(l_BootstrapCommandBuffer, &l_BootstrapBeginInfo);

        VkImageMemoryBarrier l_BootstrapBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        l_BootstrapBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_BootstrapBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_BootstrapBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_BootstrapBarrier.subresourceRange.baseMipLevel = 0;
        l_BootstrapBarrier.subresourceRange.levelCount = 1;
        l_BootstrapBarrier.subresourceRange.baseArrayLayer = 0;
        l_BootstrapBarrier.subresourceRange.layerCount = 1;
        l_BootstrapBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        l_BootstrapBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        l_BootstrapBarrier.srcAccessMask = 0;
        l_BootstrapBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        l_BootstrapBarrier.image = target.m_Image;

        // Bootstrap the image layout so descriptor writes and validation stay in sync when the viewport samples before the first render pass.
        vkCmdPipelineBarrier(l_BootstrapCommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &l_BootstrapBarrier);

        vkEndCommandBuffer(l_BootstrapCommandBuffer);

        VkSubmitInfo l_BootstrapSubmitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        l_BootstrapSubmitInfo.commandBufferCount = 1;
        l_BootstrapSubmitInfo.pCommandBuffers = &l_BootstrapCommandBuffer;

        vkQueueSubmit(Application::GetGraphicsQueue(), 1, &l_BootstrapSubmitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(Application::GetGraphicsQueue());

        m_Commands.GetOneTimePool().Release(l_BootstrapCommandBuffer);

        // Register (or refresh) the descriptor used by the viewport panel and keep it cached for quick retrieval.
        target.m_TextureID = ImGui_ImplVulkan_AddTexture(target.m_Sampler, target.m_ImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        target.m_Extent = extent;
        target.m_CurrentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        target.m_DepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        TR_CORE_TRACE("Offscreen render target resized to {}x{}", extent.width, extent.height);
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

        OffscreenTarget* l_ActiveTarget = nullptr;
        if (IsValidViewport())
        {
            const auto it_Target = m_OffscreenTargets.find(m_ActiveViewportId);
            if (it_Target != m_OffscreenTargets.end())
            {
                l_ActiveTarget = &it_Target->second;
            }
        }

        const bool l_ViewportActive = l_ActiveTarget != nullptr && l_ActiveTarget->m_Framebuffer != VK_NULL_HANDLE;

        auto a_BuildClearValue = [this]() -> VkClearValue
            {
                VkClearValue l_Value{};
                l_Value.color.float32[0] = m_ClearColor.r;
                l_Value.color.float32[1] = m_ClearColor.g;
                l_Value.color.float32[2] = m_ClearColor.b;
                l_Value.color.float32[3] = m_ClearColor.a;

                return l_Value;
            };

        if (l_ViewportActive)
        {
            // Ensure the offscreen image is ready for color attachment writes before we begin the render pass.
            VkPipelineStageFlags l_PreviousStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            VkAccessFlags l_PreviousAccess = 0;
            if (l_ActiveTarget->m_CurrentLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            {
                l_PreviousStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                l_PreviousAccess = VK_ACCESS_SHADER_READ_BIT;
            }
            else if (l_ActiveTarget->m_CurrentLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
            {
                l_PreviousStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                l_PreviousAccess = VK_ACCESS_TRANSFER_READ_BIT;
            }

            VkPipelineStageFlags l_DepthPreviousStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            VkAccessFlags l_DepthPreviousAccess = 0;
            if (l_ActiveTarget->m_DepthLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
            {
                l_DepthPreviousStage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
                l_DepthPreviousAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            }

            VkImageMemoryBarrier l_PrepareDepth{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            l_PrepareDepth.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            l_PrepareDepth.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            l_PrepareDepth.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            l_PrepareDepth.subresourceRange.baseMipLevel = 0;
            l_PrepareDepth.subresourceRange.levelCount = 1;
            l_PrepareDepth.subresourceRange.baseArrayLayer = 0;
            l_PrepareDepth.subresourceRange.layerCount = 1;
            l_PrepareDepth.oldLayout = l_ActiveTarget->m_DepthLayout;
            l_PrepareDepth.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            l_PrepareDepth.srcAccessMask = l_DepthPreviousAccess;
            l_PrepareDepth.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            l_PrepareDepth.image = l_ActiveTarget->m_DepthImage;

            vkCmdPipelineBarrier(l_CommandBuffer, l_DepthPreviousStage, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0, nullptr, 0, nullptr, 1, &l_PrepareDepth);
            l_ActiveTarget->m_DepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            VkImageMemoryBarrier l_PrepareOffscreen{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            l_PrepareOffscreen.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            l_PrepareOffscreen.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            l_PrepareOffscreen.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            l_PrepareOffscreen.subresourceRange.baseMipLevel = 0;
            l_PrepareOffscreen.subresourceRange.levelCount = 1;
            l_PrepareOffscreen.subresourceRange.baseArrayLayer = 0;
            l_PrepareOffscreen.subresourceRange.layerCount = 1;
            l_PrepareOffscreen.oldLayout = l_ActiveTarget->m_CurrentLayout;
            l_PrepareOffscreen.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            l_PrepareOffscreen.srcAccessMask = l_PreviousAccess;
            l_PrepareOffscreen.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            l_PrepareOffscreen.image = l_ActiveTarget->m_Image;

            vkCmdPipelineBarrier(l_CommandBuffer, l_PreviousStage, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &l_PrepareOffscreen);
            l_ActiveTarget->m_CurrentLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            // First pass: render the scene into the offscreen target that backs the editor viewport.
            VkRenderPassBeginInfo l_OffscreenPass{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
            l_OffscreenPass.renderPass = m_Pipeline.GetRenderPass();
            l_OffscreenPass.framebuffer = l_ActiveTarget->m_Framebuffer;
            l_OffscreenPass.renderArea.offset = { 0, 0 };
            l_OffscreenPass.renderArea.extent = l_ActiveTarget->m_Extent;

            // Reuse the configured clear colour for both render passes so the viewport preview matches the swapchain output.
            std::array<VkClearValue, 2> l_OffscreenClearValues{};
            l_OffscreenClearValues[0] = a_BuildClearValue();
            l_OffscreenClearValues[1].depthStencil.depth = 1.0f;
            l_OffscreenClearValues[1].depthStencil.stencil = 0;
            l_OffscreenPass.clearValueCount = static_cast<uint32_t>(l_OffscreenClearValues.size());
            l_OffscreenPass.pClearValues = l_OffscreenClearValues.data();

            vkCmdBeginRenderPass(l_CommandBuffer, &l_OffscreenPass, VK_SUBPASS_CONTENTS_INLINE);

            // Explicitly clear the colour attachment so the viewport image always starts from the requested editor clear colour.
            VkClearAttachment l_ColorAttachmentClear{};
            l_ColorAttachmentClear.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            l_ColorAttachmentClear.colorAttachment = 0;
            l_ColorAttachmentClear.clearValue.color.float32[0] = m_ClearColor.r;
            l_ColorAttachmentClear.clearValue.color.float32[1] = m_ClearColor.g;
            l_ColorAttachmentClear.clearValue.color.float32[2] = m_ClearColor.b;
            l_ColorAttachmentClear.clearValue.color.float32[3] = m_ClearColor.a;

            VkClearRect l_ColorClearRect{};
            l_ColorClearRect.rect.offset = { 0, 0 };
            l_ColorClearRect.rect.extent = l_ActiveTarget->m_Extent;
            l_ColorClearRect.baseArrayLayer = 0;
            l_ColorClearRect.layerCount = 1;

            vkCmdClearAttachments(l_CommandBuffer, 1, &l_ColorAttachmentClear, 1, &l_ColorClearRect);
            // Future improvement: consider a dedicated render pass with VK_ATTACHMENT_LOAD_OP_CLEAR for the editor path to simplify state management.

            VkViewport l_OffscreenViewport{};
            l_OffscreenViewport.x = 0.0f;
            l_OffscreenViewport.y = 0.0f;
            l_OffscreenViewport.width = static_cast<float>(l_ActiveTarget->m_Extent.width);
            l_OffscreenViewport.height = static_cast<float>(l_ActiveTarget->m_Extent.height);
            l_OffscreenViewport.minDepth = 0.0f;
            l_OffscreenViewport.maxDepth = 1.0f;
            vkCmdSetViewport(l_CommandBuffer, 0, 1, &l_OffscreenViewport);

            VkRect2D l_OffscreenScissor{};
            l_OffscreenScissor.offset = { 0, 0 };
            l_OffscreenScissor.extent = l_ActiveTarget->m_Extent;
            vkCmdSetScissor(l_CommandBuffer, 0, 1, &l_OffscreenScissor);

            vkCmdBindPipeline(l_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline.GetPipeline());
            m_Skybox.Record(l_CommandBuffer, m_Pipeline.GetPipelineLayout(), m_DescriptorSets.data(), imageIndex);

            if (m_VertexBuffer != VK_NULL_HANDLE && m_IndexBuffer != VK_NULL_HANDLE && m_IndexCount > 0)
            {
                VkBuffer l_VertexBuffers[] = { m_VertexBuffer };
                VkDeviceSize l_Offsets[] = { 0 };
                vkCmdBindVertexBuffers(l_CommandBuffer, 0, 1, l_VertexBuffers, l_Offsets);
                vkCmdBindIndexBuffer(l_CommandBuffer, m_IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdBindDescriptorSets(l_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline.GetPipelineLayout(), 0, 1, &m_DescriptorSets[imageIndex], 0, nullptr);

                glm::mat4 l_Transform = ComposeTransform(m_Registry->GetComponent<Transform>(m_Entity));
                vkCmdPushConstants(l_CommandBuffer, m_Pipeline.GetPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &l_Transform);

                vkCmdDrawIndexed(l_CommandBuffer, m_IndexCount, 1, 0, 0, 0);
            }

            vkCmdEndRenderPass(l_CommandBuffer);

            // Layout plan:
            // 1) COLOR_ATTACHMENT_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL so ImGui can sample the image.
            // 2) TRANSFER_SRC will also be available for future blit workflows before compositing on the swapchain.
            VkImageMemoryBarrier l_OffscreenBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            l_OffscreenBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            l_OffscreenBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            l_OffscreenBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            l_OffscreenBarrier.subresourceRange.baseMipLevel = 0;
            l_OffscreenBarrier.subresourceRange.levelCount = 1;
            l_OffscreenBarrier.subresourceRange.baseArrayLayer = 0;
            l_OffscreenBarrier.subresourceRange.layerCount = 1;
            l_OffscreenBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            l_OffscreenBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            l_OffscreenBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            l_OffscreenBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            l_OffscreenBarrier.image = l_ActiveTarget->m_Image;

            vkCmdPipelineBarrier(l_CommandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &l_OffscreenBarrier);
            l_ActiveTarget->m_CurrentLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        }

        VkImage l_SwapchainImage = m_Swapchain.GetImages()[imageIndex];
        VkImage l_SwapchainDepthImage = VK_NULL_HANDLE;
        const auto& l_DepthImages = m_Pipeline.GetDepthImages();
        if (imageIndex < l_DepthImages.size())
        {
            l_SwapchainDepthImage = l_DepthImages[imageIndex];
        }
        VkPipelineStageFlags l_SwapchainSrcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkAccessFlags l_SwapchainSrcAccess = 0;
        VkImageLayout l_PreviousLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (imageIndex < m_SwapchainImageLayouts.size())
        {
            l_PreviousLayout = m_SwapchainImageLayouts[imageIndex];
        }

        // Map the cached layout to the pipeline stage/access masks the barrier expects.
        switch (l_PreviousLayout)
        {
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            // Presented images relax to bottom-of-pipe with no further access requirements.
            l_SwapchainSrcStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            l_SwapchainSrcAccess = 0;
            break;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            l_SwapchainSrcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            l_SwapchainSrcAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            l_SwapchainSrcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            l_SwapchainSrcAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_UNDEFINED:
        default:
            // Fresh images begin at the top of the pipe with no access hazards to satisfy.
            l_SwapchainSrcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            l_SwapchainSrcAccess = 0;
            break;
        }

        // Prepare the swapchain image for either a blit copy or an explicit clear prior to the presentation render pass.
        VkImageMemoryBarrier l_PrepareSwapchain{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        l_PrepareSwapchain.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_PrepareSwapchain.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_PrepareSwapchain.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_PrepareSwapchain.subresourceRange.baseMipLevel = 0;
        l_PrepareSwapchain.subresourceRange.levelCount = 1;
        l_PrepareSwapchain.subresourceRange.baseArrayLayer = 0;
        l_PrepareSwapchain.subresourceRange.layerCount = 1;
        l_PrepareSwapchain.oldLayout = l_PreviousLayout;
        l_PrepareSwapchain.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        l_PrepareSwapchain.srcAccessMask = l_SwapchainSrcAccess;
        l_PrepareSwapchain.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        l_PrepareSwapchain.image = l_SwapchainImage;

        vkCmdPipelineBarrier(l_CommandBuffer, l_SwapchainSrcStage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &l_PrepareSwapchain);
        // Persist the layout change so the next frame knows the transfer destination state is active and validation stays happy.
        if (imageIndex < m_SwapchainImageLayouts.size())
        {
            m_SwapchainImageLayouts[imageIndex] = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        }

        if (l_ViewportActive)
        {
            // Multi-panel path: copy the rendered viewport into the swapchain image so every editor panel sees a synchronized back buffer.
            VkImageBlit l_BlitRegion{};
            l_BlitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            l_BlitRegion.srcSubresource.mipLevel = 0;
            l_BlitRegion.srcSubresource.baseArrayLayer = 0;
            l_BlitRegion.srcSubresource.layerCount = 1;
            l_BlitRegion.srcOffsets[0] = { 0, 0, 0 };
            l_BlitRegion.srcOffsets[1] = { static_cast<int32_t>(l_ActiveTarget->m_Extent.width), static_cast<int32_t>(l_ActiveTarget->m_Extent.height), 1 };
            l_BlitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            l_BlitRegion.dstSubresource.mipLevel = 0;
            l_BlitRegion.dstSubresource.baseArrayLayer = 0;
            l_BlitRegion.dstSubresource.layerCount = 1;
            l_BlitRegion.dstOffsets[0] = { 0, 0, 0 };
            l_BlitRegion.dstOffsets[1] = { static_cast<int32_t>(m_Swapchain.GetExtent().width), static_cast<int32_t>(m_Swapchain.GetExtent().height), 1 };

            vkCmdBlitImage(l_CommandBuffer,
                l_ActiveTarget->m_Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                l_SwapchainImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &l_BlitRegion, VK_FILTER_LINEAR);

            // After the blit the ImGui descriptor still expects shader read, so return the offscreen image to that layout.
            VkImageMemoryBarrier l_ToSample{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            l_ToSample.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            l_ToSample.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            l_ToSample.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            l_ToSample.subresourceRange.baseMipLevel = 0;
            l_ToSample.subresourceRange.levelCount = 1;
            l_ToSample.subresourceRange.baseArrayLayer = 0;
            l_ToSample.subresourceRange.layerCount = 1;
            l_ToSample.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            l_ToSample.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            l_ToSample.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            l_ToSample.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            l_ToSample.image = l_ActiveTarget->m_Image;

            vkCmdPipelineBarrier(l_CommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &l_ToSample);
            l_ActiveTarget->m_CurrentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            // Future improvement: evaluate layered compositing so multiple render targets can blend before hitting the back buffer.
        }
        else
        {
            // Legacy path clear performed via transfer op now that the render pass load operation no longer performs it implicitly.
            VkClearColorValue l_ClearValue{};
            l_ClearValue.float32[0] = m_ClearColor.r;
            l_ClearValue.float32[1] = m_ClearColor.g;
            l_ClearValue.float32[2] = m_ClearColor.b;
            l_ClearValue.float32[3] = m_ClearColor.a;

            VkImageSubresourceRange l_ClearRange{};
            l_ClearRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            l_ClearRange.baseMipLevel = 0;
            l_ClearRange.levelCount = 1;
            l_ClearRange.baseArrayLayer = 0;
            l_ClearRange.layerCount = 1;

            vkCmdClearColorImage(l_CommandBuffer, l_SwapchainImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &l_ClearValue, 1, &l_ClearRange);
        }

        // Transition the swapchain back to COLOR_ATTACHMENT_OPTIMAL so the render pass can output ImGui and any additional overlays.
        VkImageMemoryBarrier l_ToColorAttachment{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        l_ToColorAttachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_ToColorAttachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_ToColorAttachment.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_ToColorAttachment.subresourceRange.baseMipLevel = 0;
        l_ToColorAttachment.subresourceRange.levelCount = 1;
        l_ToColorAttachment.subresourceRange.baseArrayLayer = 0;
        l_ToColorAttachment.subresourceRange.layerCount = 1;
        l_ToColorAttachment.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        l_ToColorAttachment.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        l_ToColorAttachment.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        l_ToColorAttachment.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        l_ToColorAttachment.image = l_SwapchainImage;

        vkCmdPipelineBarrier(l_CommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0, 0, nullptr, 0, nullptr, 1, &l_ToColorAttachment);
        if (imageIndex < m_SwapchainImageLayouts.size())
        {
            m_SwapchainImageLayouts[imageIndex] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }

        if (l_SwapchainDepthImage != VK_NULL_HANDLE)
        {
            VkPipelineStageFlags l_DepthSrcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            VkAccessFlags l_DepthSrcAccess = 0;
            VkImageLayout l_PreviousDepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            if (imageIndex < m_SwapchainDepthLayouts.size())
            {
                l_PreviousDepthLayout = m_SwapchainDepthLayouts[imageIndex];
            }

            if (l_PreviousDepthLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
            {
                l_DepthSrcStage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
                l_DepthSrcAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            }

            VkImageMemoryBarrier l_PrepareDepthAttachment{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            l_PrepareDepthAttachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            l_PrepareDepthAttachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            l_PrepareDepthAttachment.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            l_PrepareDepthAttachment.subresourceRange.baseMipLevel = 0;
            l_PrepareDepthAttachment.subresourceRange.levelCount = 1;
            l_PrepareDepthAttachment.subresourceRange.baseArrayLayer = 0;
            l_PrepareDepthAttachment.subresourceRange.layerCount = 1;
            l_PrepareDepthAttachment.oldLayout = l_PreviousDepthLayout;
            l_PrepareDepthAttachment.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            l_PrepareDepthAttachment.srcAccessMask = l_DepthSrcAccess;
            l_PrepareDepthAttachment.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            l_PrepareDepthAttachment.image = l_SwapchainDepthImage;

            vkCmdPipelineBarrier(l_CommandBuffer, l_DepthSrcStage, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0, nullptr, 0, nullptr, 1, &l_PrepareDepthAttachment);

            if (imageIndex < m_SwapchainDepthLayouts.size())
            {
                m_SwapchainDepthLayouts[imageIndex] = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            }
        }

        // Second pass: draw the main swapchain image. The attachment now preserves the blit results for multi-panel compositing.
        VkRenderPassBeginInfo l_SwapchainPass{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        l_SwapchainPass.renderPass = m_Pipeline.GetRenderPass();
        l_SwapchainPass.framebuffer = m_Pipeline.GetFramebuffers()[imageIndex];
        l_SwapchainPass.renderArea.offset = { 0, 0 };
        l_SwapchainPass.renderArea.extent = m_Swapchain.GetExtent();
        // Provide both colour and depth clear values; the colour entry is ignored because the attachment loads, but depth needs a fresh 1.0f each frame.
        std::array<VkClearValue, 2> l_SwapchainClearValues{};
        l_SwapchainClearValues[0] = a_BuildClearValue();
        l_SwapchainClearValues[1].depthStencil.depth = 1.0f;
        l_SwapchainClearValues[1].depthStencil.stencil = 0;
        l_SwapchainPass.clearValueCount = static_cast<uint32_t>(l_SwapchainClearValues.size());
        l_SwapchainPass.pClearValues = l_SwapchainClearValues.data();

        vkCmdBeginRenderPass(l_CommandBuffer, &l_SwapchainPass, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport l_SwapchainViewport{};
        l_SwapchainViewport.x = 0.0f;
        l_SwapchainViewport.y = 0.0f;
        l_SwapchainViewport.width = static_cast<float>(m_Swapchain.GetExtent().width);
        l_SwapchainViewport.height = static_cast<float>(m_Swapchain.GetExtent().height);
        l_SwapchainViewport.minDepth = 0.0f;
        l_SwapchainViewport.maxDepth = 1.0f;
        vkCmdSetViewport(l_CommandBuffer, 0, 1, &l_SwapchainViewport);

        VkRect2D l_SwapchainScissor{};
        l_SwapchainScissor.offset = { 0, 0 };
        l_SwapchainScissor.extent = m_Swapchain.GetExtent();
        vkCmdSetScissor(l_CommandBuffer, 0, 1, &l_SwapchainScissor);

        if (!l_ViewportActive)
        {
            // Legacy rendering path: draw directly to the back buffer when the editor viewport is hidden.
            vkCmdBindPipeline(l_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline.GetPipeline());
            m_Skybox.Record(l_CommandBuffer, m_Pipeline.GetPipelineLayout(), m_DescriptorSets.data(), imageIndex);

            if (m_VertexBuffer != VK_NULL_HANDLE && m_IndexBuffer != VK_NULL_HANDLE && m_IndexCount > 0)
            {
                VkBuffer l_VertexBuffers[] = { m_VertexBuffer };
                VkDeviceSize l_Offsets[] = { 0 };
                vkCmdBindVertexBuffers(l_CommandBuffer, 0, 1, l_VertexBuffers, l_Offsets);
                vkCmdBindIndexBuffer(l_CommandBuffer, m_IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdBindDescriptorSets(l_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline.GetPipelineLayout(), 0, 1, &m_DescriptorSets[imageIndex], 0, nullptr);

                glm::mat4 l_Transform = ComposeTransform(m_Registry->GetComponent<Transform>(m_Entity));
                vkCmdPushConstants(l_CommandBuffer, m_Pipeline.GetPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &l_Transform);

                vkCmdDrawIndexed(l_CommandBuffer, m_IndexCount, 1, 0, 0, 0);
            }
        }
        else
        {
            // The multi-panel image was already blitted; leave the pass empty so UI overlays stack on top cleanly.
            // Future improvement: extend this path to support post-processing effects before UI submission.
        }

        if (m_ImGuiLayer)
        {
            m_ImGuiLayer->Render(l_CommandBuffer);
        }

        vkCmdEndRenderPass(l_CommandBuffer);

        VkImageMemoryBarrier l_PresentBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        l_PresentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_PresentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_PresentBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_PresentBarrier.subresourceRange.baseMipLevel = 0;
        l_PresentBarrier.subresourceRange.levelCount = 1;
        l_PresentBarrier.subresourceRange.baseArrayLayer = 0;
        l_PresentBarrier.subresourceRange.layerCount = 1;
        l_PresentBarrier.oldLayout = (imageIndex < m_SwapchainImageLayouts.size()) ? m_SwapchainImageLayouts[imageIndex] : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        l_PresentBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        l_PresentBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        l_PresentBarrier.dstAccessMask = 0;
        l_PresentBarrier.image = m_Swapchain.GetImages()[imageIndex];

        vkCmdPipelineBarrier(l_CommandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0, 0, nullptr, 0, nullptr, 1, &l_PresentBarrier);
        
        if (imageIndex < m_SwapchainImageLayouts.size())
        {
            // Keep the cached state aligned with the presentation transition so validation remains silent in future frames.
            m_SwapchainImageLayouts[imageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
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
        const size_t l_CurrentFrame = m_Commands.CurrentFrame();

        // Synchronization chain:
        // 1. Wait for the swapchain image acquired semaphore tied to the frame slot (keeps acquire/submit pacing aligned).
        // 2. Submit work that renders into the image for this frame-in-flight.
        // 3. Signal the image-scoped render-finished semaphore so presentation waits on the exact same handle when that image is presented.
        VkSemaphore l_WaitSemaphores[] = { m_Commands.GetImageAvailableSemaphorePerImage(l_CurrentFrame) };
        VkPipelineStageFlags l_WaitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        VkSemaphore l_SignalSemaphores[] = { m_Commands.GetRenderFinishedSemaphoreForImage(imageIndex) };

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
        const size_t l_CurrentFrame = m_Commands.CurrentFrame();

        // Presentation waits on the per-image semaphore that SubmitFrame signaled. This keeps validation happy by ensuring
        // the handle is only recycled after vkQueuePresentKHR consumes it and the swapchain re-issues the image.
        VkSemaphore l_WaitSemaphores[] = { m_Commands.GetRenderFinishedSemaphoreForImage(imageIndex) };

        VkPresentInfoKHR l_PresentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        l_PresentInfo.waitSemaphoreCount = 1;
        l_PresentInfo.pWaitSemaphores = l_WaitSemaphores;

        VkSwapchainKHR l_Swapchains[] = { m_Swapchain.GetSwapchain() };
        l_PresentInfo.swapchainCount = 1;
        l_PresentInfo.pSwapchains = l_Swapchains;
        l_PresentInfo.pImageIndices = &imageIndex;

        VkResult l_PresentResult = vkQueuePresentKHR(Application::GetPresentQueue(), &l_PresentInfo);

        // Future improvement: leverage VK_EXT_swapchain_maintenance1 to release images earlier if presentation gets backlogged.

        if (l_PresentResult == VK_ERROR_OUT_OF_DATE_KHR || l_PresentResult == VK_SUBOPTIMAL_KHR)
        {
            RecreateSwapchain();
        }

        else if (l_PresentResult != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to present swap chain image!");
        }
    }

    void Renderer::ProcessReloadEvents()
    {
        auto& a_Watcher = Utilities::FileWatcher::Get();
        bool l_DeviceIdle = false;

        while (auto a_Event = a_Watcher.PopPendingEvent())
        {
            if (!l_DeviceIdle)
            {
                // Block the graphics queue once before processing the first reload to ensure resources are idle.
                vkDeviceWaitIdle(Application::GetDevice());
                l_DeviceIdle = true;
            }

            bool l_Success = false;
            std::string l_Message{};

            switch (a_Event->Type)
            {
            case Utilities::FileWatcher::WatchType::Shader:
            {
                // Shader reload leverages the existing hot-reload path but skips the internal wait because we already idled above.
                bool l_Reloaded = m_Pipeline.ReloadIfNeeded(m_Swapchain, false);
                if (l_Reloaded && m_Pipeline.GetPipeline() != VK_NULL_HANDLE)
                {
                    l_Success = true;
                    l_Message = "Graphics pipeline rebuilt";
                }
                else
                {
                    l_Message = "Shader reload failed - check compiler output";
                }
                break;
            }
            case Utilities::FileWatcher::WatchType::Model:
            {
                auto a_ModelData = Loader::ModelLoader::Load(a_Event->Path);
                if (!a_ModelData.Meshes.empty())
                {
                    UploadMesh(a_ModelData.Meshes, a_ModelData.Materials);
                    l_Success = true;
                    l_Message = "Model assets reuploaded";
                }
                else
                {
                    l_Message = "Model loader returned no meshes";
                }
                break;
            }
            case Utilities::FileWatcher::WatchType::Texture:
            {
                auto a_Texture = Loader::TextureLoader::Load(a_Event->Path);
                if (!a_Texture.Pixels.empty())
                {
                    UploadTexture(a_Texture);
                    l_Success = true;
                    l_Message = "Texture refreshed";
                }
                else
                {
                    l_Message = "Texture loader returned empty pixel data";
                }
                break;
            }
            default:
                l_Message = "Unhandled reload type";
                break;
            }

            if (l_Success)
            {
                a_Watcher.MarkEventSuccess(a_Event->Id, l_Message);
                TR_CORE_INFO("Hot reload succeeded for {}", a_Event->Path.c_str());
            }
            else
            {
                a_Watcher.MarkEventFailure(a_Event->Id, l_Message);
                TR_CORE_ERROR("Hot reload failed for {}: {}", a_Event->Path.c_str(), l_Message.c_str());
            }
        }
    }

    void Renderer::UpdateUniformBuffer(uint32_t currentImage)
    {
        GlobalUniformBuffer l_Global{};
        const CameraSnapshot l_CameraSnapshot = ResolveViewportCamera();

        l_Global.View = l_CameraSnapshot.View;

        float l_AspectRatio = static_cast<float>(m_Swapchain.GetExtent().width) / static_cast<float>(m_Swapchain.GetExtent().height);
        l_Global.Projection = glm::perspective(glm::radians(l_CameraSnapshot.FieldOfView), l_AspectRatio, l_CameraSnapshot.NearClip, l_CameraSnapshot.FarClip);
        l_Global.Projection[1][1] *= -1.0f; // Flip Y for Vulkan's clip space

        l_Global.CameraPosition = glm::vec4(l_CameraSnapshot.Position, 1.0f);

        glm::vec3 l_LightDirection = glm::normalize(m_MainLight.Direction);
        l_Global.LightDirection = glm::vec4(l_LightDirection, 0.0f);
        l_Global.LightColorIntensity = glm::vec4(m_MainLight.Color, m_MainLight.Intensity);
        l_Global.AmbientColorIntensity = glm::vec4(m_AmbientColor, m_AmbientIntensity);

        MaterialUniformBuffer l_Material{};
        if (!m_Materials.empty())
        {
            const Geometry::Material& l_FirstMaterial = m_Materials.front();
            l_Material.BaseColorFactor = l_FirstMaterial.BaseColorFactor;
            l_Material.MaterialFactors = glm::vec4(l_FirstMaterial.MetallicFactor, l_FirstMaterial.RoughnessFactor, 1.0f, 0.0f);
        }
        else
        {
            l_Material.BaseColorFactor = glm::vec4(1.0f);
            l_Material.MaterialFactors = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
        }

        void* l_Data = nullptr;
        vkMapMemory(Application::GetDevice(), m_GlobalUniformBuffersMemory[currentImage], 0, sizeof(l_Global), 0, &l_Data);
        memcpy(l_Data, &l_Global, sizeof(l_Global));
        vkUnmapMemory(Application::GetDevice(), m_GlobalUniformBuffersMemory[currentImage]);

        vkMapMemory(Application::GetDevice(), m_MaterialUniformBuffersMemory[currentImage], 0, sizeof(l_Material), 0, &l_Data);
        memcpy(l_Data, &l_Material, sizeof(l_Material));
        vkUnmapMemory(Application::GetDevice(), m_MaterialUniformBuffersMemory[currentImage]);
    }

    void Renderer::SetTransform(const Transform& props)
    {
        if (m_Registry)
        {
            if (!m_Registry->HasComponent<Transform>(m_Entity))
            {
                m_Registry->AddComponent<Transform>(m_Entity, props);
            }
            else
            {
                m_Registry->GetComponent<Transform>(m_Entity) = props;
            }
        }
    }

    Transform Renderer::GetTransform() const
    {
        if (m_Registry && m_Registry->HasComponent<Transform>(m_Entity))
        {
            return m_Registry->GetComponent<Transform>(m_Entity);
        }

        return {};
    }

    void Renderer::SetPerformanceCaptureEnabled(bool enabled)
    {
        if (enabled == m_PerformanceCaptureEnabled)
        {
            return;
        }

        m_PerformanceCaptureEnabled = enabled;
        if (m_PerformanceCaptureEnabled)
        {
            // Reset capture buffer so the exported data only contains the new capture session.
            m_PerformanceCaptureBuffer.clear();
            m_PerformanceCaptureBuffer.reserve(s_PerformanceHistorySize);
            m_PerformanceCaptureStartTime = std::chrono::system_clock::now();

            TR_CORE_INFO("Performance capture enabled");
        }
        else
        {
            ExportPerformanceCapture();
            m_PerformanceCaptureBuffer.clear();

            TR_CORE_INFO("Performance capture disabled");
        }
    }

    void Renderer::AccumulateFrameTiming(double frameMilliseconds, double framesPerSecond, VkExtent2D extent, std::chrono::system_clock::time_point captureTimestamp)
    {
        FrameTimingSample l_Sample{};
        l_Sample.FrameMilliseconds = frameMilliseconds;
        l_Sample.FramesPerSecond = framesPerSecond;
        l_Sample.Extent = extent;
        l_Sample.CaptureTime = captureTimestamp;

        // Store the latest sample inside the fixed-size ring buffer.
        m_PerformanceHistory[m_PerformanceHistoryNextIndex] = l_Sample;
        m_PerformanceHistoryNextIndex = (m_PerformanceHistoryNextIndex + 1) % m_PerformanceHistory.size();
        if (m_PerformanceSampleCount < m_PerformanceHistory.size())
        {
            ++m_PerformanceSampleCount;
        }

        UpdateFrameTimingStats();

        if (m_PerformanceCaptureEnabled)
        {
            m_PerformanceCaptureBuffer.push_back(l_Sample);
        }
    }

    void Renderer::UpdateFrameTimingStats()
    {
        if (m_PerformanceSampleCount == 0)
        {
            m_PerformanceStats = {};

            return;
        }

        double l_MinMilliseconds = std::numeric_limits<double>::max();
        double l_MaxMilliseconds = 0.0;
        double l_TotalMilliseconds = 0.0;
        double l_TotalFPS = 0.0;

        const size_t l_BufferSize = m_PerformanceHistory.size();
        const size_t l_ValidCount = m_PerformanceSampleCount;
        const size_t l_FirstIndex = (m_PerformanceHistoryNextIndex + l_BufferSize - l_ValidCount) % l_BufferSize;
        for (size_t l_Offset = 0; l_Offset < l_ValidCount; ++l_Offset)
        {
            const size_t l_Index = (l_FirstIndex + l_Offset) % l_BufferSize;
            const FrameTimingSample& it_Sample = m_PerformanceHistory[l_Index];

            l_MinMilliseconds = std::min(l_MinMilliseconds, it_Sample.FrameMilliseconds);
            l_MaxMilliseconds = std::max(l_MaxMilliseconds, it_Sample.FrameMilliseconds);
            l_TotalMilliseconds += it_Sample.FrameMilliseconds;
            l_TotalFPS += it_Sample.FramesPerSecond;
        }

        const double l_Count = static_cast<double>(l_ValidCount);
        m_PerformanceStats.MinimumMilliseconds = l_MinMilliseconds;
        m_PerformanceStats.MaximumMilliseconds = l_MaxMilliseconds;
        m_PerformanceStats.AverageMilliseconds = l_TotalMilliseconds / l_Count;
        m_PerformanceStats.AverageFPS = l_TotalFPS / l_Count;
    }

    void Renderer::ExportPerformanceCapture()
    {
        if (m_PerformanceCaptureBuffer.empty())
        {
            TR_CORE_WARN("Performance capture requested without any collected samples");

            return;
        }

        std::filesystem::path l_OutputDirectory{ "PerformanceCaptures" };
        std::error_code l_CreateError{};
        std::filesystem::create_directories(l_OutputDirectory, l_CreateError);
        if (l_CreateError)
        {
            TR_CORE_ERROR("Failed to create performance capture directory: {}", l_CreateError.message().c_str());

            return;
        }

        const std::time_t l_StartTime = std::chrono::system_clock::to_time_t(m_PerformanceCaptureStartTime);
        const std::tm l_StartLocal = ToLocalTime(l_StartTime);
        std::ostringstream l_FileNameStream;
        l_FileNameStream << "capture_" << std::put_time(&l_StartLocal, "%Y%m%d_%H%M%S") << ".csv";
        const std::filesystem::path l_FilePath = l_OutputDirectory / l_FileNameStream.str();

        std::ofstream l_File(l_FilePath, std::ios::trunc);
        if (!l_File.is_open())
        {
            TR_CORE_ERROR("Failed to open performance capture file: {}", l_FilePath.string().c_str());

            return;
        }

        // Write CSV header to simplify downstream analysis in spreadsheets.
        l_File << "Timestamp,Frame (ms),FPS,Extent Width,Extent Height\n";
        for (const FrameTimingSample& it_Sample : m_PerformanceCaptureBuffer)
        {
            const std::time_t l_SampleTime = std::chrono::system_clock::to_time_t(it_Sample.CaptureTime);
            const std::tm l_SampleLocal = ToLocalTime(l_SampleTime);
            l_File << std::put_time(&l_SampleLocal, "%Y-%m-%d %H:%M:%S") << ',' << it_Sample.FrameMilliseconds << ','
                << it_Sample.FramesPerSecond << ',' << it_Sample.Extent.width << ',' << it_Sample.Extent.height << '\n';
        }

        l_File.close();

        TR_CORE_INFO("Performance capture exported to {}", l_FilePath.string().c_str());
    }
}