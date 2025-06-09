#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

namespace Trident
{
	class ImGuiLayer
	{
	public:
        void Init(GLFWwindow* window, VkInstance instance, VkDevice device, VkPhysicalDevice physicalDevice,
            uint32_t queueFamily, VkQueue queue, VkRenderPass renderPass, uint32_t imageCount, VkCommandPool commandPool);

        void Begin();
        void SetupDockspace();
        void End(VkCommandBuffer commandBuffer);

        void Shutdown();

    private:
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        GLFWwindow* m_Window = nullptr;
        VkCommandPool m_CommandPool = VK_NULL_HANDLE;
	};
}