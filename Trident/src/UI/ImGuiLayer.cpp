#include "UI/ImGuiLayer.h"

#include "Core/Utilities.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <algorithm>

namespace Trident
{
    namespace UI
    {
        void ImGuiLayer::Init(GLFWwindow* window, VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, uint32_t queueFamily, VkQueue queue,
            VkRenderPass renderPass, uint32_t imageCount, VkCommandPool commandPool)
        {
            TR_CORE_INFO("-------INITIALIZING IMGUI-------");

            m_Device = device;
            m_Queue = queue;

            const uint32_t l_DescriptorCount = std::max(imageCount, 1u);

            VkDescriptorPoolSize l_PoolSizes[] =
            {
                { VK_DESCRIPTOR_TYPE_SAMPLER, l_DescriptorCount },
                { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, l_DescriptorCount },
                { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, l_DescriptorCount },
                { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, l_DescriptorCount },
                { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, l_DescriptorCount },
                { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, l_DescriptorCount },
                { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, l_DescriptorCount },
                { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, l_DescriptorCount },
                { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, l_DescriptorCount },
                { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, l_DescriptorCount },
                { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, l_DescriptorCount }
            };

            VkDescriptorPoolCreateInfo l_PoolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
            l_PoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            l_PoolInfo.maxSets = l_DescriptorCount * static_cast<uint32_t>(std::size(l_PoolSizes));
            l_PoolInfo.poolSizeCount = static_cast<uint32_t>(std::size(l_PoolSizes));
            l_PoolInfo.pPoolSizes = l_PoolSizes;
            if (vkCreateDescriptorPool(device, &l_PoolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS)
            {
                TR_CORE_CRITICAL("Failed to create ImGui descriptor pool");
            }

            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGui::StyleColorsDark();

            ImGui_ImplGlfw_InitForVulkan(window, true);

            ImGui_ImplVulkan_InitInfo initInfo{};
            initInfo.Instance = instance;
            initInfo.PhysicalDevice = physicalDevice;
            initInfo.Device = device;
            initInfo.QueueFamily = queueFamily;
            initInfo.Queue = queue;
            initInfo.PipelineCache = VK_NULL_HANDLE;
            initInfo.DescriptorPool = m_DescriptorPool;
            initInfo.Subpass = 0;
            initInfo.MinImageCount = imageCount;
            initInfo.ImageCount = imageCount;
            initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

            ImGui_ImplVulkan_Init(&initInfo, renderPass);

            VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandPool = commandPool;
            allocInfo.commandBufferCount = 1;

            VkCommandBuffer commandBuffer;
            vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

            VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(commandBuffer, &beginInfo);
            ImGui_ImplVulkan_CreateFontsTexture(commandBuffer);
            vkEndCommandBuffer(commandBuffer);

            VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffer;
            vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
            vkDeviceWaitIdle(device);

            ImGui_ImplVulkan_DestroyFontUploadObjects();
            vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);

            TR_CORE_INFO("-------IMGUI INITIALIZED-------");
        }

        void ImGuiLayer::Shutdown()
        {
            TR_CORE_TRACE("Shutting Down ImGui");
            ImGui_ImplVulkan_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
            if (m_DescriptorPool != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
                m_DescriptorPool = VK_NULL_HANDLE;
            }
            m_Device = VK_NULL_HANDLE;
        }

        void ImGuiLayer::BeginFrame()
        {
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
        }

        void ImGuiLayer::EndFrame()
        {
            ImGui::Render();
        }

        void ImGuiLayer::Render(VkCommandBuffer commandBuffer)
        {
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
        }
    }
}