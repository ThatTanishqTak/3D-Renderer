#include "Application.h"

namespace Trident
{
    Application* Application::s_Instance = nullptr;
    QueueFamilyIndices m_QueueFamilyIndicies;

    Application::Application(Window& window) : m_Window(window)
    {
        s_Instance = this;
    }

    void Application::Init()
    {
        InitVulkan();

        m_Renderer = std::make_unique<Renderer>();
        m_Renderer->Init();
    }

    void Application::Update()
    {
        m_Window.PollEvents();

        m_Renderer->DrawFrame();
    }

    void Application::Shutdown()
    {
        CleanupVulkan();
        
        TR_CORE_INFO("Vulkan Shutdown.");
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
        TR_CORE_INFO("Creating Vulkan Instance");

        // VkApplicationInfo
        VkApplicationInfo l_AppInfo{};

        l_AppInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        l_AppInfo.pApplicationName = "Trident Engine";
        l_AppInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        l_AppInfo.pEngineName = "Trident";
        l_AppInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        l_AppInfo.apiVersion = VK_API_VERSION_1_2;

        std::vector<const char*> extensions =
        {
            VK_KHR_SURFACE_EXTENSION_NAME
        };

#ifndef NDEBUG
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

        std::vector<const char*> l_Layers;
#ifndef NDEBUG
        l_Layers.push_back("VK_LAYER_KHRONOS_validation");
#endif

        // VkInstanceCreateInfo
        VkInstanceCreateInfo l_CreateInfo{};
        auto l_Extensions = GetRequiredExtensions();

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

        TR_CORE_INFO("Vulkan Instance Created");
    }

#ifndef NDEBUG
    static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type,
        const VkDebugUtilsMessengerCallbackDataEXT* data, void*)
    {
        TR_CORE_CRITICAL("Validation: {}\n", data->pMessage);

        return VK_FALSE;
    }

    void Application::SetupDebugMessenger()
    {
        TR_CORE_INFO("Setting Up Debug Messenger");

        auto a_Function = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_Instance, "vkCreateDebugUtilsMessengerEXT");

        if (!a_Function)
        {
            return;
        }

        VkDebugUtilsMessengerCreateInfoEXT dbgInfo{};
        dbgInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        dbgInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dbgInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT  | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dbgInfo.pfnUserCallback = DebugCallback;

        TR_CORE_INFO("Debug Messenger Setup");
    }
#endif

    void Application::CreateSurface()
    {
        TR_CORE_INFO("Creating GLFW Window Surface");

        if (glfwCreateWindowSurface(m_Instance, m_Window.GetNativeWindow(), nullptr, &m_Surface) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create window surface");
        }

        TR_CORE_INFO("Window Surface Created");
    }

    void Application::PickPhysicalDevice()
    {
        TR_CORE_INFO("Selecting Physical Device (GPU)");
        
        uint32_t l_DeviceCount = 0;
        vkEnumeratePhysicalDevices(m_Instance, &l_DeviceCount, nullptr);
        
        if (l_DeviceCount == 0)
        {
            TR_CORE_CRITICAL("No Vulkan-capable GPUs found");
        }

        std::vector<VkPhysicalDevice> l_Devices(l_DeviceCount);
        vkEnumeratePhysicalDevices(m_Instance, &l_DeviceCount, l_Devices.data());

        for (const auto& it_Device : l_Devices)
        {
            if (IsDeviceSuitable(it_Device))
            {
                m_PhysicalDevice = it_Device;

                break;
            }
        }

        if (m_PhysicalDevice == VK_NULL_HANDLE)
        {
            TR_CORE_CRITICAL("Failed to find a suitable GPU");
        }

        VkPhysicalDeviceProperties l_Properties{};
        vkGetPhysicalDeviceProperties(m_PhysicalDevice, &l_Properties);
        
        TR_CORE_INFO("Selected GPU: {}", l_Properties.deviceName);
    }

    void Application::CreateLogicalDevice()
    {
        TR_CORE_INFO("Creating Logical Device And Queues");

        auto a_QueueFamily = FindQueueFamilies(m_PhysicalDevice);
        std::set<uint32_t> uniqueFamilies = { *a_QueueFamily.GraphicsFamily, *a_QueueFamily.PresentFamily };

        float prio = 1.0f;
        std::vector<VkDeviceQueueCreateInfo> l_QueueCreateInfo;

        for (auto family : uniqueFamilies)
        {
            VkDeviceQueueCreateInfo l_DeviceQueueInfo{};

            l_DeviceQueueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            l_DeviceQueueInfo.queueFamilyIndex = family;
            l_DeviceQueueInfo.queueCount = 1;
            l_DeviceQueueInfo.pQueuePriorities = &prio;
            l_QueueCreateInfo.push_back(l_DeviceQueueInfo);
        }

        VkPhysicalDeviceFeatures l_Features{};
        VkDeviceCreateInfo l_DeviceCreateInfo{};

        l_DeviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        l_DeviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(l_QueueCreateInfo.size());
        l_DeviceCreateInfo.pQueueCreateInfos = l_QueueCreateInfo.data();
        l_DeviceCreateInfo.pEnabledFeatures = &l_Features;
        const char* devExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        l_DeviceCreateInfo.enabledExtensionCount = 1;
        l_DeviceCreateInfo.ppEnabledExtensionNames = devExts;

        if (vkCreateDevice(m_PhysicalDevice, &l_DeviceCreateInfo, nullptr, &m_Device) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create logical device");
        }

        vkGetDeviceQueue(m_Device, *a_QueueFamily.GraphicsFamily, 0, &m_GraphicsQueue);
        vkGetDeviceQueue(m_Device, *a_QueueFamily.PresentFamily, 0, &m_PresentQueue);

        TR_CORE_INFO("Logical Device And Queues ready (GFX = {}, Present = {})", *a_QueueFamily.GraphicsFamily, *a_QueueFamily.PresentFamily);
    }

    //----------------------------------------------------------------------------------------------------------------------------------------------------------//

    std::vector<const char*> Application::GetRequiredExtensions()
    {
        uint32_t l_ExtensionCount = 0;
        const char** l_ExtensionName = glfwGetRequiredInstanceExtensions(&l_ExtensionCount);

        std::vector<const char*> l_Extension(l_ExtensionName, l_ExtensionName + l_ExtensionCount);

        return l_Extension;
    }

    QueueFamilyIndices Application::FindQueueFamilies(VkPhysicalDevice device)
    {
        QueueFamilyIndices l_Indices;
        uint32_t l_Count = 0;

        vkGetPhysicalDeviceQueueFamilyProperties(device, &l_Count, nullptr);
        std::vector<VkQueueFamilyProperties> l_Families(l_Count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &l_Count, l_Families.data());

        for (uint32_t i = 0; i < l_Count; ++i)
        {
            if (l_Families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                l_Indices.GraphicsFamily = i;
            }

            VkBool32 l_IsPresent = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, GetSurface(), &l_IsPresent);
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
}