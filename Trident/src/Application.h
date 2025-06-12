#pragma once

#include "Window/Window.h"

#include "Core/Utilities.h"

#include "Renderer/Renderer.h"

#include "ImGui/ImGuiLayer.h"

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include <optional>
#include <vector>
#include <set>
#include <memory>
#include <functional>

namespace Trident
{
    struct QueueFamilyIndices
    {
        std::optional<uint32_t> GraphicsFamily;
        std::optional<uint32_t> PresentFamily;

        bool IsComplete() const { return GraphicsFamily.has_value() && PresentFamily.has_value(); }
    };

    class Application
    {
    public:
        explicit Application(Window& window);

        void Init();
        void Update();

        void RenderScene();

        void Shutdown();

        static Application& Get() { return *s_Instance; }
        static VkInstance GetInstance() { return Get().m_Instance; }
        static VkPhysicalDevice GetPhysicalDevice() { return Get().m_PhysicalDevice; }
        static VkDevice GetDevice() { return Get().m_Device; }
        static VkSurfaceKHR GetSurface() { return Get().m_Surface; }
        static VkQueue GetGraphicsQueue() { return Get().m_GraphicsQueue; }
        static VkQueue GetPresentQueue() { return Get().m_PresentQueue; }
        static QueueFamilyIndices GetQueueFamilyIndices() { return Get().m_QueueFamilyIndices; }
        static Window& GetWindow() { return Get().m_Window; }
        static ImGuiLayer& GetImGuiLayer() { return *Get().m_ImGuiLayer; }
        static Renderer& GetRenderer() { return *Get().m_Renderer; }
        bool IsDeviceSuitable(VkPhysicalDevice device);

    private:
        Window& m_Window;
        VkInstance m_Instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
        VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
        VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
        VkDevice m_Device = VK_NULL_HANDLE;
        VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
        VkQueue m_PresentQueue = VK_NULL_HANDLE;
        QueueFamilyIndices m_QueueFamilyIndices;

        std::unique_ptr<Renderer> m_Renderer;
        std::unique_ptr<ImGuiLayer> m_ImGuiLayer;

        static Application* s_Instance;

    private:
        void InitVulkan();
        void CleanupVulkan();

        void CreateInstance();
        void SetupDebugMessenger();
        void CreateSurface();
        void PickPhysicalDevice();
        void CreateLogicalDevice();

        std::vector<const char*> GetRequiredExtensions();
        bool CheckValidationLayerSupport();

        QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device);
    };
}