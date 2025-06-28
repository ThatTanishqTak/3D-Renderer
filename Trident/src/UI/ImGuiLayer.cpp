#include "UI/ImGuiLayer.h"

#include "Core/Utilities.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <algorithm>

namespace
{
    struct ScopedCommandBuffer
    {
        VkDevice Device = VK_NULL_HANDLE;
        VkCommandPool CommandPool = VK_NULL_HANDLE;
        VkQueue Queue = VK_NULL_HANDLE;
        VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;

        ScopedCommandBuffer(VkDevice device, VkCommandPool pool, VkQueue queue) : Device(device), CommandPool(pool), Queue(queue)
        {
            VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandPool = CommandPool;
            allocInfo.commandBufferCount = 1;

            vkAllocateCommandBuffers(Device, &allocInfo, &CommandBuffer);

            VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(CommandBuffer, &beginInfo);
        }

        ~ScopedCommandBuffer()
        {
            vkEndCommandBuffer(CommandBuffer);

            VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &CommandBuffer;
            vkQueueSubmit(Queue, 1, &submitInfo, VK_NULL_HANDLE);
            vkQueueWaitIdle(Queue);

            vkFreeCommandBuffers(Device, CommandPool, 1, &CommandBuffer);
        }
    };
}

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

            TR_CORE_TRACE("ImGui version: {}", IMGUI_VERSION);

            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
            ImGui::StyleColorsDark();

            if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
            {
                ImGuiStyle& style = ImGui::GetStyle();
                style.WindowRounding = 0.0f;
                style.Colors[ImGuiCol_WindowBg].w = 1.0f;
            }

            ImGui_ImplGlfw_InitForVulkan(window, true);

            ImGui_ImplVulkan_InitInfo initInfo{};
            initInfo.ApiVersion = VK_API_VERSION_1_0;
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
            initInfo.RenderPass = renderPass;
            //initInfo.CheckVkResultFn = [](VkResult err) { TR_CORE_ERROR("ImGui VkResult: {}", static_cast<int32_t>(err)); };

            ImGui_ImplVulkan_Init(&initInfo);
            ImGui_ImplVulkan_SetMinImageCount(imageCount);

            ScopedCommandBuffer fontCmd{ device, commandPool, queue };
            ImGui_ImplVulkan_CreateFontsTexture();
            ImGui_ImplVulkan_DestroyFontsTexture();

            TR_CORE_INFO("-------IMGUI INITIALIZED-------");
        }

        void ImGuiLayer::Shutdown()
        {
            TR_CORE_TRACE("Shutting Down ImGui");

            if (m_Device != VK_NULL_HANDLE)
            {
                vkDeviceWaitIdle(m_Device);
            }

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
            if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
            {
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
            }
        }

        void ImGuiLayer::Render(VkCommandBuffer commandBuffer)
        {
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
        }
    }
}