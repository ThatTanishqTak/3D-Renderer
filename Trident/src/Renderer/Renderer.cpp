#include "Renderer.h"

#include "Core/Utilities.h"

#include <stdexcept>

namespace Trident
{
    Renderer::Renderer()
    {
        // The renderer defers heavy initialization until Vulkan handles are supplied by Startup.
    }

    Renderer::~Renderer()
    {
        Shutdown();
    }

    void Renderer::Initialize(VkInstance a_Instance, VkPhysicalDevice a_PhysicalDevice, VkDevice a_Device,
        VkSurfaceKHR a_Surface, VkQueue a_GraphicsQueue, VkQueue a_PresentQueue,
        uint32_t a_FramebufferWidth, uint32_t a_FramebufferHeight)
    {
        if (m_IsInitialized)
        {
            TR_CORE_WARN("Renderer::Initialize called more than once; ignoring duplicate request.");
            return;
        }

        m_Instance = a_Instance;
        m_PhysicalDevice = a_PhysicalDevice;
        m_Device = a_Device;
        m_Surface = a_Surface;
        m_GraphicsQueue = a_GraphicsQueue;
        m_PresentQueue = a_PresentQueue;

        CreateSwapchain(a_FramebufferWidth, a_FramebufferHeight);

        m_IsInitialized = true;
    }

    void Renderer::Shutdown()
    {
        if (!m_IsInitialized)
        {
            return;
        }

        DestroySwapchain();
        ResetGeometryState();

        m_Instance = VK_NULL_HANDLE;
        m_PhysicalDevice = VK_NULL_HANDLE;
        m_Device = VK_NULL_HANDLE;
        m_Surface = VK_NULL_HANDLE;
        m_GraphicsQueue = VK_NULL_HANDLE;
        m_PresentQueue = VK_NULL_HANDLE;

        m_IsInitialized = false;
    }

    void Renderer::UploadMesh(const std::vector<Geometry::Mesh>& a_Meshes,
        const std::vector<Geometry::Material>& a_Materials,
        const std::vector<std::string>& a_TexturePaths)
    {
        if (!m_IsInitialized)
        {
            TR_CORE_WARN("UploadMesh called before renderer initialization; skipping upload.");
            return;
        }

        ResetGeometryState();

        m_Meshes = a_Meshes;
        m_Materials = a_Materials;
        m_TexturePaths = a_TexturePaths;

        m_MeshAllocations.resize(m_Meshes.size());

        uint32_t l_NextIndexOffset = 0;

        for (size_t it_Index = 0; it_Index < m_Meshes.size(); ++it_Index)
        {
            const Geometry::Mesh& l_Mesh = m_Meshes[it_Index];
            MeshAllocation& l_Allocation = m_MeshAllocations[it_Index];

            l_Allocation.m_FirstIndex = l_NextIndexOffset;
            l_Allocation.m_IndexCount = static_cast<uint32_t>(l_Mesh.Indices.size());
            l_Allocation.m_BaseVertex = 0; // Vertex data is not yet streamed, so base vertex remains zero.

            l_NextIndexOffset += l_Allocation.m_IndexCount;

            for (uint32_t it_Element : l_Mesh.Indices)
            {
                m_IndexBuffer.push_back(it_Element);
            }
        }

        // At this stage GPU buffers would typically be staged and uploaded. The engine currently
        // mirrors the index data on the CPU so draw command construction can reference stable
        // offsets while the Vulkan backend is fleshed out.
        TR_CORE_INFO("Renderer uploaded {} meshes, {} materials, and {} textures into staging caches.",
            m_Meshes.size(), m_Materials.size(), m_TexturePaths.size());
    }

    void Renderer::CreateSwapchain(uint32_t a_FramebufferWidth, uint32_t a_FramebufferHeight)
    {
        // The swapchain creation path intentionally stays minimal to keep the renderer lightweight
        // while the rest of the graphics backend is under construction. The function validates the
        // required handles and records the desired dimensions so the future Vulkan pipeline can use
        // them when creating images and framebuffers.
        if (m_Instance == VK_NULL_HANDLE || m_Device == VK_NULL_HANDLE || m_Surface == VK_NULL_HANDLE)
        {
            throw std::runtime_error("Renderer missing Vulkan handles during swapchain creation.");
        }

        (void)a_FramebufferWidth;
        (void)a_FramebufferHeight;

        // Real swapchain creation will land once the presentation pipeline is assembled. For now the
        // renderer simply tracks that a swapchain would be required so Startup can gate teardown logic.
        m_Swapchain = VK_NULL_HANDLE;
    }

    void Renderer::DestroySwapchain()
    {
        if (m_Swapchain != VK_NULL_HANDLE && m_Device != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);
            m_Swapchain = VK_NULL_HANDLE;
        }
    }

    void Renderer::ResetGeometryState()
    {
        m_Meshes.clear();
        m_Materials.clear();
        m_TexturePaths.clear();
        m_MeshAllocations.clear();
        m_IndexBuffer.clear();
    }
}