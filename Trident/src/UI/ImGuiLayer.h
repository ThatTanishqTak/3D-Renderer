#pragma once

#include "UI/ImGuiStyleManager.h"

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <imgui.h>

namespace Trident
{
    namespace UI
    {
        /**
         * @brief Bridges Dear ImGui with the engine's Vulkan renderer.
         *
         * The layer owns the ImGui context, configures styling, and records draw data into the
         * renderer's primary command buffer so UI overlays composite correctly each frame.
         */
        class ImGuiLayer
        {
        public:
            ImGuiLayer();
            ~ImGuiLayer();

            // Initialise ImGui using the engine's graphics handles.
            void Init(GLFWwindow* window,
                VkInstance instance,
                VkPhysicalDevice physicalDevice,
                VkDevice device,
                uint32_t graphicsQueueFamily,
                VkQueue graphicsQueue,
                VkRenderPass renderPass,
                uint32_t imageCount,
                VkCommandPool commandPool);

            // Begin a new ImGui frame. Safe to call even when the layer has not been initialised yet.
            void BeginFrame();

            // Finalise the current frame so draw data is ready for recording.
            void EndFrame();

            // Record ImGui draw data into the provided command buffer. The caller must have begun a render pass.
            void Render(VkCommandBuffer commandBuffer);

            // Render platform windows (multi-viewport) after the main swapchain submission has completed.
            void RenderAdditionalViewports();

            // Tear down ImGui and release Vulkan resources.
            void Shutdown();

            bool IsInitialised() const { return m_Initialised; }

        private:
            void CreateDescriptorPool();
            void DestroyDescriptorPool();
            void UploadFonts();

        private:
            GLFWwindow* m_Window = nullptr;
            VkInstance m_Instance = VK_NULL_HANDLE;
            VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
            VkDevice m_Device = VK_NULL_HANDLE;
            VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
            uint32_t m_GraphicsQueueFamily = 0;
            VkRenderPass m_RenderPass = VK_NULL_HANDLE;
            uint32_t m_ImageCount = 0;
            VkCommandPool m_CommandPool = VK_NULL_HANDLE;
            VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
            bool m_Initialised = false;

            ImGuiStyleManager m_StyleManager;
        };
    }
}