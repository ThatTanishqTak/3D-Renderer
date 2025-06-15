#include "Application.h"

#include <string>

namespace Trident
{
    Application* Application::s_Instance = nullptr;

    Application::Application(Window& window) : m_Window(window)
    {
        s_Instance = this;
    }

    void Application::Init()
    {
        Utilities::Time::Init();

        InitVulkan();

        m_Renderer = std::make_unique<Renderer>();
        m_Renderer->Init();
    }

    void Application::Update()
    {
        Utilities::Time::Update();
        m_Window.PollEvents();
    }

    void Application::RenderScene()
    {
        m_Renderer->DrawFrame();
    }

    void Application::Shutdown()
    {
        TR_CORE_TRACE("Shuting down Vulkan");

        CleanupVulkan();
        
        TR_CORE_TRACE("Vulkan Shutdown Complete");
    }

    //------------------------------------------------------------------------------------------------------------------------------------------------------//

    void Application::InitVulkan()
    {
        TR_CORE_INFO("-------INITIALIZING VULKAN-------");

        CreateInstance();
#ifndef NDEBUG
        SetupDebugMessenger();
#endif
        CreateSurface();
        PickPhysicalDevice();
        CreateLogicalDevice();

        TR_CORE_INFO("-------VULKAN INITIALIZED-------");
    }

    void Application::CleanupVulkan()
    {
        vkDeviceWaitIdle(m_Device);

        if (m_Renderer)
        {
            m_Renderer->Shutdown();
            m_Renderer.reset();
        }

        if (m_Surface != VK_NULL_HANDLE)
        {
            vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);

            m_Surface = VK_NULL_HANDLE;
        }

        if (m_Device != VK_NULL_HANDLE)
        {
            vkDestroyDevice(m_Device, nullptr);

            m_Device = VK_NULL_HANDLE;
        }

#ifndef NDEBUG
        if (m_DebugMessenger != VK_NULL_HANDLE)
        {
            auto a_Function = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_Instance, "vkDestroyDebugUtilsMessengerEXT");
            if (a_Function)
            {
                a_Function(m_Instance, m_DebugMessenger, nullptr);
            }

            m_DebugMessenger = VK_NULL_HANDLE;
        }
#endif
        if (m_Instance != VK_NULL_HANDLE)
        {
            vkDestroyInstance(m_Instance, nullptr);

            m_Instance = VK_NULL_HANDLE;
        }
    }

    //------------------------------------------------------------------------------------------------------------------------------------------------------//

    void Application::CreateInstance()
    {
        TR_CORE_TRACE("Creating Vulkan Instance");

        // VkApplicationInfo
        VkApplicationInfo l_AppInfo{};

        l_AppInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        l_AppInfo.pApplicationName = "Trident Engine";
        l_AppInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        l_AppInfo.pEngineName = "Trident";
        l_AppInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        l_AppInfo.apiVersion = VK_API_VERSION_1_2;

        std::vector<const char*> l_Layers;
#ifndef NDEBUG
        l_Layers.push_back("VK_LAYER_KHRONOS_validation");
#endif

#ifndef NDEBUG
        if (!CheckValidationLayerSupport())
        {
            TR_CORE_CRITICAL("Validation layers requested, but not available!");
        }
#endif

        // VkInstanceCreateInfo
        VkInstanceCreateInfo l_CreateInfo{};
        auto l_Extensions = GetRequiredExtensions();

#ifndef NDEBUG
        l_Extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

        l_CreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        l_CreateInfo.pApplicationInfo = &l_AppInfo;
        l_CreateInfo.enabledExtensionCount = static_cast<uint32_t>(l_Extensions.size());
        l_CreateInfo.ppEnabledExtensionNames = l_Extensions.data();
        l_CreateInfo.enabledLayerCount = static_cast<uint32_t>(l_Layers.size());
        l_CreateInfo.ppEnabledLayerNames = l_Layers.data();

        if (vkCreateInstance(&l_CreateInfo, nullptr, &m_Instance) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create Vulkan instance");
        }

        TR_CORE_TRACE("Vulkan Instance Created");
    }

#ifndef NDEBUG
    static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type,
        const VkDebugUtilsMessengerCallbackDataEXT* data, void*)
    {
        TR_CORE_CRITICAL("Validation: {}", data->pMessage);

        return VK_FALSE;
    }

    void Application::SetupDebugMessenger()
    {
        TR_CORE_TRACE("Setting Up Debug Messenger");

        auto a_Function = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_Instance, "vkCreateDebugUtilsMessengerEXT");

        if (!a_Function)
        {
            TR_CORE_ERROR("vkCreateDebugUtilsMessengerEXT not found");

            return;
        }

        VkDebugUtilsMessengerCreateInfoEXT l_DebugInfo{};
        l_DebugInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        l_DebugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        l_DebugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT  | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        l_DebugInfo.pfnUserCallback = DebugCallback;

        if (a_Function(m_Instance, &l_DebugInfo, nullptr, &m_DebugMessenger) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create debug messenger");
            
            return;
        }

        TR_CORE_TRACE("Debug Messenger Setup");
    }
#endif

    void Application::CreateSurface()
    {
        TR_CORE_TRACE("Creating GLFW Window Surface");

        if (glfwCreateWindowSurface(m_Instance, m_Window.GetNativeWindow(), nullptr, &m_Surface) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create window surface");
        }

        TR_CORE_TRACE("Window Surface Created");
    }

    void Application::PickPhysicalDevice()
    {
        TR_CORE_TRACE("Selecting Physical Device (GPU)");
        
        uint32_t l_DeviceCount = 0;
        vkEnumeratePhysicalDevices(m_Instance, &l_DeviceCount, nullptr);
        
        if (l_DeviceCount == 0)
        {
            TR_CORE_CRITICAL("No Vulkan-capable GPUs found");
        }

        std::vector<VkPhysicalDevice> l_Devices(l_DeviceCount);
        vkEnumeratePhysicalDevices(m_Instance, &l_DeviceCount, l_Devices.data());

        VkPhysicalDevice l_BestDevice = VK_NULL_HANDLE;

        for (const auto& it_Device : l_Devices)
        {
            if (!IsDeviceSuitable(it_Device))
            {
                continue;
            }

            VkPhysicalDeviceProperties l_Properites;
            vkGetPhysicalDeviceProperties(it_Device, &l_Properites);

            if (l_Properites.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            {
                m_PhysicalDevice = it_Device;
                m_QueueFamilyIndices = FindQueueFamilies(it_Device);

                break;
            }

            // fallback (if no discrete GPU is found)
            if (l_BestDevice == VK_NULL_HANDLE)
            {
                l_BestDevice = it_Device;
                m_QueueFamilyIndices = FindQueueFamilies(it_Device);
            }
        }

        if (m_PhysicalDevice == VK_NULL_HANDLE && l_BestDevice != VK_NULL_HANDLE)
        {
            m_PhysicalDevice = l_BestDevice;
        }

        if (m_PhysicalDevice == VK_NULL_HANDLE)
        {
            TR_CORE_CRITICAL("Failed to find a suitable GPU");
        }

        VkPhysicalDeviceProperties l_Properties{};
        vkGetPhysicalDeviceProperties(m_PhysicalDevice, &l_Properties);
        
        TR_CORE_TRACE("Selected GPU: {}", l_Properties.deviceName);
    }

    void Application::CreateLogicalDevice()
    {
        TR_CORE_TRACE("Creating Logical Device And Queues");

        auto a_QueueFamily = m_QueueFamilyIndices;
        if (!a_QueueFamily.IsComplete())
        {
            TR_CORE_CRITICAL("Queue it_Family indices not set");
            
            return;
        }
        std::set<uint32_t> l_UniqueFamilies = { a_QueueFamily.GraphicsFamily.value(), a_QueueFamily.PresentFamily.value() };

        float l_Priority = 1.0f;
        std::vector<VkDeviceQueueCreateInfo> l_QueueCreateInfo;

        for (auto it_Family : l_UniqueFamilies)
        {
            VkDeviceQueueCreateInfo l_DeviceQueueInfo{};

            l_DeviceQueueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            l_DeviceQueueInfo.queueFamilyIndex = it_Family;
            l_DeviceQueueInfo.queueCount = 1;
            l_DeviceQueueInfo.pQueuePriorities = &l_Priority;
            l_QueueCreateInfo.push_back(l_DeviceQueueInfo);
        }

        VkPhysicalDeviceFeatures l_Features{};
        VkDeviceCreateInfo l_DeviceCreateInfo{};

        l_DeviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        l_DeviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(l_QueueCreateInfo.size());
        l_DeviceCreateInfo.pQueueCreateInfos = l_QueueCreateInfo.data();
        l_DeviceCreateInfo.pEnabledFeatures = &l_Features;
        const char* l_Extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        l_DeviceCreateInfo.enabledExtensionCount = 1;
        l_DeviceCreateInfo.ppEnabledExtensionNames = l_Extensions;

        if (vkCreateDevice(m_PhysicalDevice, &l_DeviceCreateInfo, nullptr, &m_Device) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create logical it_Device");
        }

        vkGetDeviceQueue(m_Device, *a_QueueFamily.GraphicsFamily, 0, &m_GraphicsQueue);
        vkGetDeviceQueue(m_Device, *a_QueueFamily.PresentFamily, 0, &m_PresentQueue);

        TR_CORE_TRACE("Logical Device And Queues ready (GFX = {}, Present = {})", *a_QueueFamily.GraphicsFamily, *a_QueueFamily.PresentFamily);
    }

    //----------------------------------------------------------------------------------------------------------------------------------------------------------//

    std::vector<const char*> Application::GetRequiredExtensions()
    {
        uint32_t l_ExtensionCount = 0;
        const char** l_ExtensionName = glfwGetRequiredInstanceExtensions(&l_ExtensionCount);

        std::vector<const char*> l_Extension(l_ExtensionName, l_ExtensionName + l_ExtensionCount);

        return l_Extension;
    }

    QueueFamilyIndices Application::FindQueueFamilies(VkPhysicalDevice it_Device)
    {
        QueueFamilyIndices l_Indices;
        uint32_t l_Count = 0;

        vkGetPhysicalDeviceQueueFamilyProperties(it_Device, &l_Count, nullptr);
        std::vector<VkQueueFamilyProperties> l_Families(l_Count);
        vkGetPhysicalDeviceQueueFamilyProperties(it_Device, &l_Count, l_Families.data());

        for (uint32_t i = 0; i < l_Count; ++i)
        {
            if (l_Families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                l_Indices.GraphicsFamily = i;
            }

            VkBool32 l_IsPresent = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(it_Device, i, GetSurface(), &l_IsPresent);
            if (l_IsPresent)
            {
                l_Indices.PresentFamily = i;
            }

            if (l_Indices.IsComplete())
            {
                break;
            }
        }

        return l_Indices;
    }

    bool Application::CheckValidationLayerSupport()
    {
        TR_CORE_TRACE("Checking Validation Layer Support");

        uint32_t l_LayerCount = 0;
        vkEnumerateInstanceLayerProperties(&l_LayerCount, nullptr);

        std::vector<VkLayerProperties> l_AvailableLayers(l_LayerCount);
        vkEnumerateInstanceLayerProperties(&l_LayerCount, l_AvailableLayers.data());

        const std::vector<const char*> l_ValidationLayers = { "VK_LAYER_KHRONOS_validation" };

        for (const char* l_LayerName : l_ValidationLayers)
        {
            bool l_LayerFound = false;

            for (const auto& layerProps : l_AvailableLayers)
            {
                if (strcmp(l_LayerName, layerProps.layerName) == 0)
                {
                    l_LayerFound = true;

                    break;
                }
            }

            if (!l_LayerFound)
            {
                TR_CORE_TRACE("Validation layer {} not present", l_LayerName);

                return false;
            }
        }

        TR_CORE_TRACE("All requested validation layers are available");

        return true;
    }

    bool Application::IsDeviceSuitable(VkPhysicalDevice it_Device)
    {
        VkPhysicalDeviceProperties l_Properties{};
        vkGetPhysicalDeviceProperties(it_Device, &l_Properties);

        auto a_Indices = FindQueueFamilies(it_Device);

        // Check required it_Device extensions
        uint32_t l_ExtensionCount = 0;
        vkEnumerateDeviceExtensionProperties(it_Device, nullptr, &l_ExtensionCount, nullptr);
        std::vector<VkExtensionProperties> l_AvailableExtensions(l_ExtensionCount);
        vkEnumerateDeviceExtensionProperties(it_Device, nullptr, &l_ExtensionCount, l_AvailableExtensions.data());

        std::set<std::string> l_RequiredExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        for (const auto& a_Extensions : l_AvailableExtensions)
        {
            l_RequiredExtensions.erase(a_Extensions.extensionName);
        }

        bool l_ExtensionsSupported = l_RequiredExtensions.empty();

        bool l_SwapchainAdequate = false;
        if (l_ExtensionsSupported)
        {
            uint32_t l_FormatCount = 0;
            vkGetPhysicalDeviceSurfaceFormatsKHR(it_Device, GetSurface(), &l_FormatCount, nullptr);

            uint32_t l_PresentModeCount = 0;
            vkGetPhysicalDeviceSurfacePresentModesKHR(it_Device, GetSurface(), &l_PresentModeCount, nullptr);

            l_SwapchainAdequate = l_FormatCount > 0 && l_PresentModeCount > 0;
        }

        return a_Indices.IsComplete() && l_ExtensionsSupported && l_SwapchainAdequate;
    }
}