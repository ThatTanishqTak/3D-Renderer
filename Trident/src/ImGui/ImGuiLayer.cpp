#include "ImGui/ImGuiLayer.h"
#include "Application.h"             // for Application::GetDevice(), etc.
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

namespace Trident
{
    void ImGuiLayer::Init(GLFWwindow* window, VkInstance instance, VkDevice device, VkPhysicalDevice physicalDevice,
        uint32_t queueFamily, VkQueue queue, VkRenderPass renderPass, uint32_t imageCount, VkCommandPool commandPool)
    {
        m_Window = window;
        m_CommandPool = commandPool;

        VkDescriptorPoolSize pool_sizes[] =
        {
            { VK_DESCRIPTOR_TYPE_SAMPLER,               1000 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1000 },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,         1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,         1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,  1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,  1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,        1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,        1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,1000 },
            { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,      1000 }
        };

        VkDescriptorPoolCreateInfo pool_info{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = IM_ARRAYSIZE(pool_sizes) * 1000;
        pool_info.poolSizeCount = IM_ARRAYSIZE(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;
        vkCreateDescriptorPool(device, &pool_info, nullptr, &m_DescriptorPool);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
        ImGui::StyleColorsDark();

        ImGui_ImplGlfw_InitForVulkan(window, true);
        ImGui_ImplVulkan_InitInfo init_info{};
        init_info.Instance = instance;
        init_info.PhysicalDevice = physicalDevice;
        init_info.Device = device;
        init_info.QueueFamily = queueFamily;
        init_info.Queue = queue;
        init_info.DescriptorPool = m_DescriptorPool;
        init_info.MinImageCount = imageCount;
        init_info.ImageCount = imageCount;
        init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        init_info.RenderPass = renderPass;
        
        ImGui_ImplVulkan_Init(&init_info);
    }

    void ImGuiLayer::Begin()
    {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void ImGuiLayer::SetupDockspace()
    {
        ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_PassthruCentralNode;
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking 
            | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoBackground;
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::SetNextWindowViewport(vp->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::Begin("DockSpace", nullptr, flags);
        ImGui::PopStyleVar();
        ImGuiID dockId = ImGui::GetID("TridentDockSpace");
        ImGui::DockSpace(dockId, ImVec2(0, 0), dockspaceFlags);
        ImGui::End();
    }

    void ImGuiLayer::End(VkCommandBuffer commandBuffer)
    {
        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
    }

    void ImGuiLayer::Shutdown()
    {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
     
        if (m_DescriptorPool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(Application::GetDevice(), m_DescriptorPool, nullptr);
        }
    }

    ImTextureID ImGuiLayer::RegisterTexture(VkSampler sampler, VkImageView imageView, VkImageLayout layout)
    {
        return (ImTextureID)ImGui_ImplVulkan_AddTexture(sampler, imageView, layout);
    }
}