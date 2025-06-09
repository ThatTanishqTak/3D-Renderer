#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

namespace Trident
{
    class ImGuiLayer
    {
    public:
        void Init(GLFWwindow* window, VkInstance instance, VkDevice device, VkPhysicalDevice physicalDevice,
            uint32_t queueFamily, VkQueue queue, VkRenderPass renderPass, uint32_t imageCount, VkCommandPool commandPool);

        void Begin();
        void End(VkCommandBuffer cmd);

        void SetupDockspace();
        void Shutdown();

    private:
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        GLFWwindow* m_Window = nullptr;
        VkCommandPool m_CommandPool = VK_NULL_HANDLE;
    };
}