#include "ImGuiLayer.h"

#include "Core/Utilities.h"

#include <array>
#include <stdexcept>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

namespace Trident
{
    namespace UI
    {
        void ImGuiLayer::Init(GLFWwindow* window, VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, uint32_t queueFamily, VkQueue graphicsQueue,
            VkRenderPass renderPass, uint32_t imageCount, VkCommandPool commandPool)
        {
            // Cache parameters so future ImGui setup can access the renderer context.
            m_Window = window;
            m_Instance = instance;
            m_PhysicalDevice = physicalDevice;
            m_Device = device;
            m_QueueFamily = queueFamily;
            m_GraphicsQueue = graphicsQueue;
            m_RenderPass = renderPass;
            m_ImageCount = imageCount;
            m_CommandPool = commandPool;

            // Mark that the layer has been primed for future UI initialisation work.
            m_IsInitialized = true;
        }

        void ImGuiLayer::Initialize()
        {
            if (!m_IsInitialized)
            {
                TR_CORE_ERROR("ImGuiLayer::Init must be called before Initialize to provide Vulkan handles.");

                return;
            }

            if (m_IsImGuiContextReady)
            {
                TR_CORE_WARN("ImGuiLayer::Initialize called more than once; skipping duplicate setup.");

                return;
            }

            IMGUI_CHECKVERSION();

            m_ImGuiContext = ImGui::CreateContext();
            if (m_ImGuiContext == nullptr)
            {
                throw std::runtime_error("Failed to create ImGui context.");
            }

            ImGui::SetCurrentContext(m_ImGuiContext);

            // Enable editor friendly features out of the box so future UI panels can rely on docking and multiple viewports.
            ImGuiIO& l_IO = ImGui::GetIO();
            l_IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
#ifdef TRIDENT_IMGUI_VIEWPORTS
            l_IO.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
#endif
            l_IO.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

            ImGui::StyleColorsDark();

            if (l_IO.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
            {
                ImGuiStyle& l_Style = ImGui::GetStyle();
                l_Style.WindowRounding = 0.0f;
                l_Style.Colors[ImGuiCol_WindowBg].w = 1.0f;
            }

            // Allocate a descriptor pool dedicated to ImGui so UI allocations do not interfere with the renderer.
            std::array<VkDescriptorPoolSize, 11> l_PoolSizes = {
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
            };

            VkDescriptorPoolCreateInfo l_PoolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
            l_PoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            l_PoolInfo.maxSets = 1000 * static_cast<uint32_t>(l_PoolSizes.size());
            l_PoolInfo.poolSizeCount = static_cast<uint32_t>(l_PoolSizes.size());
            l_PoolInfo.pPoolSizes = l_PoolSizes.data();

            const VkResult l_PoolResult = vkCreateDescriptorPool(m_Device, &l_PoolInfo, nullptr, &m_ImGuiDescriptorPool);
            if (l_PoolResult != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create ImGui descriptor pool.");
            }

            // Bridge ImGui with GLFW and Vulkan using the cached handles collected during Init.
            ImGui_ImplGlfw_InitForVulkan(m_Window, true);

            ImGui_ImplVulkan_InitInfo l_InitInfo{};
            l_InitInfo.Instance = m_Instance;
            l_InitInfo.PhysicalDevice = m_PhysicalDevice;
            l_InitInfo.Device = m_Device;
            l_InitInfo.QueueFamily = m_QueueFamily;
            l_InitInfo.Queue = m_GraphicsQueue;
            l_InitInfo.PipelineCache = VK_NULL_HANDLE;
            l_InitInfo.DescriptorPool = m_ImGuiDescriptorPool;
            l_InitInfo.PipelineInfoMain.RenderPass = m_RenderPass;
            l_InitInfo.PipelineInfoMain.Subpass = 0;
            l_InitInfo.MinImageCount = m_ImageCount;
            l_InitInfo.ImageCount = m_ImageCount;
            l_InitInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
            l_InitInfo.CheckVkResultFn = nullptr;

            ImGui_ImplVulkan_Init(&l_InitInfo);

            // Upload fonts so ImGui can start returning textures immediately (needed for ImGui_ImplVulkan_AddTexture calls).
            VkCommandBufferAllocateInfo l_CmdAllocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
            l_CmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            l_CmdAllocInfo.commandPool = m_CommandPool;
            l_CmdAllocInfo.commandBufferCount = 1;

            VkCommandBuffer l_UploadCommandBuffer = VK_NULL_HANDLE;
            const VkResult l_AllocateResult = vkAllocateCommandBuffers(m_Device, &l_CmdAllocInfo, &l_UploadCommandBuffer);
            if (l_AllocateResult != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to allocate ImGui font upload command buffer.");
            }

            VkCommandBufferBeginInfo l_BeginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            l_BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(l_UploadCommandBuffer, &l_BeginInfo);

            vkEndCommandBuffer(l_UploadCommandBuffer);

            VkSubmitInfo l_SubmitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
            l_SubmitInfo.commandBufferCount = 1;
            l_SubmitInfo.pCommandBuffers = &l_UploadCommandBuffer;

            vkQueueSubmit(m_GraphicsQueue, 1, &l_SubmitInfo, VK_NULL_HANDLE);
            vkQueueWaitIdle(m_GraphicsQueue);
            vkFreeCommandBuffers(m_Device, m_CommandPool, 1, &l_UploadCommandBuffer);

            m_IsImGuiContextReady = true;

            TR_CORE_INFO("ImGui context initialised and font atlas uploaded.");
        }

        void ImGuiLayer::Shutdown()
        {
            if (!m_IsImGuiContextReady)
            {
                // Cached handles are still reset below so subsequent Init calls start fresh.
                m_IsInitialized = false;
                m_Window = nullptr;
                m_Instance = VK_NULL_HANDLE;
                m_PhysicalDevice = VK_NULL_HANDLE;
                m_Device = VK_NULL_HANDLE;
                m_QueueFamily = 0;
                m_GraphicsQueue = VK_NULL_HANDLE;
                m_RenderPass = VK_NULL_HANDLE;
                m_ImageCount = 0;
                m_CommandPool = VK_NULL_HANDLE;

                return;
            }

            ImGui_ImplVulkan_Shutdown();
            ImGui_ImplGlfw_Shutdown();

            if (m_ImGuiContext)
            {
                ImGui::DestroyContext(m_ImGuiContext);
                m_ImGuiContext = nullptr;
            }

            if (m_ImGuiDescriptorPool != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorPool(m_Device, m_ImGuiDescriptorPool, nullptr);
                m_ImGuiDescriptorPool = VK_NULL_HANDLE;
            }

            m_IsImGuiContextReady = false;
            m_IsInitialized = false;
            m_Window = nullptr;
            m_Instance = VK_NULL_HANDLE;
            m_PhysicalDevice = VK_NULL_HANDLE;
            m_Device = VK_NULL_HANDLE;
            m_QueueFamily = 0;
            m_GraphicsQueue = VK_NULL_HANDLE;
            m_RenderPass = VK_NULL_HANDLE;
            m_ImageCount = 0;
            m_CommandPool = VK_NULL_HANDLE;
        }

        void ImGuiLayer::Update()
        {
            // No per-frame UI updates yet; reserved for editor or tooling logic.
        }

        void ImGuiLayer::Render()
        {
            // Rendering is driven externally via the Vulkan command buffer overload.
        }

        void ImGuiLayer::Render(VkCommandBuffer commandBuffer)
        {
            if (!m_IsImGuiContextReady)
            {
                return;
            }

            ImDrawData* l_DrawData = ImGui::GetDrawData();
            if (l_DrawData == nullptr || l_DrawData->CmdListsCount == 0)
            {
                return;
            }

            ImGui_ImplVulkan_RenderDrawData(l_DrawData, commandBuffer);
        }

        void ImGuiLayer::BeginFrame()
        {
            if (!m_IsImGuiContextReady)
            {
                return;
            }

            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
        }

        void ImGuiLayer::EndFrame()
        {
            if (!m_IsImGuiContextReady)
            {
                return;
            }

            ImGui::Render();
        }

        void ImGuiLayer::RenderAdditionalViewports()
        {
            if (!m_IsImGuiContextReady)
            {
                return;
            }

            ImGuiIO& l_IO = ImGui::GetIO();
            if ((l_IO.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) == 0)
            {
                return;
            }

            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
    }
}