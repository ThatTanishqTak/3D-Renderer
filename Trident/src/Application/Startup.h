#pragma once

#include "Vulkan/Vulkan.h"
#include "Renderer/Renderer.h"
#include "Renderer/Camera/RuntimeCamera.h"
#include "ECS/Registry.h"

#include <glm/vec3.hpp>

#include <optional>
#include <vector>
#include <memory>
#include <filesystem>

namespace Trident
{
    class Window;
    class Scene;

    struct QueueFamilyIndices
    {
        std::optional<uint32_t> GraphicsFamily;
        std::optional<uint32_t> PresentFamily;

        bool IsComplete() const { return GraphicsFamily.has_value() && PresentFamily.has_value(); }
    };

    class Startup
    {
    public:
        Startup(Window& window);
        ~Startup();

        static Startup& Get() { return *s_Instance; }
        static VkInstance GetInstance() { return Get().m_Instance; }
        static VkPhysicalDevice GetPhysicalDevice() { return Get().m_PhysicalDevice; }
        static VkDevice GetDevice() { return Get().m_Device; }
        static VkSurfaceKHR GetSurface() { return Get().m_Surface; }
        static VkQueue GetGraphicsQueue() { return Get().m_GraphicsQueue; }
        static VkQueue GetPresentQueue() { return Get().m_PresentQueue; }
        static QueueFamilyIndices GetQueueFamilyIndices() { return Get().m_QueueFamilyIndices; }
        static Window& GetWindow() { return Get().m_Window; }
        static Renderer& GetRenderer() { return Get().m_Renderer; }
        static Renderer* TryGetRenderer()
        {
            // Expose a safe pointer for callers that may run before the singleton has finished constructing.
            return s_Instance ? &s_Instance->m_Renderer : nullptr;
        }
        static ECS::Registry& GetRegistry() { return Get().m_Registry; }
        static bool HasInstance() { return s_Instance != nullptr; }
        static const std::filesystem::path& GetPackagedScenePath() { return Get().m_PackagedScenePath; }
        static const std::filesystem::path& GetPackagedContentDirectory() { return Get().m_PackagedContentDirectory; }

        void ApplyPackagedRuntimeState();

    private:
        void Initialize();
        void Shutdown();

        void DiscoverPackagedContent();
        void LoadPackagedCameraTransform();

        void CreateInstance();
        void SetupDebugMessenger();
        void CreateSurface();
        void PickPhysicalDevice();
        void CreateLogicalDevice();

        std::vector<const char*> GetRequiredExtensions();
        bool CheckValidationLayerSupport();
        QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device);
        bool IsDeviceSuitable(VkPhysicalDevice device);

    private:
        // Store a reference instead of a value to avoid copying the non-copyable
        // Window wrapper while still giving Startup long-term access.
        Window& m_Window;
        Renderer m_Renderer;
        ECS::Registry m_Registry;

        std::filesystem::path m_PackagedContentDirectory;
        std::filesystem::path m_PackagedScenePath;
        std::unique_ptr<Scene> m_PackagedScene;
        std::optional<glm::vec3> m_PackagedCameraPosition;
        std::optional<glm::vec3> m_PackagedCameraRotation;
        Trident::RuntimeCamera m_PackagedRuntimeCamera;
        bool m_HasAppliedPackagedState = false;

        VkInstance m_Instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
        VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
        VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
        VkDevice m_Device = VK_NULL_HANDLE;
        VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
        VkQueue m_PresentQueue = VK_NULL_HANDLE;
        QueueFamilyIndices m_QueueFamilyIndices;

        static Startup* s_Instance;
    };
}