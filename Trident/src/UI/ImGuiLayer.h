#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

namespace Trident
{
    namespace UI
    {
        class ImGuiLayer
        {
        public:
            ImGuiLayer() = default;
            void Init(GLFWwindow* window, VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, uint32_t queueFamily, VkQueue queue,
                VkRenderPass renderPass, uint32_t imageCount, VkCommandPool commandPool);
            void Shutdown();

            void BeginFrame();
            void Dockspace();
            void EndFrame();
            void Render(VkCommandBuffer commandBuffer);

        private:
            VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
            VkDevice m_Device = VK_NULL_HANDLE;
            VkQueue m_Queue = VK_NULL_HANDLE;
        };
    }
}