#pragma once

#include "Geometry/Mesh.h"
#include "Geometry/Material.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Trident
{
    /**
     * @brief Lightweight rendering facade that owns swapchain state and uploaded geometry.
     *
     * The renderer centralises Vulkan objects created after device initialisation so other
     * systems (scene management, UI, editors) can push mesh, material, and texture data
     * without handling GPU resource lifetimes directly. The implementation focuses on
     * predictable behaviour over performance so it can run inside tooling builds.
     */
    class Renderer
    {
    public:
        Renderer();
        ~Renderer();

        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;

        /**
         * @brief Initializes swapchain-facing state using handles produced during startup.
         *
         * Startup wires Vulkan device creation, so the renderer consumes the resulting
         * handles here and prepares a minimal swapchain description. Future passes can
         * expand this to allocate command buffers and descriptor layouts.
         */
        void Initialize(VkInstance a_Instance, VkPhysicalDevice a_PhysicalDevice, VkDevice a_Device,
            VkSurfaceKHR a_Surface, VkQueue a_GraphicsQueue, VkQueue a_PresentQueue,
            uint32_t a_FramebufferWidth, uint32_t a_FramebufferHeight);

        /**
         * @brief Releases swapchain objects and clears cached geometry.
         */
        void Shutdown();

        bool IsInitialized() const { return m_IsInitialized; }

        /**
         * @brief Uploads mesh, material, and texture data into CPU side caches that mirror GPU buffers.
         *
         * Scene systems rebuild geometry tables during asset import. This method consumes the new
         * data, repacks it into contiguous arrays, and recalculates draw offsets so future command
         * buffers can bind shared index buffers without reprocessing the CPU scene graph.
         */
        void UploadMesh(const std::vector<Geometry::Mesh>& a_Meshes,
            const std::vector<Geometry::Material>& a_Materials,
            const std::vector<std::string>& a_TexturePaths);

        /**
         * @brief Describes where each mesh lives inside the shared buffers.
         */
        struct MeshAllocation
        {
            uint32_t m_FirstIndex = 0;
            uint32_t m_IndexCount = 0;
            int32_t m_BaseVertex = 0;
        };

        const std::vector<MeshAllocation>& GetMeshAllocations() const { return m_MeshAllocations; }
        const std::vector<Geometry::Mesh>& GetMeshes() const { return m_Meshes; }
        const std::vector<Geometry::Material>& GetMaterials() const { return m_Materials; }
        const std::vector<std::string>& GetTexturePaths() const { return m_TexturePaths; }

    private:
        void CreateSwapchain(uint32_t a_FramebufferWidth, uint32_t a_FramebufferHeight);
        void DestroySwapchain();
        void ResetGeometryState();

    private:
        VkInstance m_Instance = VK_NULL_HANDLE;
        VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
        VkDevice m_Device = VK_NULL_HANDLE;
        VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
        VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
        VkQueue m_PresentQueue = VK_NULL_HANDLE;
        VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;

        std::vector<Geometry::Mesh> m_Meshes{};
        std::vector<Geometry::Material> m_Materials{};
        std::vector<std::string> m_TexturePaths{};
        std::vector<MeshAllocation> m_MeshAllocations{};
        std::vector<uint32_t> m_IndexBuffer{}; // CPU-side mirror of the packed index buffer.

        bool m_IsInitialized = false;
    };
}