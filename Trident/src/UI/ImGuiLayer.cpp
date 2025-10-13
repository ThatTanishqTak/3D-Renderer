#include "UI/ImGuiLayer.h"

#include "Application.h"
#include "Core/Utilities.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <filesystem>
#include <system_error>

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

            TR_CORE_TRACE("ImGui avaiable: {}", IMGUI_CHECKVERSION());

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

            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
            ImGui::StyleColorsDark();

            // Persist layout customisation to a renderer-scoped file so editor and runtime do not conflict.
            const std::filesystem::path l_LayoutDirectory{ "Assets/Layouts/" };
            const std::filesystem::path l_LayoutFile = "imgui.ini";
            m_LayoutIniFilePath = l_LayoutFile.string();
            io.IniFilename = m_LayoutIniFilePath.c_str();

            std::error_code l_DirectoryError{};
            std::filesystem::create_directories(l_LayoutDirectory, l_DirectoryError);
            if (l_DirectoryError)
            {
                TR_CORE_WARN("Unable to ensure ImGui layout directory '{}' exists: {}", l_LayoutDirectory.string(), l_DirectoryError.message());
            }

            // When no saved layout is available we bootstrap the hard-coded dock builder profile before ImGui writes a fresh file.
            if (std::filesystem::exists(l_LayoutFile))
            {
                if (!LoadLayoutFromDisk())
                {
                    TR_CORE_WARN("Falling back to default dockspace layout after failing to load '{}'.", m_LayoutIniFilePath);
                    ResetLayoutToDefault();
                }
            }
            else
            {
                TR_CORE_INFO("ImGui layout file '{}' not found. Applying default dockspace and awaiting user save.", m_LayoutIniFilePath);
                ResetLayoutToDefault();
            }

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
            initInfo.PipelineInfoMain.Subpass = 0;
            initInfo.MinImageCount = imageCount;
            initInfo.ImageCount = imageCount;
            initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
            initInfo.PipelineInfoMain.RenderPass = renderPass;
            //initInfo.CheckVkResultFn = [](VkResult err) { TR_CORE_ERROR("ImGui VkResult: {}", static_cast<int32_t>(err)); };

            ImGui_ImplVulkan_Init(&initInfo);
            ImGui_ImplVulkan_SetMinImageCount(imageCount);

            ScopedCommandBuffer fontCmd{ device, commandPool, queue };

            TR_CORE_INFO("-------IMGUI INITIALIZED-------");

            // Reset the dockspace layout flag whenever ImGui is re-initialized to ensure we rebuild the layout.
            m_DockspaceInitialized = false;
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
            m_DockspaceInitialized = false;

            TR_CORE_TRACE("ImGui Shutdown Complete");
        }

        void ImGuiLayer::BeginFrame()
        {
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            Dockspace();
        }

        void ImGuiLayer::Dockspace()
        {
            const ImGuiID l_DockspaceID = ImGui::DockSpaceOverViewport();

            vkDeviceWaitIdle(Application::GetDevice());

            if (m_DockspaceInitialized)
            {
                return;
            }

            // Rebuild the dockspace node tree only once to avoid rebuilding every frame.
            ImGui::DockBuilderRemoveNode(l_DockspaceID);
            ImGui::DockBuilderAddNode(l_DockspaceID, ImGuiDockNodeFlags_DockSpace);

            // The main dock node (center) is initially the same as the dockspace identifier.
            ImGuiID l_CenterNodeID = l_DockspaceID;

            // Split the main node to produce a dedicated area for the World Outliner on the left side.
            ImGuiID l_LeftNodeID = ImGui::DockBuilderSplitNode(l_CenterNodeID, ImGuiDir_Left, 0.20f, nullptr, &l_CenterNodeID);

            // Split the remaining center horizontally to create a region for the Details panel on the right.
            ImGuiID l_RightNodeID = ImGui::DockBuilderSplitNode(l_CenterNodeID, ImGuiDir_Right, 0.25f, nullptr, &l_CenterNodeID);

            // Split the updated center vertically to host the Content Browser and Output Log at the bottom.
            ImGuiID l_BottomNodeID = ImGui::DockBuilderSplitNode(l_CenterNodeID, ImGuiDir_Down, 0.30f, nullptr, &l_CenterNodeID);

            // Dock target windows to their dedicated nodes (tab bar order is Content Browser then Output Log).
            ImGui::DockBuilderDockWindow("Scene", l_CenterNodeID);                   // Central viewport for the scene rendering.
            ImGui::DockBuilderDockWindow("Scene Hierarchy", l_LeftNodeID);            // Hierarchy of objects sits on the left for quick access.
            ImGui::DockBuilderDockWindow("Inspector", l_RightNodeID);                  // Selected object properties live on the right.
            ImGui::DockBuilderDockWindow("Content Browser", l_BottomNodeID);         // Asset management panel anchored at the bottom.
            ImGui::DockBuilderDockWindow("Output Log", l_BottomNodeID);              // Output log shares the bottom area with the content browser.

            // Finalize the builder so ImGui can start presenting the configured dockspace.
            ImGui::DockBuilderFinish(l_DockspaceID);

            // Mark as initialized so the layout is not reconstructed on subsequent frames.
            m_DockspaceInitialized = true;
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

        bool ImGuiLayer::SaveLayoutToDisk() const
        {
            if (m_LayoutIniFilePath.empty())
            {
                TR_CORE_ERROR("Cannot save ImGui layout because the ini file path has not been initialised.");
                return false;
            }

            ImGui::SaveIniSettingsToDisk(m_LayoutIniFilePath.c_str());

            const bool l_FileExists = std::filesystem::exists(m_LayoutIniFilePath);
            if (!l_FileExists)
            {
                TR_CORE_WARN("ImGui reported saving layout data, but '{}' was not created.", m_LayoutIniFilePath);
                return false;
            }

            TR_CORE_INFO("Saved ImGui layout to '{}'.", m_LayoutIniFilePath);
            return true;
        }

        bool ImGuiLayer::LoadLayoutFromDisk()
        {
            if (m_LayoutIniFilePath.empty())
            {
                TR_CORE_ERROR("Cannot load ImGui layout because the ini file path has not been initialised.");
                return false;
            }

            if (!std::filesystem::exists(m_LayoutIniFilePath))
            {
                TR_CORE_WARN("ImGui layout file '{}' does not exist.", m_LayoutIniFilePath);
                return false;
            }

            ImGui::LoadIniSettingsFromDisk(m_LayoutIniFilePath.c_str());
            m_DockspaceInitialized = true;

            TR_CORE_INFO("Loaded ImGui layout from '{}'.", m_LayoutIniFilePath);
            return true;
        }

        void ImGuiLayer::ResetLayoutToDefault()
        {
            TR_CORE_INFO("Resetting ImGui layout to built-in dockspace arrangement.");

            // Clearing the active settings ensures the builder recreates the layout before any disk persistence occurs.
            ImGui::LoadIniSettingsFromMemory("", 0);
            m_DockspaceInitialized = false;

            if (!m_LayoutIniFilePath.empty())
            {
                std::error_code l_RemoveError{};
                std::filesystem::remove(m_LayoutIniFilePath, l_RemoveError);
                if (l_RemoveError)
                {
                    TR_CORE_WARN("Failed to remove previous layout file '{}': {}", m_LayoutIniFilePath, l_RemoveError.message());
                }
            }

            // Future improvement: support selecting between multiple layout presets and auto-versioning them here.
        }
    }
}