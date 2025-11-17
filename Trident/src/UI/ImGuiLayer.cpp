#include "UI/ImGuiLayer.h"

#include "Core/Utilities.h"

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <array>
#include <stdexcept>

namespace Trident
{
    namespace UI
    {
        ImGuiLayer::ImGuiLayer() = default;

        ImGuiLayer::~ImGuiLayer()
        {
            Shutdown();
        }

        void ImGuiLayer::Init(GLFWwindow* window, VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, uint32_t graphicsQueueFamily, VkQueue graphicsQueue,
            VkRenderPass renderPass, uint32_t imageCount, VkCommandPool commandPool)
        {
            // If the layer is being reinitialised, ensure previous resources are released first.
            if (m_Initialised)
            {
                Shutdown();
            }

            m_Window = window;
            m_Instance = instance;
            m_PhysicalDevice = physicalDevice;
            m_Device = device;
            m_GraphicsQueueFamily = graphicsQueueFamily;
            m_GraphicsQueue = graphicsQueue;
            m_RenderPass = renderPass;
            m_ImageCount = imageCount;
            m_CommandPool = commandPool;

            IMGUI_CHECKVERSION();
            ImGui::CreateContext();

            ImGuiIO& l_IO = ImGui::GetIO();
            l_IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
#ifdef TRIDENT_IMGUI_VIEWPORTS
            l_IO.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
#endif // TRIDENT_IMGUI_VIEWPORTS

            // Apply a consistent editor visual style and enable key ImGui features.
            m_StyleManager.ApplyStyle(l_IO);

            CreateDescriptorPool();

            // Initialise the platform backend. We install callbacks so input is forwarded automatically.
            ImGui_ImplGlfw_InitForVulkan(m_Window, true);

            ImGui_ImplVulkan_InitInfo l_InitInfo{};
            l_InitInfo.Instance = m_Instance;
            l_InitInfo.PhysicalDevice = m_PhysicalDevice;
            l_InitInfo.Device = m_Device;
            l_InitInfo.QueueFamily = m_GraphicsQueueFamily;
            l_InitInfo.Queue = m_GraphicsQueue;
            l_InitInfo.PipelineCache = VK_NULL_HANDLE;
            l_InitInfo.DescriptorPool = m_DescriptorPool;
            l_InitInfo.PipelineInfoMain.Subpass = 0;
            l_InitInfo.MinImageCount = m_ImageCount;
            l_InitInfo.ImageCount = m_ImageCount;
            l_InitInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
            l_InitInfo.Allocator = nullptr;
            l_InitInfo.CheckVkResultFn = nullptr;

            ImGui_ImplVulkan_Init(&l_InitInfo);

            UploadFonts();

            m_Initialised = true;
            TR_CORE_INFO("ImGui layer initialised with {} swapchain images", m_ImageCount);
        }

        void ImGuiLayer::BeginFrame()
        {
            if (!m_Initialised)
            {
                return;
            }

            // Prepare ImGui to receive widgets for the new frame.
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
        }

        void ImGuiLayer::EndFrame()
        {
            if (!m_Initialised)
            {
                return;
            }

            // Finalise draw data. The renderer will submit the command buffer via Render().
            ImGui::Render();

            ImGuiIO& l_IO = ImGui::GetIO();
            if (l_IO.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
            {
                // Multi-viewport support renders additional platform windows outside the primary swapchain.
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
            }
        }

        void ImGuiLayer::Render(VkCommandBuffer commandBuffer)
        {
            if (!m_Initialised)
            {
                return;
            }

            // The renderer has already begun the render pass. Feed ImGui's draw data into the command buffer.
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
        }

        void ImGuiLayer::Shutdown()
        {
            if (!m_Initialised)
            {
                return;
            }

            ImGui_ImplVulkan_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();

            DestroyDescriptorPool();

            m_Window = nullptr;
            m_Instance = VK_NULL_HANDLE;
            m_PhysicalDevice = VK_NULL_HANDLE;
            m_Device = VK_NULL_HANDLE;
            m_RenderPass = VK_NULL_HANDLE;
            m_ImageCount = 0;
            m_CommandPool = VK_NULL_HANDLE;
            m_GraphicsQueue = VK_NULL_HANDLE;
            m_GraphicsQueueFamily = 0;
            m_Initialised = false;

            // Keep a log trail for debugging shutdown ordering issues.
            TR_CORE_INFO("ImGui layer shutdown complete");
        }

        void ImGuiLayer::CreateDescriptorPool()
        {
            // TODO: Allow the descriptor pool sizing to be driven by configuration so lightweight tools can reserve less memory.
            std::array<VkDescriptorPoolSize, 11> l_PoolSizes{};
            l_PoolSizes[0] = { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 };
            l_PoolSizes[1] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 };
            l_PoolSizes[2] = { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 };
            l_PoolSizes[3] = { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 };
            l_PoolSizes[4] = { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 };
            l_PoolSizes[5] = { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 };
            l_PoolSizes[6] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 };
            l_PoolSizes[7] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 };
            l_PoolSizes[8] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 };
            l_PoolSizes[9] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 };
            l_PoolSizes[10] = { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 };

            VkDescriptorPoolCreateInfo l_PoolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
            l_PoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            l_PoolInfo.maxSets = 1000 * static_cast<uint32_t>(l_PoolSizes.size());
            l_PoolInfo.poolSizeCount = static_cast<uint32_t>(l_PoolSizes.size());
            l_PoolInfo.pPoolSizes = l_PoolSizes.data();

            VkResult l_Result = vkCreateDescriptorPool(m_Device, &l_PoolInfo, nullptr, &m_DescriptorPool);
            if (l_Result != VK_SUCCESS)
            {
                TR_CORE_CRITICAL("Failed to create ImGui descriptor pool: {}", static_cast<int>(l_Result));
                throw std::runtime_error("Failed to create ImGui descriptor pool");
            }
        }

        void ImGuiLayer::DestroyDescriptorPool()
        {
            if (m_DescriptorPool != VK_NULL_HANDLE && m_Device != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
                m_DescriptorPool = VK_NULL_HANDLE;
            }
        }

        void ImGuiLayer::UploadFonts()
        {
            VkCommandBufferAllocateInfo l_AllocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
            l_AllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            l_AllocInfo.commandPool = m_CommandPool;
            l_AllocInfo.commandBufferCount = 1;

            VkCommandBuffer l_CommandBuffer = VK_NULL_HANDLE;
            VkResult l_Result = vkAllocateCommandBuffers(m_Device, &l_AllocInfo, &l_CommandBuffer);
            if (l_Result != VK_SUCCESS)
            {
                TR_CORE_CRITICAL("Failed to allocate ImGui font upload command buffer: {}", static_cast<int>(l_Result));
                throw std::runtime_error("Failed to allocate ImGui font upload command buffer");
            }

            VkCommandBufferBeginInfo l_BeginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            l_BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            l_Result = vkBeginCommandBuffer(l_CommandBuffer, &l_BeginInfo);
            if (l_Result != VK_SUCCESS)
            {
                TR_CORE_CRITICAL("Failed to begin ImGui font upload command buffer: {}", static_cast<int>(l_Result));
                throw std::runtime_error("Failed to begin ImGui font upload command buffer");
            }

            //ImGui_ImplVulkan_CreateFontsTexture(l_CommandBuffer);

            l_Result = vkEndCommandBuffer(l_CommandBuffer);
            if (l_Result != VK_SUCCESS)
            {
                TR_CORE_CRITICAL("Failed to end ImGui font upload command buffer: {}", static_cast<int>(l_Result));
                throw std::runtime_error("Failed to end ImGui font upload command buffer");
            }

            VkSubmitInfo l_SubmitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
            l_SubmitInfo.commandBufferCount = 1;
            l_SubmitInfo.pCommandBuffers = &l_CommandBuffer;
            l_Result = vkQueueSubmit(m_GraphicsQueue, 1, &l_SubmitInfo, VK_NULL_HANDLE);
            if (l_Result != VK_SUCCESS)
            {
                TR_CORE_CRITICAL("Failed to submit ImGui font upload command buffer: {}", static_cast<int>(l_Result));
                throw std::runtime_error("Failed to submit ImGui font upload command buffer");
            }

            l_Result = vkQueueWaitIdle(m_GraphicsQueue);
            if (l_Result != VK_SUCCESS)
            {
                TR_CORE_CRITICAL("Failed to idle graphics queue after ImGui font upload: {}", static_cast<int>(l_Result));
                throw std::runtime_error("Failed to idle graphics queue after ImGui font upload");
            }

            //ImGui_ImplVulkan_DestroyFontUploadObjects();

            vkFreeCommandBuffers(m_Device, m_CommandPool, 1, &l_CommandBuffer);
        }
    }
}