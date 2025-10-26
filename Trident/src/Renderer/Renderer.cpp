#include "Renderer/Renderer.h"

#include "Application/Startup.h"

#include "ECS/Components/TransformComponent.h"
#include "Geometry/Mesh.h"
#include "UI/ImGuiLayer.h"
#include "Core/Utilities.h"
#include "Window/Window.h"
#include "Loader/ModelLoader.h"
#include "Loader/TextureLoader.h"

#include <stdexcept>
#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <chrono>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <limits>
#include <ctime>
#include <memory>
#include <system_error>
#include <cstring>
#include <cctype>
#include <cassert>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <imgui_impl_vulkan.h>

namespace
{
    constexpr const char* kDefaultTextureKey = "renderer://default-white";

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

    std::tm ToLocalTime(std::time_t time)
    {
        std::tm l_LocalTime{};
#ifdef _WIN32
        localtime_s(&l_LocalTime, &time);
#else
        localtime_r(&time, &l_LocalTime);
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

        m_Registry = &Startup::GetRegistry();

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
        // Allocate per-frame uniform buffers for camera/light state and storage buffers for the material table.
        m_Buffers.CreateUniformBuffers(m_Swapchain.GetImageCount(), l_GlobalSize, m_GlobalUniformBuffers, m_GlobalUniformBuffersMemory);
        EnsureMaterialBufferCapacity(m_Materials.size());

        CreateDescriptorPool();
        CreateDefaultTexture();
        CreateDefaultSkybox();
        CreateDescriptorSets();

        // Prepare shared quad geometry so every sprite draw can reference the same GPU buffers.
        BuildSpriteGeometry();

        m_ViewportContexts.clear();
        m_ActiveViewportId = 0;

        ViewportContext& l_DefaultContext = GetOrCreateViewportContext(m_ActiveViewportId);
        l_DefaultContext.m_Info.ViewportID = m_ActiveViewportId;
        l_DefaultContext.m_Info.Position = { 0.0f, 0.0f };
        l_DefaultContext.m_Info.Size = { static_cast<float>(m_Swapchain.GetExtent().width), static_cast<float>(m_Swapchain.GetExtent().height) };
        l_DefaultContext.m_CachedExtent = { 0, 0 };

        VkFenceCreateInfo l_FenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        l_FenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (vkCreateFence(Startup::GetDevice(), &l_FenceInfo, nullptr, &m_ResourceFence) != VK_SUCCESS)
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
        m_ViewportContexts.clear();

        // Release shared sprite geometry before the buffer allocator clears tracked allocations.
        DestroySpriteGeometry();

        if (m_DescriptorPool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(Startup::GetDevice(), m_DescriptorPool, nullptr);

            m_DescriptorPool = VK_NULL_HANDLE;
        }

        m_DescriptorSets.clear();
        DestroySkyboxDescriptorSets();
        m_Pipeline.Cleanup();
        m_Swapchain.Cleanup();
        m_Skybox.Cleanup(m_Buffers);
        m_Buffers.Cleanup();
        m_GlobalUniformBuffers.clear();
        m_GlobalUniformBuffersMemory.clear();
        m_MaterialBuffers.clear();
        m_MaterialBuffersMemory.clear();
        m_MaterialBufferDirty.clear();
        m_MaterialBufferElementCount = 0;

        for (TextureSlot& it_Slot : m_TextureSlots)
        {
            DestroyTextureSlot(it_Slot);
        }
        m_TextureSlots.clear();
        m_TextureSlotLookup.clear();
        m_TextureDescriptorCache.clear();

        DestroySkyboxCubemap();

        for (auto& it_Texture : m_ImGuiTexturePool)
        {
            if (it_Texture)
            {
                DestroyImGuiTexture(*it_Texture);
            }
        }
        m_ImGuiTexturePool.clear();

        if (m_ResourceFence != VK_NULL_HANDLE)
        {
            vkDestroyFence(Startup::GetDevice(), m_ResourceFence, nullptr);

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

        // Allow developers to tweak GLSL and get instant feedback without restarting the app.
        if (m_Pipeline.ReloadIfNeeded(m_Swapchain))
        {
            TR_CORE_INFO("Graphics pipeline reloaded after shader edit");
        }

        VkFence l_InFlightFence = m_Commands.GetInFlightFence(m_Commands.CurrentFrame());
        vkWaitForFences(Startup::GetDevice(), 1, &l_InFlightFence, VK_TRUE, UINT64_MAX);

        uint32_t l_ImageIndex = 0;
        if (!AcquireNextImage(l_ImageIndex, l_InFlightFence))
        {
            TR_CORE_CRITICAL("Failed to acquire the next image");

            return;
        }

        UpdateUniformBuffer(l_ImageIndex);

        vkResetFences(Startup::GetDevice(), 1, &l_InFlightFence);

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

    void Renderer::UploadMesh(const std::vector<Geometry::Mesh>& meshes, const std::vector<Geometry::Material>& materials, const std::vector<std::string>& textures)
    {
        // Persist the latest geometry so subsequent drag-and-drop operations can rebuild GPU buffers without reloading from disk.
        m_GeometryCache = meshes;
        m_Materials = materials;

        // Resolve texture slots for every material so the fragment shader can index the descriptor array safely.
        ResolveMaterialTextureSlots(textures, 0, m_Materials.size());

        UploadMeshFromCache();
    }

    void Renderer::AppendMeshes(std::vector<Geometry::Mesh> meshes, std::vector<Geometry::Material> materials, std::vector<std::string> textures)
    {
        if (meshes.empty())
        {
            return;
        }

        const size_t l_OldMaterialCount = m_Materials.size();
        m_GeometryCache.reserve(m_GeometryCache.size() + meshes.size());

        for (Geometry::Mesh& it_Mesh : meshes)
        {
            if (it_Mesh.MaterialIndex >= 0)
            {
                it_Mesh.MaterialIndex += static_cast<int32_t>(l_OldMaterialCount);
            }
            m_GeometryCache.emplace_back(std::move(it_Mesh));
        }

        const size_t l_NewMaterialOffset = m_Materials.size();
        m_Materials.reserve(m_Materials.size() + materials.size());
        for (Geometry::Material& it_Material : materials)
        {
            m_Materials.emplace_back(std::move(it_Material));
        }

        // Texture indices stored on the incoming materials refer to the provided texture array. Resolve the
        // GPU slot mapping now so draws issued after the upload can reference the correct descriptor.
        ResolveMaterialTextureSlots(textures, l_NewMaterialOffset, materials.size());

        UploadMeshFromCache();
    }

    void Renderer::UploadMeshFromCache()
    {
        // Ensure no GPU operations are using the old buffers before reallocating resources.
        vkWaitForFences(Startup::GetDevice(), 1, &m_ResourceFence, VK_TRUE, UINT64_MAX);

        // Ensure the GPU-visible material table matches the CPU cache before geometry uploads begin.
        EnsureMaterialBufferCapacity(m_Materials.size());
        MarkMaterialBuffersDirty();

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

        const auto& l_Meshes = m_GeometryCache;

        size_t l_VertexCount = 0;
        size_t l_IndexCount = 0;
        for (const auto& it_Mesh : l_Meshes)
        {
            l_VertexCount += it_Mesh.Vertices.size();
            l_IndexCount += it_Mesh.Indices.size();
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
        for (const auto& it_Mesh : l_Meshes)
        {
            std::copy(it_Mesh.Vertices.begin(), it_Mesh.Vertices.end(), m_StagingVertices.get() + l_VertOffset);
            for (auto index : it_Mesh.Indices)
            {
                m_StagingIndices[l_IndexOffset++] = index + l_Offset;
            }
            l_VertOffset += it_Mesh.Vertices.size();
            l_Offset += static_cast<uint32_t>(it_Mesh.Vertices.size());
        }

        std::vector<Vertex> l_AllVertices(m_StagingVertices.get(), m_StagingVertices.get() + l_VertexCount);
        std::vector<uint32_t> l_AllIndices(m_StagingIndices.get(), m_StagingIndices.get() + l_IndexCount);

        // Upload the combined geometry once per load so every mesh can share the same GPU buffers.
        if (!l_AllVertices.empty())
        {
            m_Buffers.CreateVertexBuffer(l_AllVertices, m_Commands.GetOneTimePool(), m_VertexBuffer, m_VertexBufferMemory);
        }
        if (!l_AllIndices.empty())
        {
            m_Buffers.CreateIndexBuffer(l_AllIndices, m_Commands.GetOneTimePool(), m_IndexBuffer, m_IndexBufferMemory, m_IndexCount);
        }

        // Record the uploaded index count so the command buffer draw guard can validate pending draws.
        m_IndexCount = static_cast<uint32_t>(l_IndexCount);

        // Cache draw metadata for each mesh so render submissions can address shared buffers safely.
        m_MeshDrawInfo.clear();
        m_MeshDrawInfo.reserve(l_Meshes.size());

        uint32_t l_FirstIndexCursor = 0;
        int32_t l_BaseVertexCursor = 0;
        for (size_t l_MeshIndex = 0; l_MeshIndex < l_Meshes.size(); ++l_MeshIndex)
        {
            const auto& it_Mesh = l_Meshes[l_MeshIndex];
            MeshDrawInfo l_DrawInfo{};
            l_DrawInfo.m_FirstIndex = l_FirstIndexCursor;
            l_DrawInfo.m_IndexCount = static_cast<uint32_t>(it_Mesh.Indices.size());
            l_DrawInfo.m_BaseVertex = l_BaseVertexCursor;
            l_DrawInfo.m_MaterialIndex = it_Mesh.MaterialIndex;

            m_MeshDrawInfo.push_back(l_DrawInfo);

            l_FirstIndexCursor += l_DrawInfo.m_IndexCount;
            l_BaseVertexCursor += static_cast<int32_t>(it_Mesh.Vertices.size());
        }

        // Clear any cached draw list so the next frame rebuilds commands using the fresh offsets.
        m_MeshDrawCommands.clear();

        if (m_Registry)
        {
            const auto& l_Entities = m_Registry->GetEntities();
            for (ECS::Entity it_Entity : l_Entities)
            {
                if (!m_Registry->HasComponent<MeshComponent>(it_Entity))
                {
                    continue;
                }

                MeshComponent& l_Component = m_Registry->GetComponent<MeshComponent>(it_Entity);
                if (l_Component.m_Primitive != MeshComponent::PrimitiveType::None && l_Component.m_MeshIndex == std::numeric_limits<size_t>::max())
                {
                    // TODO: Generate renderer-side draw info when procedural primitives acquire baked geometry.
                    continue;
                }

                if (l_Component.m_MeshIndex >= m_MeshDrawInfo.size())
                {
                    continue;
                }

                const MeshDrawInfo& l_DrawInfo = m_MeshDrawInfo[l_Component.m_MeshIndex];
                l_Component.m_FirstIndex = l_DrawInfo.m_FirstIndex;
                l_Component.m_IndexCount = l_DrawInfo.m_IndexCount;
                l_Component.m_BaseVertex = l_DrawInfo.m_BaseVertex;
                l_Component.m_MaterialIndex = l_DrawInfo.m_MaterialIndex;
            }
        }

        m_ModelCount = l_Meshes.size();
        m_TriangleCount = l_IndexCount / 3;

        TR_CORE_INFO("Scene info - Models: {} Triangles: {} Materials: {}", m_ModelCount, m_TriangleCount, m_Materials.size());
    }

    void Renderer::UploadTexture(const std::string& texturePath, const Loader::TextureData& texture)
    {
        std::string l_NormalizedPath = NormalizeTexturePath(texturePath);
        TR_CORE_TRACE("Uploading texture refresh for '{}' ({}x{})", l_NormalizedPath.c_str(), texture.Width, texture.Height);

        if (l_NormalizedPath.empty())
        {
            TR_CORE_WARN("Texture refresh skipped because the provided path was empty.");
            return;
        }

        if (texture.Pixels.empty())
        {
            TR_CORE_WARN("Texture '{}' has no pixel data. Keeping the existing descriptor binding.", l_NormalizedPath.c_str());
            return;
        }

        if (m_ResourceFence != VK_NULL_HANDLE)
        {
            vkWaitForFences(Startup::GetDevice(), 1, &m_ResourceFence, VK_TRUE, UINT64_MAX);
        }

        auto a_Existing = m_TextureSlotLookup.find(l_NormalizedPath);
        if (a_Existing == m_TextureSlotLookup.end())
        {
            if (m_TextureSlots.size() >= Pipeline::s_MaxMaterialTextures)
            {
                TR_CORE_WARN("Texture budget exhausted. Unable to hot-reload '{}'.", l_NormalizedPath.c_str());
                return;
            }

            TextureSlot l_NewSlot{};
            if (!PopulateTextureSlot(l_NewSlot, texture))
            {
                TR_CORE_WARN("Failed to upload new texture data for '{}'. Keeping previous bindings.", l_NormalizedPath.c_str());
                return;
            }

            l_NewSlot.m_SourcePath = l_NormalizedPath;
            m_TextureSlots.push_back(std::move(l_NewSlot));
            const uint32_t l_NewIndex = static_cast<uint32_t>(m_TextureSlots.size() - 1);
            m_TextureSlotLookup.emplace(l_NormalizedPath, l_NewIndex);
        }
        else
        {
            const uint32_t l_SlotIndex = a_Existing->second;
            if (l_SlotIndex >= m_TextureSlots.size())
            {
                TR_CORE_WARN("Texture cache entry for '{}' referenced an invalid slot. Reverting to default.", l_NormalizedPath.c_str());
                m_TextureSlotLookup[l_NormalizedPath] = 0u;
            }
            else
            {
                DestroyTextureSlot(m_TextureSlots[l_SlotIndex]);

                TextureSlot l_Replacement{};
                if (!PopulateTextureSlot(l_Replacement, texture))
                {
                    TR_CORE_WARN("Failed to refresh texture '{}'. Using the default slot instead.", l_NormalizedPath.c_str());
                    m_TextureSlotLookup[l_NormalizedPath] = 0u;
                }
                else
                {
                    l_Replacement.m_SourcePath = l_NormalizedPath;
                    m_TextureSlots[l_SlotIndex] = std::move(l_Replacement);
                }
            }
        }

        RefreshTextureDescriptorBindings();
    }

    Renderer::ImGuiTexture* Renderer::CreateImGuiTexture(const Loader::TextureData& texture)
    {
        // Icons use the same upload path as standard textures but retain their Vulkan
        // objects so ImGui can sample them every frame. This method centralises the
        // boilerplate and keeps lifetime management within the renderer.
        if (texture.Pixels.empty())
        {
            TR_CORE_WARN("ImGui texture creation skipped because no pixel data was supplied.");

            return nullptr;
        }

        VkDevice l_Device = Startup::GetDevice();

        auto l_TextureStorage = std::make_unique<ImGuiTexture>();
        ImGuiTexture& l_Texture = *l_TextureStorage;

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

        if (vkCreateImage(l_Device, &l_ImageInfo, nullptr, &l_Texture.m_Image) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create ImGui texture image");

            m_Buffers.DestroyBuffer(l_StagingBuffer, l_StagingMemory);

            return nullptr;
        }

        VkMemoryRequirements l_MemReq{};
        vkGetImageMemoryRequirements(l_Device, l_Texture.m_Image, &l_MemReq);

        VkMemoryAllocateInfo l_AllocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        l_AllocInfo.allocationSize = l_MemReq.size;
        l_AllocInfo.memoryTypeIndex = m_Buffers.FindMemoryType(l_MemReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(l_Device, &l_AllocInfo, nullptr, &l_Texture.m_ImageMemory) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to allocate ImGui texture memory");

            vkDestroyImage(l_Device, l_Texture.m_Image, nullptr);
            l_Texture.m_Image = VK_NULL_HANDLE;
            m_Buffers.DestroyBuffer(l_StagingBuffer, l_StagingMemory);

            return nullptr;
        }

        vkBindImageMemory(l_Device, l_Texture.m_Image, l_Texture.m_ImageMemory, 0);

        VkCommandBuffer l_CommandBuffer = m_Commands.BeginSingleTimeCommands();

        VkImageMemoryBarrier l_BarrierToTransfer{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        l_BarrierToTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        l_BarrierToTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        l_BarrierToTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_BarrierToTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_BarrierToTransfer.image = l_Texture.m_Image;
        l_BarrierToTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_BarrierToTransfer.subresourceRange.baseMipLevel = 0;
        l_BarrierToTransfer.subresourceRange.levelCount = 1;
        l_BarrierToTransfer.subresourceRange.baseArrayLayer = 0;
        l_BarrierToTransfer.subresourceRange.layerCount = 1;
        l_BarrierToTransfer.srcAccessMask = 0;
        l_BarrierToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(l_CommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &l_BarrierToTransfer);

        VkBufferImageCopy l_CopyRegion{};
        l_CopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_CopyRegion.imageSubresource.mipLevel = 0;
        l_CopyRegion.imageSubresource.baseArrayLayer = 0;
        l_CopyRegion.imageSubresource.layerCount = 1;
        l_CopyRegion.imageOffset = { 0, 0, 0 };
        l_CopyRegion.imageExtent = { static_cast<uint32_t>(texture.Width), static_cast<uint32_t>(texture.Height), 1 };

        vkCmdCopyBufferToImage(l_CommandBuffer, l_StagingBuffer, l_Texture.m_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &l_CopyRegion);

        VkImageMemoryBarrier l_BarrierToShader{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        l_BarrierToShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        l_BarrierToShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        l_BarrierToShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_BarrierToShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_BarrierToShader.image = l_Texture.m_Image;
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
        l_ViewInfo.image = l_Texture.m_Image;
        l_ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        l_ViewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        l_ViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_ViewInfo.subresourceRange.baseMipLevel = 0;
        l_ViewInfo.subresourceRange.levelCount = 1;
        l_ViewInfo.subresourceRange.baseArrayLayer = 0;
        l_ViewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(l_Device, &l_ViewInfo, nullptr, &l_Texture.m_ImageView) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create ImGui texture image view");

            DestroyImGuiTexture(l_Texture);

            return nullptr;
        }

        VkSamplerCreateInfo l_SamplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        l_SamplerInfo.magFilter = VK_FILTER_LINEAR;
        l_SamplerInfo.minFilter = VK_FILTER_LINEAR;
        l_SamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        l_SamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        l_SamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        l_SamplerInfo.anisotropyEnable = VK_FALSE;
        l_SamplerInfo.maxAnisotropy = 1.0f;
        l_SamplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        l_SamplerInfo.unnormalizedCoordinates = VK_FALSE;
        l_SamplerInfo.compareEnable = VK_FALSE;
        l_SamplerInfo.compareOp = VK_COMPARE_OP_NEVER;
        l_SamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        l_SamplerInfo.mipLodBias = 0.0f;
        l_SamplerInfo.minLod = 0.0f;
        l_SamplerInfo.maxLod = 0.0f;

        if (vkCreateSampler(l_Device, &l_SamplerInfo, nullptr, &l_Texture.m_Sampler) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create ImGui texture sampler");

            DestroyImGuiTexture(l_Texture);

            return nullptr;
        }

        l_Texture.m_Descriptor = (ImTextureID)ImGui_ImplVulkan_AddTexture(l_Texture.m_Sampler, l_Texture.m_ImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        l_Texture.m_Extent.width = static_cast<uint32_t>(texture.Width);
        l_Texture.m_Extent.height = static_cast<uint32_t>(texture.Height);

        m_ImGuiTexturePool.push_back(std::move(l_TextureStorage));

        return m_ImGuiTexturePool.back().get();
    }

    void Renderer::DestroyImGuiTexture(ImGuiTexture& texture)
    {
        // Safe guard every handle so the method tolerates partially initialised textures
        // created during error paths.
        if (texture.m_Sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(Startup::GetDevice(), texture.m_Sampler, nullptr);
            texture.m_Sampler = VK_NULL_HANDLE;
        }

        if (texture.m_ImageView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(Startup::GetDevice(), texture.m_ImageView, nullptr);
            texture.m_ImageView = VK_NULL_HANDLE;
        }

        if (texture.m_Image != VK_NULL_HANDLE)
        {
            vkDestroyImage(Startup::GetDevice(), texture.m_Image, nullptr);
            texture.m_Image = VK_NULL_HANDLE;
        }

        if (texture.m_ImageMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(Startup::GetDevice(), texture.m_ImageMemory, nullptr);
            texture.m_ImageMemory = VK_NULL_HANDLE;
        }

        texture.m_Extent = { 0, 0 };
    }

    void Renderer::SetImGuiLayer(UI::ImGuiLayer* layer)
    {
        m_ImGuiLayer = layer;
    }

    void Renderer::SetEditorCamera(Camera* camera)
    {
        m_EditorCamera = camera;
        if (m_EditorCamera)
        {
            glm::vec2 l_ViewportSize{ static_cast<float>(m_Swapchain.GetExtent().width), static_cast<float>(m_Swapchain.GetExtent().height) };

            // Prefer the dedicated editor viewport (ID 1U) when sizing the camera so the game view never steals its feed.
            if (const ViewportContext* l_EditorContext = FindViewportContext(1U))
            {
                if (IsValidViewport(l_EditorContext->m_Info))
                {
                    l_ViewportSize = l_EditorContext->m_Info.Size;
                }
            }
            else if (const ViewportContext* l_Context = FindViewportContext(m_ActiveViewportId))
            {
                if (IsValidViewport(l_Context->m_Info))
                {
                    l_ViewportSize = l_Context->m_Info.Size;
                }
            }
            else
            {
                for (const std::pair<const uint32_t, ViewportContext>& it_ContextPair : m_ViewportContexts)
                {
                    if (IsValidViewport(it_ContextPair.second.m_Info))
                    {
                        l_ViewportSize = it_ContextPair.second.m_Info.Size;
                        break;
                    }
                }
            }

            // Keep the editor camera aligned with the authoring viewport so manipulator math remains predictable.
            m_EditorCamera->SetViewportSize(l_ViewportSize);
        }
    }

    void Renderer::SetRuntimeCamera(Camera* camera)
    {
        m_RuntimeCamera = camera;
        m_RuntimeCameraReady = false;
        if (!m_RuntimeCamera)
        {
            // Clearing the runtime camera prevents stale frames from showing while the scene searches for a replacement.
            return;
        }

        glm::vec2 l_ViewportSize{ static_cast<float>(m_Swapchain.GetExtent().width), static_cast<float>(m_Swapchain.GetExtent().height) };

        // Runtime output targets the game viewport (ID 2U); fall back to any active viewport if it has not initialised yet.
        if (const ViewportContext* l_GameContext = FindViewportContext(2U))
        {
            if (IsValidViewport(l_GameContext->m_Info))
            {
                l_ViewportSize = l_GameContext->m_Info.Size;
            }
        }
        else if (const ViewportContext* l_Context = FindViewportContext(m_ActiveViewportId))
        {
            if (IsValidViewport(l_Context->m_Info))
            {
                l_ViewportSize = l_Context->m_Info.Size;
            }
        }
        else
        {
            for (const std::pair<const uint32_t, ViewportContext>& it_ContextPair : m_ViewportContexts)
            {
                if (IsValidViewport(it_ContextPair.second.m_Info))
                {
                    l_ViewportSize = it_ContextPair.second.m_Info.Size;
                    break;
                }
            }
        }

        // Keep the runtime camera sized to its viewport so gameplay simulations respect their intended aspect ratio.
        m_RuntimeCamera->SetViewportSize(l_ViewportSize);
    }

    void Renderer::SetRuntimeCameraReady(bool ready)
    {
        // Guard the flag with the pointer so callers cannot accidentally mark a cleared camera as ready.
        m_RuntimeCameraReady = ready && (m_RuntimeCamera != nullptr);
    }

    void Renderer::SetClearColor(const glm::vec4& color)
    {
        // Persist the preferred clear colour so both render passes remain visually consistent.
        m_ClearColor = color;
    }

    VkDescriptorSet Renderer::GetViewportTexture(uint32_t viewportId) const
    {
        // Provide the descriptor set that ImGui::Image expects when the viewport is active.
        // The active camera routing happens elsewhere; this helper only surfaces the render target texture to ImGui.
        const ViewportContext* l_Context = FindViewportContext(viewportId);
        if (!l_Context || !IsValidViewport(l_Context->m_Info))
        {
            return VK_NULL_HANDLE;
        }

        const OffscreenTarget& l_Target = l_Context->m_Target;
        if (l_Target.m_TextureID != VK_NULL_HANDLE)
        {
            return l_Target.m_TextureID;
        }

        return VK_NULL_HANDLE;
    }

    const Camera* Renderer::GetActiveCamera() const
    {
        const ViewportContext* l_Context = FindViewportContext(m_ActiveViewportId);
        if (!l_Context)
        {
            // When no context is active prefer the editor camera to keep tools responsive, otherwise fall back to runtime.
            return m_EditorCamera ? m_EditorCamera : m_RuntimeCamera;
        }

        return GetActiveCamera(*l_Context);
    }

    glm::mat4 Renderer::GetViewportViewMatrix(uint32_t viewportId) const
    {
        const ViewportContext* l_Context = FindViewportContext(viewportId);
        const Camera* l_Camera = l_Context ? GetActiveCamera(*l_Context) : nullptr;
        if (!l_Camera)
        {
            return glm::mat4{ 1.0f };
        }

        return l_Camera->GetViewMatrix();
    }

    glm::mat4 Renderer::GetViewportProjectionMatrix(uint32_t viewportId) const
    {
        const ViewportContext* l_Context = FindViewportContext(viewportId);
        const Camera* l_Camera = l_Context ? GetActiveCamera(*l_Context) : nullptr;
        if (!l_Camera)
        {
            return glm::mat4{ 1.0f };
        }

        return l_Camera->GetProjectionMatrix();
    }

    void Renderer::SetViewport(uint32_t viewportId, const ViewportInfo& info)
    {
        const uint32_t l_PreviousViewportId = m_ActiveViewportId;
        m_ActiveViewportId = viewportId;

        ViewportContext& l_Context = GetOrCreateViewportContext(viewportId);
        l_Context.m_Info = info;
        l_Context.m_Info.ViewportID = viewportId;

        auto a_UpdateCameraSize = [info](Camera* targetCamera)
            {
                if (targetCamera)
                {
                    // Ensure the camera's projection matches the viewport so culling and mouse picking behave correctly.
                    targetCamera->SetViewportSize(info.Size);
                }
            };

        if (viewportId == 1U)
        {
            // Explicitly size the editor camera against the editor viewport. If it is missing fall back so rendering continues.
            if (m_EditorCamera)
            {
                a_UpdateCameraSize(m_EditorCamera);
            }
            else
            {
                a_UpdateCameraSize(m_RuntimeCamera);
            }
        }
        else if (viewportId == 2U)
        {
            // Runtime viewport pulls from the gameplay camera, falling back to editor output for inactive simulations.
            if (m_RuntimeCamera)
            {
                a_UpdateCameraSize(m_RuntimeCamera);
            }
            else
            {
                a_UpdateCameraSize(m_EditorCamera);
            }
        }
        else
        {
            // Additional viewports inherit editor sizing today; future multi-camera routing can specialise this branch.
            a_UpdateCameraSize(m_EditorCamera ? m_EditorCamera : m_RuntimeCamera);
        }

        if (!IsValidViewport(info))
        {
            // The viewport was closed or minimized, so free the auxiliary render target when possible.
            DestroyOffscreenResources(l_PreviousViewportId);

            return;
        }

        VkExtent2D l_RequestedExtent{};
        l_RequestedExtent.width = static_cast<uint32_t>(std::max(info.Size.x, 0.0f));
        l_RequestedExtent.height = static_cast<uint32_t>(std::max(info.Size.y, 0.0f));

        l_Context.m_CachedExtent = l_RequestedExtent;

        if (l_RequestedExtent.width == 0 || l_RequestedExtent.height == 0)
        {
            DestroyOffscreenResources(viewportId);

            return;
        }

        OffscreenTarget& l_Target = l_Context.m_Target;
        if (l_Target.m_Extent.width == l_RequestedExtent.width && l_Target.m_Extent.height == l_RequestedExtent.height)
        {
            // Nothing to do – the backing image already matches the requested size.
            return;
        }

        CreateOrResizeOffscreenResources(l_Target, l_RequestedExtent);

        // Future: consider pooling and recycling detached targets so background viewports can warm-start when reopened.
    }

    ViewportInfo Renderer::GetViewport() const
    {
        const ViewportContext* l_Context = FindViewportContext(m_ActiveViewportId);
        if (l_Context)
        {
            return l_Context->m_Info;
        }

        return {};
    }

    void Renderer::BuildSpriteGeometry()
    {
        // Ensure any previous allocation is cleared before creating a new quad.
        DestroySpriteGeometry();

        std::array<Vertex, 4> l_Vertices{};
        // Define a unit quad facing the camera so transforms can scale it to the desired size.
        l_Vertices[0].Position = { -0.5f, -0.5f, 0.0f };
        l_Vertices[1].Position = { 0.5f, -0.5f, 0.0f };
        l_Vertices[2].Position = { 0.5f, 0.5f, 0.0f };
        l_Vertices[3].Position = { -0.5f, 0.5f, 0.0f };

        for (Vertex& it_Vertex : l_Vertices)
        {
            it_Vertex.Normal = { 0.0f, 0.0f, -1.0f };   // Point towards the default camera direction.
            it_Vertex.Tangent = { 1.0f, 0.0f, 0.0f };  // Tangent aligned with X for normal mapping compatibility.
            it_Vertex.Bitangent = { 0.0f, 1.0f, 0.0f }; // Bitangent aligned with Y.
            it_Vertex.Color = { 1.0f, 1.0f, 1.0f };     // White default so tinting works consistently.
        }

        l_Vertices[0].TexCoord = { 0.0f, 0.0f };
        l_Vertices[1].TexCoord = { 1.0f, 0.0f };
        l_Vertices[2].TexCoord = { 1.0f, 1.0f };
        l_Vertices[3].TexCoord = { 0.0f, 1.0f };

        const std::array<uint32_t, 6> l_Indices{ 0, 2, 1, 0, 3, 2 };

        std::vector<Vertex> l_VertexData(l_Vertices.begin(), l_Vertices.end());
        std::vector<uint32_t> l_IndexData(l_Indices.begin(), l_Indices.end());

        m_Buffers.CreateVertexBuffer(l_VertexData, m_Commands.GetOneTimePool(), m_SpriteVertexBuffer, m_SpriteVertexMemory);
        m_Buffers.CreateIndexBuffer(l_IndexData, m_Commands.GetOneTimePool(), m_SpriteIndexBuffer, m_SpriteIndexMemory, m_SpriteIndexCount);

        if (m_SpriteIndexCount == 0)
        {
            m_SpriteIndexCount = static_cast<uint32_t>(l_Indices.size());
        }
    }

    void Renderer::DestroySpriteGeometry()
    {
        if (m_SpriteVertexBuffer != VK_NULL_HANDLE || m_SpriteVertexMemory != VK_NULL_HANDLE)
        {
            m_Buffers.DestroyBuffer(m_SpriteVertexBuffer, m_SpriteVertexMemory);
            m_SpriteVertexBuffer = VK_NULL_HANDLE;
            m_SpriteVertexMemory = VK_NULL_HANDLE;
        }

        if (m_SpriteIndexBuffer != VK_NULL_HANDLE || m_SpriteIndexMemory != VK_NULL_HANDLE)
        {
            m_Buffers.DestroyBuffer(m_SpriteIndexBuffer, m_SpriteIndexMemory);
            m_SpriteIndexBuffer = VK_NULL_HANDLE;
            m_SpriteIndexMemory = VK_NULL_HANDLE;
            m_SpriteIndexCount = 0;
        }
    }

    void Renderer::GatherMeshDraws()
    {
        m_MeshDrawCommands.clear();

        if (!m_Registry)
        {
            return;
        }

        const auto& l_Entities = m_Registry->GetEntities();
        // Reserve upfront so dynamic scenes with many meshes avoid repeated allocations.
        m_MeshDrawCommands.reserve(l_Entities.size());

        for (ECS::Entity it_Entity : l_Entities)
        {
            if (!m_Registry->HasComponent<MeshComponent>(it_Entity))
            {
                continue;
            }

            MeshComponent& l_MeshComponent = m_Registry->GetComponent<MeshComponent>(it_Entity);
            if (!l_MeshComponent.m_Visible)
            {
                continue;
            }

            if (l_MeshComponent.m_Primitive != MeshComponent::PrimitiveType::None && l_MeshComponent.m_MeshIndex == std::numeric_limits<size_t>::max())
            {
                // TODO: Once primitives upload procedural vertex/index buffers, enqueue them here.
                continue;
            }

            if (l_MeshComponent.m_MeshIndex >= m_MeshDrawInfo.size())
            {
                // The component references geometry that has not been uploaded yet. Future streaming
                // work can patch this once asynchronous loading lands.
                continue;
            }

            const MeshDrawInfo& l_DrawInfo = m_MeshDrawInfo[l_MeshComponent.m_MeshIndex];
            if (l_DrawInfo.m_IndexCount == 0)
            {
                continue;
            }

            glm::mat4 l_ModelMatrix{ 1.0f };
            if (m_Registry->HasComponent<Transform>(it_Entity))
            {
                l_ModelMatrix = ComposeTransform(m_Registry->GetComponent<Transform>(it_Entity));
            }

            MeshDrawCommand l_Command{};
            l_Command.m_ModelMatrix = l_ModelMatrix;
            l_Command.m_Component = &l_MeshComponent;
            l_Command.m_Entity = it_Entity;
            m_MeshDrawCommands.push_back(l_Command);
        }
    }

    void Renderer::GatherSpriteDraws()
    {
        m_SpriteDrawList.clear();

        if (!m_Registry)
        {
            return;
        }

        const auto& l_Entities = m_Registry->GetEntities();
        m_SpriteDrawList.reserve(l_Entities.size());

        for (ECS::Entity it_Entity : l_Entities)
        {
            if (!m_Registry->HasComponent<Transform>(it_Entity) || !m_Registry->HasComponent<SpriteComponent>(it_Entity))
            {
                continue;
            }

            SpriteComponent& l_Sprite = m_Registry->GetComponent<SpriteComponent>(it_Entity);
            if (!l_Sprite.m_Visible)
            {
                continue;
            }

            SpriteDrawCommand l_Command{};
            l_Command.m_ModelMatrix = ComposeTransform(m_Registry->GetComponent<Transform>(it_Entity));
            l_Command.m_Component = &l_Sprite;
            l_Command.m_Entity = it_Entity;

            m_SpriteDrawList.push_back(l_Command);
        }
    }

    void Renderer::DrawSprites(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        (void)imageIndex; // Reserved for future per-swapchain sprite atlas selection.

        if (m_SpriteDrawList.empty() || m_SpriteVertexBuffer == VK_NULL_HANDLE || m_SpriteIndexBuffer == VK_NULL_HANDLE)
        {
            return;
        }

        VkBuffer l_VertexBuffers[] = { m_SpriteVertexBuffer };
        VkDeviceSize l_Offsets[] = { 0 };
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, l_VertexBuffers, l_Offsets);
        vkCmdBindIndexBuffer(commandBuffer, m_SpriteIndexBuffer, 0, VK_INDEX_TYPE_UINT32);

        for (const SpriteDrawCommand& it_Command : m_SpriteDrawList)
        {
            if (it_Command.m_Component == nullptr)
            {
                continue;
            }

            RenderablePushConstant l_PushConstant{};
            l_PushConstant.m_ModelMatrix = it_Command.m_ModelMatrix;
            l_PushConstant.m_TintColor = it_Command.m_Component->m_TintColor;
            l_PushConstant.m_TextureScale = it_Command.m_Component->m_UVScale;
            l_PushConstant.m_TextureOffset = it_Command.m_Component->m_UVOffset;
            l_PushConstant.m_TilingFactor = it_Command.m_Component->m_TilingFactor;
            l_PushConstant.m_UseMaterialOverride = it_Command.m_Component->m_UseMaterialOverride ? 1 : 0;
            l_PushConstant.m_SortBias = it_Command.m_Component->m_SortOffset;
            l_PushConstant.m_TextureSlot = 0; // Sprites currently sample the default texture until atlas streaming is wired up.

            l_PushConstant.m_MaterialIndex = -1; // Sprites rely on texture tinting only for now.

            vkCmdPushConstants(commandBuffer, m_Pipeline.GetPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 
                sizeof(RenderablePushConstant), &l_PushConstant);
            vkCmdDrawIndexed(commandBuffer, m_SpriteIndexCount, 1, 0, 0, 0);
        }
    }

    void Renderer::RecreateSwapchain()
    {
        TR_CORE_TRACE("Recreating Swapchain");

        uint32_t l_Width = 0;
        uint32_t l_Height = 0;

        Startup::GetWindow().GetFramebufferSize(l_Width, l_Height);

        while (l_Width == 0 || l_Height == 0)
        {
            glfwWaitEvents();

            Startup::GetWindow().GetFramebufferSize(l_Width, l_Height);
        }

        vkDeviceWaitIdle(Startup::GetDevice());

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

            for (size_t i = 0; i < m_MaterialBuffers.size(); ++i)
            {
                m_Buffers.DestroyBuffer(m_MaterialBuffers[i], m_MaterialBuffersMemory[i]);
            }

            if (!m_DescriptorSets.empty())
            {
                // Free descriptor sets from the old pool so we can rebuild them cleanly.
                vkFreeDescriptorSets(Startup::GetDevice(), m_DescriptorPool, static_cast<uint32_t>(m_DescriptorSets.size()), m_DescriptorSets.data());
                m_DescriptorSets.clear();
            }

            DestroySkyboxDescriptorSets();

            if (m_DescriptorPool != VK_NULL_HANDLE)
            {
                // Tear down the descriptor pool so that we can rebuild it with the new descriptor counts.
                vkDestroyDescriptorPool(Startup::GetDevice(), m_DescriptorPool, nullptr);
                m_DescriptorPool = VK_NULL_HANDLE;
            }

            m_GlobalUniformBuffers.clear();
            m_GlobalUniformBuffersMemory.clear();
            m_MaterialBuffers.clear();
            m_MaterialBuffersMemory.clear();
            m_MaterialBufferDirty.clear();

            VkDeviceSize l_GlobalSize = sizeof(GlobalUniformBuffer);

            m_Buffers.CreateUniformBuffers(l_ImageCount, l_GlobalSize, m_GlobalUniformBuffers, m_GlobalUniformBuffersMemory);
            EnsureMaterialBufferCapacity(m_Materials.size());

            // Recreate the descriptor pool before allocating descriptor sets so the pool matches the new swapchain image count.
            CreateDescriptorPool();
            CreateDescriptorSets();

            TR_CORE_TRACE("Descriptor resources recreated (SwapchainImages = {}, GlobalUBOs = {}, MaterialBuffers = {}, CombinedSamplers = {}, DescriptorSets = {})",
                l_ImageCount, m_GlobalUniformBuffers.size(), m_MaterialBuffers.size(), l_ImageCount, m_DescriptorSets.size());
        }

        const ViewportContext* l_ActiveContext = FindViewportContext(m_ActiveViewportId);
        if (l_ActiveContext && IsValidViewport(l_ActiveContext->m_Info) && m_ActiveViewportId != 0)
        {
            VkExtent2D l_ViewportExtent{};
            l_ViewportExtent.width = static_cast<uint32_t>(std::max(l_ActiveContext->m_Info.Size.x, 0.0f));
            l_ViewportExtent.height = static_cast<uint32_t>(std::max(l_ActiveContext->m_Info.Size.y, 0.0f));

            if (l_ViewportExtent.width > 0 && l_ViewportExtent.height > 0)
            {
                ViewportContext& l_MutableContext = GetOrCreateViewportContext(m_ActiveViewportId);
                CreateOrResizeOffscreenResources(l_MutableContext.m_Target, l_ViewportExtent);
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

        uint32_t l_ImageCount = m_Swapchain.GetImageCount();
        VkDescriptorPoolSize l_PoolSizes[3]{};
        l_PoolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        l_PoolSizes[0].descriptorCount = l_ImageCount * 2; // Global UBO for the main pipeline plus the skybox uniform.
        l_PoolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        l_PoolSizes[1].descriptorCount = l_ImageCount; // Material storage buffer bound once per swapchain image.
        l_PoolSizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        // Each swapchain image consumes an array of material textures plus a cubemap sampler in the main set and a cubemap
        // sampler in the dedicated skybox set.
        l_PoolSizes[2].descriptorCount = l_ImageCount * (Pipeline::s_MaxMaterialTextures + 2);

        VkDescriptorPoolCreateInfo l_PoolInfo{};
        l_PoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        // We free and recreate descriptor sets whenever the swapchain is resized, so enable free-descriptor support.
        l_PoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        l_PoolInfo.poolSizeCount = 3;
        l_PoolInfo.pPoolSizes = l_PoolSizes;
        l_PoolInfo.maxSets = l_ImageCount * 2; // Main render pipeline + dedicated skybox descriptors.

        if (vkCreateDescriptorPool(Startup::GetDevice(), &l_PoolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create descriptor pool");
        }

        TR_CORE_TRACE("Descriptor Pool Created (MaxSets = {})", l_PoolInfo.maxSets);
    }

    void Renderer::CreateDefaultTexture()
    {
        TR_CORE_TRACE("Creating Default Texture");

        for (TextureSlot& it_Slot : m_TextureSlots)
        {
            DestroyTextureSlot(it_Slot);
        }
        m_TextureSlots.clear();
        m_TextureSlotLookup.clear();

        Loader::TextureData l_DefaultData{};
        l_DefaultData.Width = 1;
        l_DefaultData.Height = 1;
        l_DefaultData.Channels = 4;
        l_DefaultData.Pixels = { 0xFF, 0xFF, 0xFF, 0xFF };

        TextureSlot l_DefaultSlot{};
        if (!PopulateTextureSlot(l_DefaultSlot, l_DefaultData))
        {
            TR_CORE_CRITICAL("Failed to create default texture slot");
            return;
        }

        l_DefaultSlot.m_SourcePath = kDefaultTextureKey;
        m_TextureSlots.push_back(std::move(l_DefaultSlot));
        m_TextureSlotLookup.emplace(kDefaultTextureKey, 0u);

        EnsureTextureDescriptorCapacity();
        RefreshTextureDescriptorBindings();

        TR_CORE_TRACE("Default Texture Created");
    }

    void Renderer::DestroyTextureSlot(TextureSlot& slot)
    {
        VkDevice l_Device = Startup::GetDevice();

        if (slot.m_Sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(l_Device, slot.m_Sampler, nullptr);
            slot.m_Sampler = VK_NULL_HANDLE;
        }

        if (slot.m_View != VK_NULL_HANDLE)
        {
            vkDestroyImageView(l_Device, slot.m_View, nullptr);
            slot.m_View = VK_NULL_HANDLE;
        }

        if (slot.m_Image != VK_NULL_HANDLE)
        {
            vkDestroyImage(l_Device, slot.m_Image, nullptr);
            slot.m_Image = VK_NULL_HANDLE;
        }

        if (slot.m_Memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(l_Device, slot.m_Memory, nullptr);
            slot.m_Memory = VK_NULL_HANDLE;
        }

        slot.m_Descriptor = {};
    }

    bool Renderer::PopulateTextureSlot(TextureSlot& slot, const Loader::TextureData& textureData)
    {
        DestroyTextureSlot(slot);

        if (textureData.Pixels.empty() || textureData.Width <= 0 || textureData.Height <= 0)
        {
            return false;
        }

        VkDevice l_Device = Startup::GetDevice();
        const VkDeviceSize l_ImageSize = static_cast<VkDeviceSize>(textureData.Pixels.size());

        VkBuffer l_StagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory l_StagingMemory = VK_NULL_HANDLE;
        m_Buffers.CreateBuffer(l_ImageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, l_StagingBuffer, l_StagingMemory);

        void* l_Data = nullptr;
        vkMapMemory(l_Device, l_StagingMemory, 0, l_ImageSize, 0, &l_Data);
        std::memcpy(l_Data, textureData.Pixels.data(), static_cast<size_t>(l_ImageSize));
        vkUnmapMemory(l_Device, l_StagingMemory);

        VkImageCreateInfo l_ImageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        l_ImageInfo.imageType = VK_IMAGE_TYPE_2D;
        l_ImageInfo.extent.width = static_cast<uint32_t>(std::max(textureData.Width, 1));
        l_ImageInfo.extent.height = static_cast<uint32_t>(std::max(textureData.Height, 1));
        l_ImageInfo.extent.depth = 1;
        l_ImageInfo.mipLevels = 1;
        l_ImageInfo.arrayLayers = 1;
        l_ImageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        l_ImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        l_ImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        l_ImageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        l_ImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        l_ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

        if (vkCreateImage(l_Device, &l_ImageInfo, nullptr, &slot.m_Image) != VK_SUCCESS)
        {
            m_Buffers.DestroyBuffer(l_StagingBuffer, l_StagingMemory);
            DestroyTextureSlot(slot);
            return false;
        }

        VkMemoryRequirements l_MemoryRequirements{};
        vkGetImageMemoryRequirements(l_Device, slot.m_Image, &l_MemoryRequirements);

        VkMemoryAllocateInfo l_AllocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        l_AllocInfo.allocationSize = l_MemoryRequirements.size;
        l_AllocInfo.memoryTypeIndex = m_Buffers.FindMemoryType(l_MemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(l_Device, &l_AllocInfo, nullptr, &slot.m_Memory) != VK_SUCCESS)
        {
            m_Buffers.DestroyBuffer(l_StagingBuffer, l_StagingMemory);
            DestroyTextureSlot(slot);
            return false;
        }

        vkBindImageMemory(l_Device, slot.m_Image, slot.m_Memory, 0);

        VkCommandBuffer l_CommandBuffer = m_Commands.BeginSingleTimeCommands();

        VkImageMemoryBarrier l_BarrierToTransfer{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        l_BarrierToTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        l_BarrierToTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        l_BarrierToTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_BarrierToTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_BarrierToTransfer.image = slot.m_Image;
        l_BarrierToTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_BarrierToTransfer.subresourceRange.baseMipLevel = 0;
        l_BarrierToTransfer.subresourceRange.levelCount = 1;
        l_BarrierToTransfer.subresourceRange.baseArrayLayer = 0;
        l_BarrierToTransfer.subresourceRange.layerCount = 1;
        l_BarrierToTransfer.srcAccessMask = 0;
        l_BarrierToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(l_CommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &l_BarrierToTransfer);

        VkBufferImageCopy l_CopyRegion{};
        l_CopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_CopyRegion.imageSubresource.mipLevel = 0;
        l_CopyRegion.imageSubresource.baseArrayLayer = 0;
        l_CopyRegion.imageSubresource.layerCount = 1;
        l_CopyRegion.imageOffset = { 0, 0, 0 };
        l_CopyRegion.imageExtent = { l_ImageInfo.extent.width, l_ImageInfo.extent.height, 1 };

        vkCmdCopyBufferToImage(l_CommandBuffer, l_StagingBuffer, slot.m_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &l_CopyRegion);

        VkImageMemoryBarrier l_BarrierToShader{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        l_BarrierToShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        l_BarrierToShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        l_BarrierToShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_BarrierToShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_BarrierToShader.image = slot.m_Image;
        l_BarrierToShader.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_BarrierToShader.subresourceRange.baseMipLevel = 0;
        l_BarrierToShader.subresourceRange.levelCount = 1;
        l_BarrierToShader.subresourceRange.baseArrayLayer = 0;
        l_BarrierToShader.subresourceRange.layerCount = 1;
        l_BarrierToShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        l_BarrierToShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(l_CommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &l_BarrierToShader);

        m_Commands.EndSingleTimeCommands(l_CommandBuffer);

        m_Buffers.DestroyBuffer(l_StagingBuffer, l_StagingMemory);

        VkImageViewCreateInfo l_ViewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        l_ViewInfo.image = slot.m_Image;
        l_ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        l_ViewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        l_ViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_ViewInfo.subresourceRange.baseMipLevel = 0;
        l_ViewInfo.subresourceRange.levelCount = 1;
        l_ViewInfo.subresourceRange.baseArrayLayer = 0;
        l_ViewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(l_Device, &l_ViewInfo, nullptr, &slot.m_View) != VK_SUCCESS)
        {
            DestroyTextureSlot(slot);
            return false;
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

        if (vkCreateSampler(l_Device, &l_SamplerInfo, nullptr, &slot.m_Sampler) != VK_SUCCESS)
        {
            DestroyTextureSlot(slot);
            return false;
        }

        slot.m_Descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        slot.m_Descriptor.imageView = slot.m_View;
        slot.m_Descriptor.sampler = slot.m_Sampler;

        return true;
    }

    void Renderer::EnsureTextureDescriptorCapacity()
    {
        if (m_TextureDescriptorCache.size() != Pipeline::s_MaxMaterialTextures)
        {
            m_TextureDescriptorCache.resize(Pipeline::s_MaxMaterialTextures);
        }
    }

    void Renderer::RefreshTextureDescriptorBindings()
    {
        if (m_DescriptorSets.empty())
        {
            return;
        }

        if (m_TextureSlots.empty())
        {
            TR_CORE_WARN("Texture descriptor refresh skipped because no slots were initialised");
            return;
        }

        EnsureTextureDescriptorCapacity();

        const VkDescriptorImageInfo l_DefaultDescriptor = m_TextureSlots.front().m_Descriptor;
        for (uint32_t it_Index = 0; it_Index < Pipeline::s_MaxMaterialTextures; ++it_Index)
        {
            if (it_Index < m_TextureSlots.size())
            {
                m_TextureDescriptorCache[it_Index] = m_TextureSlots[it_Index].m_Descriptor;
            }
            else
            {
                m_TextureDescriptorCache[it_Index] = l_DefaultDescriptor;
            }
        }

        for (VkDescriptorSet l_Set : m_DescriptorSets)
        {
            VkWriteDescriptorSet l_TextureWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            l_TextureWrite.dstSet = l_Set;
            l_TextureWrite.dstBinding = 2;
            l_TextureWrite.dstArrayElement = 0;
            l_TextureWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            l_TextureWrite.descriptorCount = Pipeline::s_MaxMaterialTextures;
            l_TextureWrite.pImageInfo = m_TextureDescriptorCache.data();

            vkUpdateDescriptorSets(Startup::GetDevice(), 1, &l_TextureWrite, 0, nullptr);
        }
    }

    std::string Renderer::NormalizeTexturePath(const std::string& texturePath) const
    {
        if (texturePath.empty())
        {
            return {};
        }

        return Utilities::FileManagement::NormalizePath(texturePath);
    }

    uint32_t Renderer::AcquireTextureSlot(const std::string& normalizedPath, const Loader::TextureData& textureData)
    {
        if (normalizedPath.empty())
        {
            return 0;
        }

        auto a_Existing = m_TextureSlotLookup.find(normalizedPath);
        if (a_Existing != m_TextureSlotLookup.end())
        {
            return a_Existing->second;
        }

        if (m_TextureSlots.size() >= Pipeline::s_MaxMaterialTextures)
        {
            TR_CORE_WARN("Material texture budget ({}) exhausted. {} will fall back to the default slot.",
                Pipeline::s_MaxMaterialTextures, normalizedPath.c_str());
        }
        else if (!textureData.Pixels.empty())
        {
            TextureSlot l_NewSlot{};
            if (PopulateTextureSlot(l_NewSlot, textureData))
            {
                l_NewSlot.m_SourcePath = normalizedPath;
                m_TextureSlots.push_back(std::move(l_NewSlot));
                const uint32_t l_NewIndex = static_cast<uint32_t>(m_TextureSlots.size() - 1);
                m_TextureSlotLookup.emplace(normalizedPath, l_NewIndex);
                return l_NewIndex;
            }

            TR_CORE_WARN("Failed to upload texture '{}'. Using the default slot instead.", normalizedPath.c_str());
        }
        else
        {
            TR_CORE_WARN("Texture '{}' provided no pixel data. Using the default slot instead.", normalizedPath.c_str());
        }

        m_TextureSlotLookup.emplace(normalizedPath, 0u);
        return 0;
    }

    void Renderer::ResolveMaterialTextureSlots(const std::vector<std::string>& textures, size_t materialOffset, size_t materialCount)
    {
        if (m_Materials.empty())
        {
            return;
        }

        const size_t l_SafeOffset = std::min(materialOffset, m_Materials.size());
        const size_t l_ResolvedCount = std::min(materialCount, m_Materials.size() - l_SafeOffset);

        std::vector<std::string> l_NormalizedPaths{};
        l_NormalizedPaths.reserve(textures.size());

        for (const std::string& it_Path : textures)
        {
            std::string l_Normalized = NormalizeTexturePath(it_Path);
            l_NormalizedPaths.push_back(l_Normalized);

            if (l_Normalized.empty() || m_TextureSlotLookup.find(l_Normalized) != m_TextureSlotLookup.end())
            {
                continue;
            }

            Loader::TextureData l_TextureData = Loader::TextureLoader::Load(l_Normalized);
            AcquireTextureSlot(l_Normalized, l_TextureData);
        }

        for (size_t it_Index = 0; it_Index < l_ResolvedCount; ++it_Index)
        {
            Geometry::Material& l_Material = m_Materials[l_SafeOffset + it_Index];
            l_Material.BaseColorTextureSlot = 0; // Fallback to the default white texture when the source is missing.

            if (l_Material.BaseColorTextureIndex < 0)
            {
                continue;
            }

            const size_t l_TextureIndex = static_cast<size_t>(l_Material.BaseColorTextureIndex);
            if (l_TextureIndex >= l_NormalizedPaths.size())
            {
                continue;
            }

            const std::string& l_Path = l_NormalizedPaths[l_TextureIndex];
            if (l_Path.empty())
            {
                continue;
            }

            auto a_Mapping = m_TextureSlotLookup.find(l_Path);
            if (a_Mapping != m_TextureSlotLookup.end())
            {
                l_Material.BaseColorTextureSlot = static_cast<int32_t>(a_Mapping->second);
            }
        }

        RefreshTextureDescriptorBindings();
    }

    void Renderer::CreateDefaultSkybox()
    {
        TR_CORE_TRACE("Creating Default Skybox");

        // Build a placeholder cubemap so the dedicated skybox shaders have a valid texture binding.
        CreateSkyboxCubemap();

        m_Skybox.Init(m_Buffers, m_Commands.GetOneTimePool());

        TR_CORE_TRACE("Default Skybox Created");
    }

    void Renderer::CreateSkyboxCubemap()
    {
        DestroySkyboxCubemap();

        TR_CORE_TRACE("Creating skybox cubemap");

        Loader::CubemapTextureData l_CubemapData{};
        std::string l_CubemapSource{};

        // Try loading a pre-authored cubemap from disk. This keeps the renderer flexible and allows
        // artists to swap between KTX packages and loose face images without touching the code.
        const std::filesystem::path l_DefaultSkyboxRoot = std::filesystem::path("Trident-Forge") / "Assets" / "Skyboxes";
        const std::filesystem::path l_DefaultKtx = l_DefaultSkyboxRoot / "DefaultSkybox.ktx";
        std::error_code l_FileError{};
        if (std::filesystem::exists(l_DefaultKtx, l_FileError))
        {
            l_CubemapData = Loader::SkyboxTextureLoader::LoadFromKtx(l_DefaultKtx);
            l_CubemapSource = "DefaultSkybox.ktx";
        }
        else
        {
            const std::filesystem::path l_DefaultFaces = l_DefaultSkyboxRoot / "Default";
            if (std::filesystem::exists(l_DefaultFaces, l_FileError))
            {
                l_CubemapData = Loader::SkyboxTextureLoader::LoadFromDirectory(l_DefaultFaces);
                l_CubemapSource = "Default directory";
            }
            else if (std::filesystem::is_directory(l_DefaultSkyboxRoot, l_FileError))
            {
                // Discover loose PNG faces named using the short px/nx/... format so older assets continue to load.
                std::array<std::filesystem::path, 6> l_FallbackFaces{};
                std::array<bool, 6> l_FoundFaces{};
                const auto a_ToLower = [](std::string text)
                    {
                        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character)
                            {
                                return static_cast<char>(std::tolower(character));
                            });
                        return text;
                    };
                constexpr std::array<std::array<std::string_view, 2>, 6> s_ShortFaceTokens{ {
                    { "posx", "px" },
                    { "negx", "nx" },
                    { "posy", "py" },
                    { "negy", "ny" },
                    { "posz", "pz" },
                    { "negz", "nz" }
                } };

                std::error_code l_IterError{};
                for (const auto& it_Entry : std::filesystem::directory_iterator(l_DefaultSkyboxRoot, l_IterError))
                {
                    if (!it_Entry.is_regular_file())
                    {
                        continue;
                    }

                    std::string l_StemLower = a_ToLower(it_Entry.path().stem().string());
                    for (size_t it_FaceIndex = 0; it_FaceIndex < s_ShortFaceTokens.size(); ++it_FaceIndex)
                    {
                        if (l_FoundFaces[it_FaceIndex])
                        {
                            continue;
                        }

                        for (std::string_view it_Token : s_ShortFaceTokens[it_FaceIndex])
                        {
                            if (it_Token.empty())
                            {
                                continue;
                            }

                            if (l_StemLower.find(it_Token) != std::string::npos)
                            {
                                l_FallbackFaces[it_FaceIndex] = it_Entry.path();
                                l_FoundFaces[it_FaceIndex] = true;
                                break;
                            }
                        }
                    }
                }

                if (l_IterError)
                {
                    TR_CORE_ERROR("Failed to enumerate fallback skybox directory '{}': {}", l_DefaultSkyboxRoot.string(), l_IterError.message());
                }

                const bool l_AllFacesDiscovered = std::all_of(l_FoundFaces.begin(), l_FoundFaces.end(), [](bool found)
                    {
                        return found;
                    });

                if (l_AllFacesDiscovered)
                {
                    l_CubemapData = Loader::SkyboxTextureLoader::LoadFromFaces(l_FallbackFaces);
                    l_CubemapSource = "PNG fallback";
                }
                else
                {
                    TR_CORE_WARN("PNG fallback skybox faces are incomplete; expected 6 unique matches in '{}'", l_DefaultSkyboxRoot.string());
                }
            }
        }

        if (!l_CubemapData.IsValid())
        {
            TR_CORE_WARN("Falling back to a solid colour cubemap because no skybox textures were found on disk");
            l_CubemapData = Loader::CubemapTextureData::CreateSolidColor(0x808080);
        }
        else if (!l_CubemapSource.empty())
        {
            TR_CORE_INFO("Skybox cubemap successfully loaded from {}", l_CubemapSource);
            // TODO: Extend this path with HDR pre-integration and IBL convolutions when the asset pipeline provides them.
        }

        if (l_CubemapData.m_Format == VK_FORMAT_UNDEFINED)
        {
            l_CubemapData.m_Format = VK_FORMAT_R8G8B8A8_SRGB;
        }

        const VkDeviceSize l_StagingSize = static_cast<VkDeviceSize>(l_CubemapData.m_PixelData.size());
        if (l_StagingSize == 0)
        {
            TR_CORE_CRITICAL("Skybox cubemap contains no pixel data");
            return;
        }

        // Stage 1: allocate a CPU-visible staging buffer so the pixel data can be uploaded efficiently.
        VkBuffer l_StagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory l_StagingMemory = VK_NULL_HANDLE;
        m_Buffers.CreateBuffer(l_StagingSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, l_StagingBuffer, l_StagingMemory);

        void* l_Data = nullptr;
        vkMapMemory(Startup::GetDevice(), l_StagingMemory, 0, l_StagingSize, 0, &l_Data);
        std::memcpy(l_Data, l_CubemapData.m_PixelData.data(), static_cast<size_t>(l_StagingSize));
        vkUnmapMemory(Startup::GetDevice(), l_StagingMemory);

        // Stage 2: create the GPU image backing the cubemap. Keeping the sample count and usage flags
        // conservative for now leaves room for future HDR/IBL upgrades without reallocating everything.
        VkImageCreateInfo l_ImageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        l_ImageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        l_ImageInfo.imageType = VK_IMAGE_TYPE_2D;
        l_ImageInfo.extent.width = l_CubemapData.m_Width;
        l_ImageInfo.extent.height = l_CubemapData.m_Height;
        l_ImageInfo.extent.depth = 1;
        l_ImageInfo.mipLevels = l_CubemapData.m_MipCount;
        l_ImageInfo.arrayLayers = 6;
        l_ImageInfo.format = l_CubemapData.m_Format;
        l_ImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        l_ImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        l_ImageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        l_ImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        l_ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

        if (vkCreateImage(Startup::GetDevice(), &l_ImageInfo, nullptr, &m_SkyboxTextureImage) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create skybox image");
        }

        VkMemoryRequirements l_ImageRequirements{};
        vkGetImageMemoryRequirements(Startup::GetDevice(), m_SkyboxTextureImage, &l_ImageRequirements);

        VkMemoryAllocateInfo l_AllocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        l_AllocInfo.allocationSize = l_ImageRequirements.size;
        l_AllocInfo.memoryTypeIndex = m_Buffers.FindMemoryType(l_ImageRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(Startup::GetDevice(), &l_AllocInfo, nullptr, &m_SkyboxTextureImageMemory) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to allocate skybox image memory");
        }

        vkBindImageMemory(Startup::GetDevice(), m_SkyboxTextureImage, m_SkyboxTextureImageMemory, 0);

        // Stage 3: record layout transitions and buffer copies. Future async streaming can split this
        // block so uploads happen on dedicated transfer queues.
        VkCommandBuffer l_CommandBuffer = m_Commands.BeginSingleTimeCommands();

        VkImageMemoryBarrier l_BarrierToTransfer{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        l_BarrierToTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        l_BarrierToTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        l_BarrierToTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_BarrierToTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_BarrierToTransfer.image = m_SkyboxTextureImage;
        l_BarrierToTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_BarrierToTransfer.subresourceRange.baseMipLevel = 0;
        l_BarrierToTransfer.subresourceRange.levelCount = l_CubemapData.m_MipCount;
        l_BarrierToTransfer.subresourceRange.baseArrayLayer = 0;
        l_BarrierToTransfer.subresourceRange.layerCount = 6;
        l_BarrierToTransfer.srcAccessMask = 0;
        l_BarrierToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(l_CommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            0, nullptr, 0, nullptr, 1, &l_BarrierToTransfer);

        std::vector<VkBufferImageCopy> l_CopyRegions{};
        l_CopyRegions.reserve(l_CubemapData.m_FaceRegions.size() * 6);

        for (uint32_t it_Mip = 0; it_Mip < l_CubemapData.m_MipCount; ++it_Mip)
        {
            uint32_t l_MipWidth = std::max(1u, l_CubemapData.m_Width >> it_Mip);
            uint32_t l_MipHeight = std::max(1u, l_CubemapData.m_Height >> it_Mip);

            for (uint32_t it_Face = 0; it_Face < 6; ++it_Face)
            {
                const Loader::CubemapFaceRegion& l_Region = l_CubemapData.m_FaceRegions[it_Mip][it_Face];

                VkBufferImageCopy l_CopyRegion{};
                l_CopyRegion.bufferOffset = static_cast<VkDeviceSize>(l_Region.m_Offset);
                l_CopyRegion.bufferRowLength = 0;
                l_CopyRegion.bufferImageHeight = 0;
                l_CopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                l_CopyRegion.imageSubresource.mipLevel = it_Mip;
                l_CopyRegion.imageSubresource.baseArrayLayer = it_Face;
                l_CopyRegion.imageSubresource.layerCount = 1;
                l_CopyRegion.imageOffset = { 0, 0, 0 };
                l_CopyRegion.imageExtent = { l_MipWidth, l_MipHeight, 1 };

                l_CopyRegions.push_back(l_CopyRegion);
            }
        }

        vkCmdCopyBufferToImage(l_CommandBuffer, l_StagingBuffer, m_SkyboxTextureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            static_cast<uint32_t>(l_CopyRegions.size()), l_CopyRegions.data());

        VkImageMemoryBarrier l_BarrierToShader{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        l_BarrierToShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        l_BarrierToShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        l_BarrierToShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_BarrierToShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_BarrierToShader.image = m_SkyboxTextureImage;
        l_BarrierToShader.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_BarrierToShader.subresourceRange.baseMipLevel = 0;
        l_BarrierToShader.subresourceRange.levelCount = l_CubemapData.m_MipCount;
        l_BarrierToShader.subresourceRange.baseArrayLayer = 0;
        l_BarrierToShader.subresourceRange.layerCount = 6;
        l_BarrierToShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        l_BarrierToShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(l_CommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
            0, nullptr, 0, nullptr, 1, &l_BarrierToShader);

        m_Commands.EndSingleTimeCommands(l_CommandBuffer);

        m_Buffers.DestroyBuffer(l_StagingBuffer, l_StagingMemory);

        // Stage 4: create the view and sampler consumed by the skybox shaders. Sampling remains simple for now
        // but clamped addressing keeps seams hidden when future HDR content arrives.
        VkImageViewCreateInfo l_ViewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        l_ViewInfo.image = m_SkyboxTextureImage;
        l_ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        l_ViewInfo.format = l_CubemapData.m_Format;
        l_ViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_ViewInfo.subresourceRange.baseMipLevel = 0;
        l_ViewInfo.subresourceRange.levelCount = l_CubemapData.m_MipCount;
        l_ViewInfo.subresourceRange.baseArrayLayer = 0;
        l_ViewInfo.subresourceRange.layerCount = 6;

        if (vkCreateImageView(Startup::GetDevice(), &l_ViewInfo, nullptr, &m_SkyboxTextureView) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create skybox view");
        }

        VkSamplerCreateInfo l_SamplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        l_SamplerInfo.magFilter = VK_FILTER_LINEAR;
        l_SamplerInfo.minFilter = VK_FILTER_LINEAR;
        l_SamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        l_SamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        l_SamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        l_SamplerInfo.anisotropyEnable = VK_FALSE;
        l_SamplerInfo.maxAnisotropy = 1.0f;
        l_SamplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        l_SamplerInfo.unnormalizedCoordinates = VK_FALSE;
        l_SamplerInfo.compareEnable = VK_FALSE;
        l_SamplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        l_SamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        l_SamplerInfo.mipLodBias = 0.0f;
        l_SamplerInfo.minLod = 0.0f;
        l_SamplerInfo.maxLod = static_cast<float>(l_CubemapData.m_MipCount);

        if (vkCreateSampler(Startup::GetDevice(), &l_SamplerInfo, nullptr, &m_SkyboxTextureSampler) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create skybox sampler");
        }

        CreateSkyboxDescriptorSets();

        // Stage 5: rewrite the main descriptor sets so forward lighting shaders can sample the cubemap.
        UpdateSkyboxBindingOnMainSets();

        // Leave space for HDR workflow upgrades (prefiltered mip chains, BRDF LUTs) without rewriting the upload path.
    }

    void Renderer::DestroySkyboxCubemap()
    {
        VkDevice l_Device = Startup::GetDevice();

        if (m_SkyboxTextureSampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(l_Device, m_SkyboxTextureSampler, nullptr);
            m_SkyboxTextureSampler = VK_NULL_HANDLE;
        }

        if (m_SkyboxTextureView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(l_Device, m_SkyboxTextureView, nullptr);
            m_SkyboxTextureView = VK_NULL_HANDLE;
        }

        if (m_SkyboxTextureImage != VK_NULL_HANDLE)
        {
            vkDestroyImage(l_Device, m_SkyboxTextureImage, nullptr);
            m_SkyboxTextureImage = VK_NULL_HANDLE;
        }

        if (m_SkyboxTextureImageMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(l_Device, m_SkyboxTextureImageMemory, nullptr);
            m_SkyboxTextureImageMemory = VK_NULL_HANDLE;
        }
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
        if (vkAllocateDescriptorSets(Startup::GetDevice(), &l_AllocateInfo, m_DescriptorSets.data()) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to allocate descriptor sets");
        }

        const VkDeviceSize l_MaterialRange = static_cast<VkDeviceSize>(std::max<size_t>(m_MaterialBufferElementCount, static_cast<size_t>(1)) * sizeof(MaterialUniformBuffer));

        for (size_t i = 0; i < l_ImageCount; ++i)
        {
            VkDescriptorBufferInfo l_GlobalBufferInfo{};
            l_GlobalBufferInfo.buffer = m_GlobalUniformBuffers[i];
            l_GlobalBufferInfo.offset = 0;
            l_GlobalBufferInfo.range = sizeof(GlobalUniformBuffer);

            VkDescriptorBufferInfo l_MaterialBufferInfo{};
            l_MaterialBufferInfo.buffer = (i < m_MaterialBuffers.size()) ? m_MaterialBuffers[i] : VK_NULL_HANDLE;
            l_MaterialBufferInfo.offset = 0;
            l_MaterialBufferInfo.range = l_MaterialRange;

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
            l_MaterialWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            l_MaterialWrite.descriptorCount = 1;
            l_MaterialWrite.pBufferInfo = &l_MaterialBufferInfo;

            VkWriteDescriptorSet l_Writes[] = { l_GlobalWrite, l_MaterialWrite };
            vkUpdateDescriptorSets(Startup::GetDevice(), 2, l_Writes, 0, nullptr);
        }

        RefreshTextureDescriptorBindings();
        UpdateSkyboxBindingOnMainSets();
        CreateSkyboxDescriptorSets();

        TR_CORE_TRACE("Descriptor Sets Allocated (Main = {}, Skybox = {})", l_ImageCount, m_SkyboxDescriptorSets.size());

        MarkMaterialBuffersDirty();
    }

    void Renderer::UpdateSkyboxBindingOnMainSets()
    {
        if (m_DescriptorSets.empty())
        {
            return;
        }

        if (m_SkyboxTextureView == VK_NULL_HANDLE || m_SkyboxTextureSampler == VK_NULL_HANDLE)
        {
            TR_CORE_WARN("Skipping skybox binding update because the cubemap view or sampler is missing");
            return;
        }

        for (size_t it_Image = 0; it_Image < m_DescriptorSets.size(); ++it_Image)
        {
            VkDescriptorImageInfo l_SkyboxInfo{};
            l_SkyboxInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            l_SkyboxInfo.imageView = m_SkyboxTextureView;
            l_SkyboxInfo.sampler = m_SkyboxTextureSampler;

            VkWriteDescriptorSet l_SkyboxWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            l_SkyboxWrite.dstSet = m_DescriptorSets[it_Image];
            l_SkyboxWrite.dstBinding = 3;
            l_SkyboxWrite.dstArrayElement = 0;
            l_SkyboxWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            l_SkyboxWrite.descriptorCount = 1;
            l_SkyboxWrite.pImageInfo = &l_SkyboxInfo;

            // Synchronization note: descriptor writes are host operations, but consumers must still wait for the
            // transfer commands recorded in CreateSkyboxCubemap to finish before sampling. Our frame fence makes
            // sure the upload completes before the next draw touches the cubemap.
            vkUpdateDescriptorSets(Startup::GetDevice(), 1, &l_SkyboxWrite, 0, nullptr);
        }
    }

    void Renderer::EnsureMaterialBufferCapacity(size_t materialCount)
    {
        const size_t l_ImageCount = m_Swapchain.GetImageCount();
        const size_t l_RequiredCount = std::max(materialCount, static_cast<size_t>(1));

        if (l_ImageCount == 0)
        {
            // Record the desired size so the next swapchain creation allocates the correct capacity.
            m_MaterialBufferElementCount = l_RequiredCount;
            m_MaterialBuffers.clear();
            m_MaterialBuffersMemory.clear();
            m_MaterialBufferDirty.clear();
            return;
        }

        const bool l_SizeMismatch = (l_RequiredCount != m_MaterialBufferElementCount);
        const bool l_ImageMismatch = (m_MaterialBuffers.size() != l_ImageCount);
        if (!l_SizeMismatch && !l_ImageMismatch)
        {
            return;
        }

        if (!m_MaterialBuffers.empty() && m_ResourceFence != VK_NULL_HANDLE)
        {
            // Guarantee that no in-flight draw is still referencing the previous buffers before we recycle them.
            vkWaitForFences(Startup::GetDevice(), 1, &m_ResourceFence, VK_TRUE, UINT64_MAX);
        }

        for (size_t it_Index = 0; it_Index < m_MaterialBuffers.size(); ++it_Index)
        {
            m_Buffers.DestroyBuffer(m_MaterialBuffers[it_Index], m_MaterialBuffersMemory[it_Index]);
        }

        const VkDeviceSize l_BufferSize = static_cast<VkDeviceSize>(l_RequiredCount * sizeof(MaterialUniformBuffer));
        m_Buffers.CreateStorageBuffers(static_cast<uint32_t>(l_ImageCount), l_BufferSize, m_MaterialBuffers, m_MaterialBuffersMemory);

        m_MaterialBufferElementCount = l_RequiredCount;
        MarkMaterialBuffersDirty();
        UpdateMaterialDescriptorBindings();
    }

    void Renderer::UpdateMaterialDescriptorBindings()
    {
        if (m_DescriptorSets.empty() || m_MaterialBuffers.empty())
        {
            return;
        }

        const VkDeviceSize l_MaterialRange = static_cast<VkDeviceSize>(std::max<size_t>(m_MaterialBufferElementCount, static_cast<size_t>(1)) * sizeof(MaterialUniformBuffer));
        for (size_t it_Image = 0; it_Image < m_DescriptorSets.size() && it_Image < m_MaterialBuffers.size(); ++it_Image)
        {
            VkDescriptorBufferInfo l_MaterialInfo{};
            l_MaterialInfo.buffer = m_MaterialBuffers[it_Image];
            l_MaterialInfo.offset = 0;
            l_MaterialInfo.range = l_MaterialRange;

            VkWriteDescriptorSet l_MaterialWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            l_MaterialWrite.dstSet = m_DescriptorSets[it_Image];
            l_MaterialWrite.dstBinding = 1;
            l_MaterialWrite.dstArrayElement = 0;
            l_MaterialWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            l_MaterialWrite.descriptorCount = 1;
            l_MaterialWrite.pBufferInfo = &l_MaterialInfo;

            vkUpdateDescriptorSets(Startup::GetDevice(), 1, &l_MaterialWrite, 0, nullptr);
        }
    }

    void Renderer::MarkMaterialBuffersDirty()
    {
        const size_t l_ImageCount = m_Swapchain.GetImageCount();
        m_MaterialBufferDirty.resize(l_ImageCount);
        std::fill(m_MaterialBufferDirty.begin(), m_MaterialBufferDirty.end(), true);
    }

    void Renderer::CreateSkyboxDescriptorSets()
    {
        DestroySkyboxDescriptorSets();

        size_t l_ImageCount = m_Swapchain.GetImageCount();
        if (l_ImageCount == 0 || m_SkyboxTextureView == VK_NULL_HANDLE || m_SkyboxTextureSampler == VK_NULL_HANDLE)
        {
            TR_CORE_WARN("Skipped skybox descriptor allocation because required resources are missing");
            return;
        }

        std::vector<VkDescriptorSetLayout> l_Layouts(l_ImageCount, m_Pipeline.GetSkyboxDescriptorSetLayout());

        VkDescriptorSetAllocateInfo l_AllocateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        l_AllocateInfo.descriptorPool = m_DescriptorPool;
        l_AllocateInfo.descriptorSetCount = static_cast<uint32_t>(l_ImageCount);
        l_AllocateInfo.pSetLayouts = l_Layouts.data();

        m_SkyboxDescriptorSets.resize(l_ImageCount);
        if (vkAllocateDescriptorSets(Startup::GetDevice(), &l_AllocateInfo, m_SkyboxDescriptorSets.data()) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to allocate skybox descriptor sets");
            m_SkyboxDescriptorSets.clear();
            return;
        }

        for (size_t i = 0; i < l_ImageCount; ++i)
        {
            VkDescriptorBufferInfo l_GlobalBufferInfo{};
            l_GlobalBufferInfo.buffer = m_GlobalUniformBuffers[i];
            l_GlobalBufferInfo.offset = 0;
            l_GlobalBufferInfo.range = sizeof(GlobalUniformBuffer);

            VkDescriptorImageInfo l_CubemapInfo{};
            l_CubemapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            l_CubemapInfo.imageView = m_SkyboxTextureView;
            l_CubemapInfo.sampler = m_SkyboxTextureSampler;

            VkWriteDescriptorSet l_GlobalWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            l_GlobalWrite.dstSet = m_SkyboxDescriptorSets[i];
            l_GlobalWrite.dstBinding = 0;
            l_GlobalWrite.dstArrayElement = 0;
            l_GlobalWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            l_GlobalWrite.descriptorCount = 1;
            l_GlobalWrite.pBufferInfo = &l_GlobalBufferInfo;

            VkWriteDescriptorSet l_CubemapWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            l_CubemapWrite.dstSet = m_SkyboxDescriptorSets[i];
            l_CubemapWrite.dstBinding = 1;
            l_CubemapWrite.dstArrayElement = 0;
            l_CubemapWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            l_CubemapWrite.descriptorCount = 1;
            l_CubemapWrite.pImageInfo = &l_CubemapInfo;

            VkWriteDescriptorSet l_Writes[] = { l_GlobalWrite, l_CubemapWrite };
            vkUpdateDescriptorSets(Startup::GetDevice(), 2, l_Writes, 0, nullptr);
        }
    }

    void Renderer::DestroySkyboxDescriptorSets()
    {
        if (!m_SkyboxDescriptorSets.empty() && m_DescriptorPool != VK_NULL_HANDLE)
        {
            vkFreeDescriptorSets(Startup::GetDevice(), m_DescriptorPool, static_cast<uint32_t>(m_SkyboxDescriptorSets.size()), m_SkyboxDescriptorSets.data());
        }

        m_SkyboxDescriptorSets.clear();
    }

    void Renderer::DestroyOffscreenResources(uint32_t viewportId)
    {
        if (viewportId == 0)
        {
            return;
        }

        ViewportContext* l_Context = FindViewportContext(viewportId);
        if (l_Context == nullptr)
        {
            return;
        }

        VkDevice l_Device = Startup::GetDevice();
        OffscreenTarget& l_Target = l_Context->m_Target;

        vkDeviceWaitIdle(l_Device);

        // TODO: LOOK INTO RAII TO HANDLE ALL THIS RESOURCE
        //if (l_Target.m_TextureID != VK_NULL_HANDLE)
        //{
        //    ImGui_ImplVulkan_RemoveTexture(l_Target.m_TextureID);
        //    l_Target.m_TextureID = VK_NULL_HANDLE;
        //}

        // The renderer owns these handles; releasing them here avoids dangling ImGui descriptors or image memory leaks. 
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
        l_Context->m_CachedExtent = { 0, 0 };
        l_Context->m_Info.Size = { 0.0f, 0.0f };
    }

    void Renderer::DestroyAllOffscreenResources()
    {
        // Iterate carefully so erasing entries mid-loop remains valid across MSVC/STL implementations.
        auto it_Context = m_ViewportContexts.begin();
        while (it_Context != m_ViewportContexts.end())
        {
            const uint32_t l_ViewportId = it_Context->first;
            ++it_Context;
            DestroyOffscreenResources(l_ViewportId);
        }

        // Future: consider retaining unused targets in a pool so secondary editor panels can resume instantly.
        m_ActiveViewportId = 0;
    }

    Renderer::ViewportContext& Renderer::GetOrCreateViewportContext(uint32_t viewportId)
    {
        auto [it_Context, l_Inserted] = m_ViewportContexts.try_emplace(viewportId);
        ViewportContext& l_Context = it_Context->second;

        if (l_Inserted)
        {
            l_Context.m_Info.ViewportID = viewportId;
            l_Context.m_Target.m_Extent = { 0, 0 };
            l_Context.m_Target.m_CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            l_Context.m_Target.m_DepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        }

        return l_Context;
    }

    const Renderer::ViewportContext* Renderer::FindViewportContext(uint32_t viewportId) const
    {
        const auto it_Context = m_ViewportContexts.find(viewportId);
        if (it_Context == m_ViewportContexts.end())
        {
            return nullptr;
        }

        return &it_Context->second;
    }

    Renderer::ViewportContext* Renderer::FindViewportContext(uint32_t viewportId)
    {
        return const_cast<ViewportContext*>(static_cast<const Renderer*>(this)->FindViewportContext(viewportId));
    }

    const Camera* Renderer::GetActiveCamera(const ViewportContext& context) const
    {
        const uint32_t l_ViewportId = context.m_Info.ViewportID;

        if (l_ViewportId == 1U)
        {
            // The Scene viewport must always reflect the editor's navigation camera.
            return m_EditorCamera;
        }

        if (l_ViewportId == 2U)
        {
            // The Game viewport only exposes the runtime camera once the simulation has produced valid matrices.
            if (m_RuntimeCameraReady && m_RuntimeCamera)
            {
                return m_RuntimeCamera;
            }

            // Fall back to the editor camera so the viewport continues to display a useful image.
            return m_EditorCamera;
        }

        // Additional viewports prefer the editor output; if none exist allow the caller to fall back to identity matrices.
        if (m_EditorCamera)
        {
            return m_EditorCamera;
        }

        return (m_RuntimeCameraReady && m_RuntimeCamera) ? m_RuntimeCamera : nullptr;
    }

    void Renderer::CreateOrResizeOffscreenResources(OffscreenTarget& target, VkExtent2D extent)
    {
        VkDevice l_Device = Startup::GetDevice();

        // Ensure the GPU is idle before we reuse or release any image memory.
        vkDeviceWaitIdle(l_Device);

        auto a_ResetTarget = [l_Device](OffscreenTarget& target)
            {
                if (target.m_TextureID != VK_NULL_HANDLE)
                {
                    ImGui_ImplVulkan_RemoveTexture(target.m_TextureID);
                    target.m_TextureID = VK_NULL_HANDLE;
                }

                if (target.m_Framebuffer != VK_NULL_HANDLE)
                {
                    vkDestroyFramebuffer(l_Device, target.m_Framebuffer, nullptr);
                    target.m_Framebuffer = VK_NULL_HANDLE;
                }

                if (target.m_DepthView != VK_NULL_HANDLE)
                {
                    vkDestroyImageView(l_Device, target.m_DepthView, nullptr);
                    target.m_DepthView = VK_NULL_HANDLE;
                }

                if (target.m_ImageView != VK_NULL_HANDLE)
                {
                    vkDestroyImageView(l_Device, target.m_ImageView, nullptr);
                    target.m_ImageView = VK_NULL_HANDLE;
                }

                if (target.m_DepthImage != VK_NULL_HANDLE)
                {
                    vkDestroyImage(l_Device, target.m_DepthImage, nullptr);
                    target.m_DepthImage = VK_NULL_HANDLE;
                }

                if (target.m_Image != VK_NULL_HANDLE)
                {
                    vkDestroyImage(l_Device, target.m_Image, nullptr);
                    target.m_Image = VK_NULL_HANDLE;
                }

                if (target.m_DepthMemory != VK_NULL_HANDLE)
                {
                    vkFreeMemory(l_Device, target.m_DepthMemory, nullptr);
                    target.m_DepthMemory = VK_NULL_HANDLE;
                }

                if (target.m_Memory != VK_NULL_HANDLE)
                {
                    vkFreeMemory(l_Device, target.m_Memory, nullptr);
                    target.m_Memory = VK_NULL_HANDLE;
                }

                if (target.m_Sampler != VK_NULL_HANDLE)
                {
                    vkDestroySampler(l_Device, target.m_Sampler, nullptr);
                    target.m_Sampler = VK_NULL_HANDLE;
                }

                target.m_Extent = { 0, 0 };
                target.m_CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                target.m_DepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
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

        vkQueueSubmit(Startup::GetGraphicsQueue(), 1, &l_BootstrapSubmitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(Startup::GetGraphicsQueue());

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
        VkResult l_Result = vkAcquireNextImageKHR(Startup::GetDevice(), m_Swapchain.GetSwapchain(), UINT64_MAX,
            m_Commands.GetImageAvailableSemaphorePerImage(m_Commands.CurrentFrame()), VK_NULL_HANDLE, &imageIndex);

        if (l_Result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            // The swapchain is no longer compatible with the window surface; rebuild and skip this frame.
            TR_CORE_WARN("Swapchain out of date detected during AcquireNextImage, recreating and skipping frame");
            RecreateSwapchain();

            return false;
        }

        if (l_Result != VK_SUCCESS && l_Result != VK_SUBOPTIMAL_KHR)
        {
            TR_CORE_CRITICAL("Failed to acquire swap chain image!");

            return false;
        }

        VkFence& l_ImageFence = m_Commands.GetImageInFlight(imageIndex);
        if (l_ImageFence != VK_NULL_HANDLE)
        {
            vkWaitForFences(Startup::GetDevice(), 1, &l_ImageFence, VK_TRUE, UINT64_MAX);
        }

        m_Commands.SetImageInFlight(imageIndex, inFlightFence);

        return true;
    }

    bool Renderer::RecordCommandBuffer(uint32_t imageIndex)
    {
        // Collect sprite draw requests up front so the render pass can submit them without additional ECS lookups.
        GatherSpriteDraws();

        VkCommandBuffer l_CommandBuffer = m_Commands.GetCommandBuffer(imageIndex);

        VkCommandBufferBeginInfo l_BeginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        vkBeginCommandBuffer(l_CommandBuffer, &l_BeginInfo);

        auto a_BuildClearValue = [this]() -> VkClearValue
            {
                VkClearValue l_Value{};
                l_Value.color.float32[0] = m_ClearColor.r;
                l_Value.color.float32[1] = m_ClearColor.g;
                l_Value.color.float32[2] = m_ClearColor.b;
                l_Value.color.float32[3] = m_ClearColor.a;

                return l_Value;
            };

        ViewportContext* l_PrimaryContext = FindViewportContext(m_ActiveViewportId);
        OffscreenTarget* l_PrimaryTarget = nullptr;
        if (l_PrimaryContext && IsValidViewport(l_PrimaryContext->m_Info) && l_PrimaryContext->m_Target.m_Framebuffer != VK_NULL_HANDLE)
        {
            l_PrimaryTarget = &l_PrimaryContext->m_Target;
        }

        bool l_RenderedViewport = false;

        // Prepare the shared draw lists once so each viewport iteration can reuse the same data set.
        GatherMeshDraws();

        auto a_RenderViewport = [&](uint32_t viewportID, ViewportContext& context, bool isPrimary)
            {
                OffscreenTarget& l_Target = context.m_Target;
                if (!IsValidViewport(context.m_Info) || l_Target.m_Framebuffer == VK_NULL_HANDLE)
                {
                    return;
                }

                if (l_Target.m_Extent.width == 0 || l_Target.m_Extent.height == 0)
                {
                    return;
                }

                l_RenderedViewport = true;

                // Temporarily mark the context as active so shared helpers resolve relative camera state correctly.
                const uint32_t l_PreviousViewportId = m_ActiveViewportId;
                m_ActiveViewportId = context.m_Info.ViewportID;

                const Camera* l_ContextCamera = GetActiveCamera(context);

                if (context.m_Info.ViewportID == 2U && !m_RuntimeCameraReady)
                {
                    // When the runtime camera has not been initialised we expect to reuse the editor matrices instead.
                    assert(l_ContextCamera == nullptr || l_ContextCamera == m_EditorCamera);
                    l_ContextCamera = nullptr;
                }

                UpdateUniformBuffer(imageIndex, l_ContextCamera, l_CommandBuffer);

                // Restore the previously active viewport so editor interactions remain consistent outside this pass.
                m_ActiveViewportId = l_PreviousViewportId;

                VkPipelineStageFlags l_PreviousStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                VkAccessFlags l_PreviousAccess = 0;
                if (l_Target.m_CurrentLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                {
                    l_PreviousStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                    l_PreviousAccess = VK_ACCESS_SHADER_READ_BIT;
                }
                else if (l_Target.m_CurrentLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
                {
                    l_PreviousStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                    l_PreviousAccess = VK_ACCESS_TRANSFER_READ_BIT;
                }

                VkPipelineStageFlags l_DepthPreviousStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                VkAccessFlags l_DepthPreviousAccess = 0;
                if (l_Target.m_DepthLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
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
                l_PrepareDepth.oldLayout = l_Target.m_DepthLayout;
                l_PrepareDepth.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                l_PrepareDepth.srcAccessMask = l_DepthPreviousAccess;
                l_PrepareDepth.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                l_PrepareDepth.image = l_Target.m_DepthImage;

                vkCmdPipelineBarrier(l_CommandBuffer, l_DepthPreviousStage, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0, nullptr, 0, nullptr, 1, &l_PrepareDepth);
                l_Target.m_DepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

                VkImageMemoryBarrier l_PrepareOffscreen{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                l_PrepareOffscreen.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                l_PrepareOffscreen.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                l_PrepareOffscreen.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                l_PrepareOffscreen.subresourceRange.baseMipLevel = 0;
                l_PrepareOffscreen.subresourceRange.levelCount = 1;
                l_PrepareOffscreen.subresourceRange.baseArrayLayer = 0;
                l_PrepareOffscreen.subresourceRange.layerCount = 1;
                l_PrepareOffscreen.oldLayout = l_Target.m_CurrentLayout;
                l_PrepareOffscreen.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                l_PrepareOffscreen.srcAccessMask = l_PreviousAccess;
                l_PrepareOffscreen.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                l_PrepareOffscreen.image = l_Target.m_Image;

                vkCmdPipelineBarrier(l_CommandBuffer, l_PreviousStage, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &l_PrepareOffscreen);
                l_Target.m_CurrentLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

                VkRenderPassBeginInfo l_OffscreenPass{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
                l_OffscreenPass.renderPass = m_Pipeline.GetRenderPass();
                l_OffscreenPass.framebuffer = l_Target.m_Framebuffer;
                l_OffscreenPass.renderArea.offset = { 0, 0 };
                l_OffscreenPass.renderArea.extent = l_Target.m_Extent;

                std::array<VkClearValue, 2> l_OffscreenClearValues{};
                l_OffscreenClearValues[0] = a_BuildClearValue();
                l_OffscreenClearValues[1].depthStencil.depth = 1.0f;
                l_OffscreenClearValues[1].depthStencil.stencil = 0;
                l_OffscreenPass.clearValueCount = static_cast<uint32_t>(l_OffscreenClearValues.size());
                l_OffscreenPass.pClearValues = l_OffscreenClearValues.data();

                vkCmdBeginRenderPass(l_CommandBuffer, &l_OffscreenPass, VK_SUBPASS_CONTENTS_INLINE);

                VkClearAttachment l_ColorAttachmentClear{};
                l_ColorAttachmentClear.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                l_ColorAttachmentClear.colorAttachment = 0;
                l_ColorAttachmentClear.clearValue.color.float32[0] = m_ClearColor.r;
                l_ColorAttachmentClear.clearValue.color.float32[1] = m_ClearColor.g;
                l_ColorAttachmentClear.clearValue.color.float32[2] = m_ClearColor.b;
                l_ColorAttachmentClear.clearValue.color.float32[3] = m_ClearColor.a;

                VkClearRect l_ColorClearRect{};
                l_ColorClearRect.rect.offset = { 0, 0 };
                l_ColorClearRect.rect.extent = l_Target.m_Extent;
                l_ColorClearRect.baseArrayLayer = 0;
                l_ColorClearRect.layerCount = 1;

                vkCmdClearAttachments(l_CommandBuffer, 1, &l_ColorAttachmentClear, 1, &l_ColorClearRect);
                // Manual QA: Scene and Game panels now show independent images driven by their respective camera selections.
                // TODO: Explore asynchronous command recording so runtime and editor paths can execute in parallel where feasible.

                VkViewport l_OffscreenViewport{};
                l_OffscreenViewport.x = 0.0f;
                l_OffscreenViewport.y = 0.0f;
                l_OffscreenViewport.width = static_cast<float>(l_Target.m_Extent.width);
                l_OffscreenViewport.height = static_cast<float>(l_Target.m_Extent.height);
                l_OffscreenViewport.minDepth = 0.0f;
                l_OffscreenViewport.maxDepth = 1.0f;
                vkCmdSetViewport(l_CommandBuffer, 0, 1, &l_OffscreenViewport);

                VkRect2D l_OffscreenScissor{};
                l_OffscreenScissor.offset = { 0, 0 };
                l_OffscreenScissor.extent = l_Target.m_Extent;
                vkCmdSetScissor(l_CommandBuffer, 0, 1, &l_OffscreenScissor);

                const bool l_HasSkyboxDescriptors = imageIndex < m_SkyboxDescriptorSets.size() && m_SkyboxDescriptorSets[imageIndex] != VK_NULL_HANDLE;
                if (l_HasSkyboxDescriptors && m_Pipeline.GetSkyboxPipeline() != VK_NULL_HANDLE)
                {
                    vkCmdBindPipeline(l_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline.GetSkyboxPipeline());
                    m_Skybox.Record(l_CommandBuffer, m_Pipeline.GetSkyboxPipelineLayout(), m_SkyboxDescriptorSets.data(), imageIndex);
                }

                vkCmdBindPipeline(l_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline.GetPipeline());

                const bool l_HasDescriptorSet = imageIndex < m_DescriptorSets.size();
                if (l_HasDescriptorSet)
                {
                    vkCmdBindDescriptorSets(l_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline.GetPipelineLayout(), 0, 1, &m_DescriptorSets[imageIndex], 0, nullptr);
                }

                if (m_VertexBuffer != VK_NULL_HANDLE && m_IndexBuffer != VK_NULL_HANDLE && !m_MeshDrawInfo.empty() && !m_MeshDrawCommands.empty() && l_HasDescriptorSet)
                {
                    VkBuffer l_VertexBuffers[] = { m_VertexBuffer };
                    VkDeviceSize l_Offsets[] = { 0 };
                    vkCmdBindVertexBuffers(l_CommandBuffer, 0, 1, l_VertexBuffers, l_Offsets);
                    vkCmdBindIndexBuffer(l_CommandBuffer, m_IndexBuffer, 0, VK_INDEX_TYPE_UINT32);

                    for (const MeshDrawCommand& l_Command : m_MeshDrawCommands)
                    {
                        if (!l_Command.m_Component)
                        {
                            continue;
                        }

                        const MeshComponent& l_Component = *l_Command.m_Component;
                        if (l_Component.m_MeshIndex >= m_MeshDrawInfo.size())
                        {
                            continue;
                        }

                        const MeshDrawInfo& l_DrawInfo = m_MeshDrawInfo[l_Component.m_MeshIndex];
                        if (l_DrawInfo.m_IndexCount == 0)
                        {
                            continue;
                        }

                        RenderablePushConstant l_PushConstant{};
                        l_PushConstant.m_ModelMatrix = l_Command.m_ModelMatrix;
                        int32_t l_MaterialIndex = l_DrawInfo.m_MaterialIndex;
                        int32_t l_TextureSlot = 0;
                        if (l_MaterialIndex >= 0 && static_cast<size_t>(l_MaterialIndex) < m_Materials.size())
                        {
                            l_TextureSlot = m_Materials[l_MaterialIndex].BaseColorTextureSlot;
                        }

                        l_PushConstant.m_TextureSlot = l_TextureSlot;
                        l_PushConstant.m_MaterialIndex = l_MaterialIndex;
                        vkCmdPushConstants(l_CommandBuffer, m_Pipeline.GetPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                            sizeof(RenderablePushConstant), &l_PushConstant);

                        vkCmdDrawIndexed(l_CommandBuffer, l_DrawInfo.m_IndexCount, 1, l_DrawInfo.m_FirstIndex, l_DrawInfo.m_BaseVertex, 0);
                    }
                }

                if (l_HasDescriptorSet)
                {
                    DrawSprites(l_CommandBuffer, imageIndex);
                }

                vkCmdEndRenderPass(l_CommandBuffer);

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
                l_OffscreenBarrier.image = l_Target.m_Image;

                vkCmdPipelineBarrier(l_CommandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &l_OffscreenBarrier);
                l_Target.m_CurrentLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

                if (!isPrimary)
                {
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
                    l_ToSample.image = l_Target.m_Image;

                    vkCmdPipelineBarrier(l_CommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        0, 0, nullptr, 0, nullptr, 1, &l_ToSample);
                    l_Target.m_CurrentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                }
            };

        for (auto& it_Context : m_ViewportContexts)
        {
            if (it_Context.first == m_ActiveViewportId)
            {
                continue;
            }

            a_RenderViewport(it_Context.first, it_Context.second, false);
        }

        if (l_PrimaryContext && l_PrimaryTarget)
        {
            a_RenderViewport(m_ActiveViewportId, *l_PrimaryContext, true);
        }

        if (!l_RenderedViewport)
        {
            UpdateUniformBuffer(imageIndex, nullptr, l_CommandBuffer);
        }

        if (l_PrimaryTarget && (l_PrimaryTarget->m_Framebuffer == VK_NULL_HANDLE || l_PrimaryTarget->m_Extent.width == 0 || l_PrimaryTarget->m_Extent.height == 0))
        {
            l_PrimaryTarget = nullptr;
        }

        const bool l_PrimaryViewportActive = l_PrimaryTarget != nullptr;

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

        if (l_PrimaryViewportActive)
        {
            // Multi-panel path: copy the rendered viewport into the swapchain image so every editor panel sees a synchronized back buffer.
            VkImageBlit l_BlitRegion{};
            l_BlitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            l_BlitRegion.srcSubresource.mipLevel = 0;
            l_BlitRegion.srcSubresource.baseArrayLayer = 0;
            l_BlitRegion.srcSubresource.layerCount = 1;
            l_BlitRegion.srcOffsets[0] = { 0, 0, 0 };
            l_BlitRegion.srcOffsets[1] = { static_cast<int32_t>(l_PrimaryTarget->m_Extent.width), static_cast<int32_t>(l_PrimaryTarget->m_Extent.height), 1 };
            l_BlitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            l_BlitRegion.dstSubresource.mipLevel = 0;
            l_BlitRegion.dstSubresource.baseArrayLayer = 0;
            l_BlitRegion.dstSubresource.layerCount = 1;
            l_BlitRegion.dstOffsets[0] = { 0, 0, 0 };
            l_BlitRegion.dstOffsets[1] = { static_cast<int32_t>(m_Swapchain.GetExtent().width), static_cast<int32_t>(m_Swapchain.GetExtent().height), 1 };

            vkCmdBlitImage(l_CommandBuffer,
                l_PrimaryTarget->m_Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
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
            l_ToSample.image = l_PrimaryTarget->m_Image;

            vkCmdPipelineBarrier(l_CommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &l_ToSample);
            l_PrimaryTarget->m_CurrentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

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

        if (!l_PrimaryViewportActive)
        {
            // Legacy rendering path: draw directly to the back buffer when the editor viewport is hidden.
            const bool l_HasSkyboxDescriptors = imageIndex < m_SkyboxDescriptorSets.size() && m_SkyboxDescriptorSets[imageIndex] != VK_NULL_HANDLE;
            if (l_HasSkyboxDescriptors && m_Pipeline.GetSkyboxPipeline() != VK_NULL_HANDLE)
            {
                vkCmdBindPipeline(l_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline.GetSkyboxPipeline());
                m_Skybox.Record(l_CommandBuffer, m_Pipeline.GetSkyboxPipelineLayout(), m_SkyboxDescriptorSets.data(), imageIndex);
            }

            vkCmdBindPipeline(l_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline.GetPipeline());

            const bool l_HasDescriptorSet = imageIndex < m_DescriptorSets.size();
            if (l_HasDescriptorSet)
            {
                vkCmdBindDescriptorSets(l_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline.GetPipelineLayout(), 0, 1, &m_DescriptorSets[imageIndex], 0, nullptr);
            }

            GatherMeshDraws();

            if (m_VertexBuffer != VK_NULL_HANDLE && m_IndexBuffer != VK_NULL_HANDLE && !m_MeshDrawInfo.empty() && !m_MeshDrawCommands.empty() && l_HasDescriptorSet)
            {
                VkBuffer l_VertexBuffers[] = { m_VertexBuffer };
                VkDeviceSize l_Offsets[] = { 0 };
                vkCmdBindVertexBuffers(l_CommandBuffer, 0, 1, l_VertexBuffers, l_Offsets);
                vkCmdBindIndexBuffer(l_CommandBuffer, m_IndexBuffer, 0, VK_INDEX_TYPE_UINT32);

                for (const MeshDrawCommand& l_Command : m_MeshDrawCommands)
                {
                    if (!l_Command.m_Component)
                    {
                        continue;
                    }

                    const MeshComponent& l_Component = *l_Command.m_Component;
                    if (l_Component.m_MeshIndex >= m_MeshDrawInfo.size())
                    {
                        continue;
                    }

                    const MeshDrawInfo& l_DrawInfo = m_MeshDrawInfo[l_Component.m_MeshIndex];
                    if (l_DrawInfo.m_IndexCount == 0)
                    {
                        continue;
                    }

                    RenderablePushConstant l_PushConstant{};
                    l_PushConstant.m_ModelMatrix = l_Command.m_ModelMatrix;
                    int32_t l_MaterialIndex = l_DrawInfo.m_MaterialIndex;
                    int32_t l_TextureSlot = 0;
                    if (l_MaterialIndex >= 0 && static_cast<size_t>(l_MaterialIndex) < m_Materials.size())
                    {
                        l_TextureSlot = m_Materials[l_MaterialIndex].BaseColorTextureSlot;
                    }

                    l_PushConstant.m_TextureSlot = l_TextureSlot;
                    l_PushConstant.m_MaterialIndex = l_MaterialIndex;
                    vkCmdPushConstants(l_CommandBuffer, m_Pipeline.GetPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                        sizeof(RenderablePushConstant), &l_PushConstant);

                    vkCmdDrawIndexed(l_CommandBuffer, l_DrawInfo.m_IndexCount, 1, l_DrawInfo.m_FirstIndex, l_DrawInfo.m_BaseVertex, 0);
                }
            }

            if (l_HasDescriptorSet)
            {
                DrawSprites(l_CommandBuffer, imageIndex);
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

            // Abort the frame so the caller can handle the failure gracefully instead of terminating the process.

            return false;
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

        if (vkQueueSubmit(Startup::GetGraphicsQueue(), 1, &l_SubmitInfo, inFlightFence) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to submit draw command buffer!");

            // Notify the caller that the queue submission failed so the frame can be skipped cleanly.

            return false;
        }

        if (vkGetFenceStatus(Startup::GetDevice(), m_ResourceFence) == VK_NOT_READY)
        {
            vkWaitForFences(Startup::GetDevice(), 1, &m_ResourceFence, VK_TRUE, UINT64_MAX);
        }

        vkResetFences(Startup::GetDevice(), 1, &m_ResourceFence);
        VkSubmitInfo l_FenceSubmit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        l_FenceSubmit.commandBufferCount = 0;
        if (vkQueueSubmit(Startup::GetGraphicsQueue(), 1, &l_FenceSubmit, m_ResourceFence) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to submit resource fence");
            // Allow the caller to decide how to recover from a failed fence submission.

            return false;
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

        VkResult l_PresentResult = vkQueuePresentKHR(Startup::GetPresentQueue(), &l_PresentInfo);

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
                vkDeviceWaitIdle(Startup::GetDevice());
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
                    UploadMesh(a_ModelData.Meshes, a_ModelData.Materials, a_ModelData.Textures);
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
                auto texture = Loader::TextureLoader::Load(a_Event->Path);
                if (!texture.Pixels.empty())
                {
                    // TODO: Investigate streaming or impostor generation so large material libraries can refresh incrementally.
                    UploadTexture(a_Event->Path, texture);
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

    void Renderer::UpdateUniformBuffer(uint32_t currentImage, const Camera* cameraOverride, VkCommandBuffer commandBuffer)
    {
        if (currentImage >= m_GlobalUniformBuffersMemory.size() || currentImage >= m_MaterialBuffersMemory.size())
        {
            return;
        }

        GlobalUniformBuffer l_Global{};
        const Camera* l_ActiveCamera = cameraOverride ? cameraOverride : GetActiveCamera();
        if (l_ActiveCamera)
        {
            l_Global.View = l_ActiveCamera->GetViewMatrix();
            l_Global.Projection = l_ActiveCamera->GetProjectionMatrix();
            l_Global.CameraPosition = glm::vec4(l_ActiveCamera->GetPosition(), 1.0f);
        }
        else
        {
            l_Global.View = glm::mat4{ 1.0f };
            l_Global.Projection = glm::mat4{ 1.0f };
            l_Global.CameraPosition = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            // TODO: Provide a jittered fallback matrix when temporal AA is introduced.
        }

        l_Global.AmbientColorIntensity = glm::vec4(m_AmbientColor, m_AmbientIntensity);

        glm::vec3 l_DirectionalDirection = glm::normalize(s_DefaultDirectionalDirection);
        glm::vec3 l_DirectionalColor = s_DefaultDirectionalColor;
        float l_DirectionalIntensity = s_DefaultDirectionalIntensity;
        uint32_t l_DirectionalCount = 0;
        uint32_t l_PointLightWriteCount = 0;

        if (m_Registry)
        {
            const std::vector<ECS::Entity>& l_Entities = m_Registry->GetEntities();
            for (ECS::Entity it_Entity : l_Entities)
            {
                if (!m_Registry->HasComponent<LightComponent>(it_Entity))
                {
                    continue;
                }

                const LightComponent& l_LightComponent = m_Registry->GetComponent<LightComponent>(it_Entity);
                if (!l_LightComponent.m_Enabled)
                {
                    continue;
                }

                if (l_LightComponent.m_Type == LightComponent::Type::Directional)
                {
                    if (l_DirectionalCount == 0)
                    {
                        const float l_LengthSquared = glm::dot(l_LightComponent.m_Direction, l_LightComponent.m_Direction);
                        if (l_LengthSquared > 0.0001f)
                        {
                            l_DirectionalDirection = glm::normalize(l_LightComponent.m_Direction);
                        }
                        l_DirectionalColor = l_LightComponent.m_Color;
                        l_DirectionalIntensity = std::max(l_LightComponent.m_Intensity, 0.0f);
                    }
                    ++l_DirectionalCount;
                    continue;
                }

                if (l_LightComponent.m_Type == LightComponent::Type::Point)
                {
                    if (l_PointLightWriteCount >= s_MaxPointLights)
                    {
                        continue;
                    }

                    glm::vec3 l_Position{ 0.0f };
                    if (m_Registry->HasComponent<Transform>(it_Entity))
                    {
                        l_Position = m_Registry->GetComponent<Transform>(it_Entity).Position;
                    }

                    const float l_Range = std::max(l_LightComponent.m_Range, 0.0f);
                    const float l_Intensity = std::max(l_LightComponent.m_Intensity, 0.0f);

                    l_Global.PointLights[l_PointLightWriteCount].PositionRange = glm::vec4(l_Position, l_Range);
                    l_Global.PointLights[l_PointLightWriteCount].ColorIntensity = glm::vec4(l_LightComponent.m_Color, l_Intensity);
                    ++l_PointLightWriteCount;
                    continue;
                }
            }
        }

        const bool l_ShouldUseFallbackDirectional = (l_DirectionalCount == 0 && l_PointLightWriteCount == 0);
        const uint32_t l_DirectionalUsed = (l_DirectionalCount > 0 || l_ShouldUseFallbackDirectional) ? 1u : 0u;

        l_Global.DirectionalLightDirection = glm::vec4(l_DirectionalDirection, 0.0f);
        l_Global.DirectionalLightColor = glm::vec4(l_DirectionalColor, l_DirectionalIntensity);
        l_Global.LightCounts = glm::uvec4(l_DirectionalUsed, l_PointLightWriteCount, 0u, 0u);

        auto BuildMaterialPayload = [this]()
            {
                std::vector<MaterialUniformBuffer> l_Payload{};
                const size_t l_TargetCount = std::max(m_MaterialBufferElementCount, static_cast<size_t>(1));
                l_Payload.reserve(l_TargetCount);

                for (const Geometry::Material& it_Material : m_Materials)
                {
                    MaterialUniformBuffer l_Record{};
                    l_Record.BaseColorFactor = it_Material.BaseColorFactor;
                    l_Record.MaterialFactors = glm::vec4(it_Material.MetallicFactor, it_Material.RoughnessFactor, 1.0f, 0.0f);
                    l_Payload.push_back(l_Record);
                }

                MaterialUniformBuffer l_Default{};
                l_Default.BaseColorFactor = glm::vec4(1.0f);
                l_Default.MaterialFactors = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);

                while (l_Payload.size() < l_TargetCount)
                {
                    l_Payload.push_back(l_Default);
                }

                return l_Payload;
            };

        const bool l_HasMaterialBuffers = currentImage < m_MaterialBuffers.size() && currentImage < m_MaterialBuffersMemory.size();

        if (commandBuffer != VK_NULL_HANDLE)
        {
            // Record GPU-side buffer updates so each viewport captures the correct camera state before its render pass begins.
            vkCmdUpdateBuffer(commandBuffer, m_GlobalUniformBuffers[currentImage], 0, sizeof(l_Global), &l_Global);
            std::vector<VkBufferMemoryBarrier> l_Barriers{};
            l_Barriers.reserve(2);

            VkBufferMemoryBarrier l_GlobalBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
            l_GlobalBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            l_GlobalBarrier.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
            l_GlobalBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            l_GlobalBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            l_GlobalBarrier.buffer = m_GlobalUniformBuffers[currentImage];
            l_GlobalBarrier.offset = 0;
            l_GlobalBarrier.size = sizeof(l_Global);
            l_Barriers.push_back(l_GlobalBarrier);

            if (l_HasMaterialBuffers && currentImage < m_MaterialBufferDirty.size() && m_MaterialBufferDirty[currentImage])
            {
                std::vector<MaterialUniformBuffer> l_MaterialPayload = BuildMaterialPayload();
                const VkDeviceSize l_CopySize = static_cast<VkDeviceSize>(l_MaterialPayload.size() * sizeof(MaterialUniformBuffer));
                if (l_CopySize > 0)
                {
                    const uint8_t* l_RawData = reinterpret_cast<const uint8_t*>(l_MaterialPayload.data());
                    VkDeviceSize l_Remaining = l_CopySize;
                    VkDeviceSize l_Offset = 0;
                    const VkDeviceSize l_MaxChunk = 65536;

                    while (l_Remaining > 0)
                    {
                        const VkDeviceSize l_ChunkSize = std::min(l_Remaining, l_MaxChunk);
                        vkCmdUpdateBuffer(commandBuffer, m_MaterialBuffers[currentImage], l_Offset, l_ChunkSize, l_RawData + l_Offset);
                        l_Remaining -= l_ChunkSize;
                        l_Offset += l_ChunkSize;
                    }

                    VkBufferMemoryBarrier l_MaterialBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
                    l_MaterialBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    l_MaterialBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    l_MaterialBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    l_MaterialBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    l_MaterialBarrier.buffer = m_MaterialBuffers[currentImage];
                    l_MaterialBarrier.offset = 0;
                    l_MaterialBarrier.size = l_CopySize;
                    l_Barriers.push_back(l_MaterialBarrier);

                    m_MaterialBufferDirty[currentImage] = false;
                }
            }

            // Guarantee the transfer writes are visible before the shader stages fetch the uniform data.
            if (!l_Barriers.empty())
            {
                vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0, 0, nullptr, static_cast<uint32_t>(l_Barriers.size()), l_Barriers.data(), 0, nullptr);
            }
        }
        else
        {
            // Fall back to direct CPU writes when no command buffer is available (e.g., pre-frame initialisation).
            void* l_Data = nullptr;
            vkMapMemory(Startup::GetDevice(), m_GlobalUniformBuffersMemory[currentImage], 0, sizeof(l_Global), 0, &l_Data);
            std::memcpy(l_Data, &l_Global, sizeof(l_Global));
            vkUnmapMemory(Startup::GetDevice(), m_GlobalUniformBuffersMemory[currentImage]);

            if (l_HasMaterialBuffers)
            {
                const bool l_ShouldUploadAll = std::any_of(m_MaterialBufferDirty.begin(), m_MaterialBufferDirty.end(), [](bool it_Dirty) { return it_Dirty; });
                if (l_ShouldUploadAll)
                {
                    std::vector<MaterialUniformBuffer> l_MaterialPayload = BuildMaterialPayload();
                    const VkDeviceSize l_CopySize = static_cast<VkDeviceSize>(l_MaterialPayload.size() * sizeof(MaterialUniformBuffer));
                    for (size_t it_Image = 0; it_Image < m_MaterialBuffersMemory.size() && it_Image < m_MaterialBufferDirty.size(); ++it_Image)
                    {
                        if (!m_MaterialBufferDirty[it_Image])
                        {
                            continue;
                        }

                        if (m_MaterialBuffersMemory[it_Image] == VK_NULL_HANDLE)
                        {
                            continue;
                        }

                        void* l_MaterialData = nullptr;
                        vkMapMemory(Startup::GetDevice(), m_MaterialBuffersMemory[it_Image], 0, l_CopySize, 0, &l_MaterialData);
                        std::memcpy(l_MaterialData, l_MaterialPayload.data(), static_cast<size_t>(l_CopySize));
                        vkUnmapMemory(Startup::GetDevice(), m_MaterialBuffersMemory[it_Image]);
                        m_MaterialBufferDirty[it_Image] = false;
                    }
                }
            }
        }

        // TODO: Expand the uniform population to handle per-camera post-processing once those systems exist.
    }

    void Renderer::SetSelectedEntity(ECS::Entity entity)
    {
        // Store the inspector's active entity so gizmo updates reference the same transform as the UI selection.
        m_Entity = entity;
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