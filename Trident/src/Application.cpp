#include "Application.h"

#include "Renderer/RenderCommand.h"

#include "Loader/SceneLoader.h"
#include "Loader/ModelLoader.h"

#include "ECS/Scene.h"
#include "ECS/Components/MeshComponent.h"
#include "ECS/Components/TransformComponent.h"

#include <string>
#include <limits>
#include <utility>
#include <cstdint>
#include <filesystem>

namespace Trident
{
    Application* Application::s_Instance = nullptr;

    Application::Application(Window& window) : m_Window(window)
    {
        s_Instance = this;
        m_Scene = std::make_unique<Scene>(m_Registry);
    }

    void Application::Init()
    {
        Utilities::Time::Init();

        // Configure default watch directories before Vulkan is initialised so we capture edits early.
        Utilities::FileWatcher::Get().RegisterDefaultDirectories();

        InitVulkan();

        m_Renderer = std::make_unique<Renderer>();
        RenderCommand::Init();
    }

    void Application::Update()
    {
        Utilities::Time::Update();
        m_Window.PollEvents();

        // Poll the file watcher after window events so any edits get reflected in the next frame.
        Utilities::FileWatcher::Get().Poll();

        // Allow scene scripts and animations to advance even when no rendering occurs.
        if (m_Scene)
        {
            m_Scene->Update(Utilities::Time::GetDeltaTime());
        }
    }

    void Application::RenderScene()
    {
        RenderCommand::DrawFrame();
    }

    void Application::LoadScene(const std::string& path)
    {
        const std::filesystem::path l_Path(path);
        // The custom .trident format stores registry state; load through the Scene runtime.
        if (l_Path.extension() == ".trident")
        {
            if (!m_Scene)
            {
                m_Scene = std::make_unique<Scene>(m_Registry);
            }

            const bool l_Loaded = m_Scene->Load(path);
            if (!l_Loaded)
            {
                TR_CORE_ERROR("Unable to load .trident scene '{}'", path);
            }
            return;
        }

        // Fallback to the legacy folder-based loader for raw asset imports.
        Loader::SceneData l_Scene = Loader::SceneLoader::Load(path);
        const bool l_HasMeshes = !l_Scene.Meshes.empty();

        if (!l_HasMeshes)
        {
            // Clear cached geometry when the scene is empty so stale data is not reused.
            m_LoadedMeshes.clear();
            m_LoadedMaterials.clear();
            TR_CORE_WARN("Scene '{}' contained no meshes to upload.", path);

            return;
        }

        // Cache the imported geometry so future drag-and-drop additions can append seamlessly.
        m_LoadedMeshes = std::move(l_Scene.Meshes);
        m_LoadedMaterials = std::move(l_Scene.Materials);

        if (m_Renderer)
        {
            m_Renderer->UploadMesh(m_LoadedMeshes, m_LoadedMaterials);
        }
    }

    void Application::SaveScene(const std::string& path) const
    {
        if (!m_Scene)
        {
            TR_CORE_WARN("SaveScene skipped because no scene instance exists");
            return;
        }

        // Persist the registry to disk so sessions can be resumed later.
        m_Scene->Save(path);
    }

    void Application::PlayScene()
    {
        if (m_Scene)
        {
            // Enter play mode so scripts and animations begin updating each frame.
            m_Scene->Play();
        }
    }

    void Application::StopScene()
    {
        if (m_Scene)
        {
            // Return to edit mode and pause any running scripts.
            m_Scene->Stop();
        }
    }

    bool Application::IsScenePlaying() const
    {
        if (!m_Scene)
        {
            return false;
        }

        return m_Scene->IsPlaying();
    }

    ECS::Entity Application::ImportModelAsset(const std::string& path)
    {
        const ECS::Entity l_InvalidEntity = std::numeric_limits<ECS::Entity>::max();

        auto a_ModelData = Loader::ModelLoader::Load(path);
        if (a_ModelData.Meshes.empty())
        {
            TR_CORE_WARN("Drag-and-drop import skipped because '{}' produced no meshes.", path);
            return l_InvalidEntity;
        }

        // Remember where the new data will land so component indices remain stable.
        const size_t l_MaterialOffset = m_LoadedMaterials.size();
        const size_t l_MeshOffset = m_LoadedMeshes.size();

        // Append new materials while preserving existing assignments. Future improvement: de-duplicate shared materials.
        m_LoadedMaterials.reserve(m_LoadedMaterials.size() + a_ModelData.Materials.size());
        for (auto& it_Material : a_ModelData.Materials)
        {
            m_LoadedMaterials.push_back(std::move(it_Material));
        }

        // Merge meshes into the cache and remap material indices so the renderer sees a contiguous table.
        m_LoadedMeshes.reserve(m_LoadedMeshes.size() + a_ModelData.Meshes.size());
        for (auto& it_Mesh : a_ModelData.Meshes)
        {
            if (it_Mesh.MaterialIndex >= 0)
            {
                it_Mesh.MaterialIndex += static_cast<int32_t>(l_MaterialOffset);
            }
            m_LoadedMeshes.push_back(std::move(it_Mesh));
        }

        ECS::Entity l_FirstEntity = l_InvalidEntity;
        for (size_t it_MeshIndex = 0; it_MeshIndex < a_ModelData.Meshes.size(); ++it_MeshIndex)
        {
            ECS::Entity l_Entity = m_Registry.CreateEntity();
            if (l_FirstEntity == l_InvalidEntity)
            {
                l_FirstEntity = l_Entity;
            }

            // Default transform keeps new assets at the origin until gizmos reposition them.
            m_Registry.AddComponent<Transform>(l_Entity, Transform{});

            MeshComponent& l_MeshComponent = m_Registry.AddComponent<MeshComponent>(l_Entity);
            l_MeshComponent.m_MeshIndex = l_MeshOffset + it_MeshIndex;
            const Geometry::Mesh& l_SourceMesh = m_LoadedMeshes[l_MeshComponent.m_MeshIndex];
            l_MeshComponent.m_MaterialIndex = l_SourceMesh.MaterialIndex;
        }

        if (m_Renderer)
        {
            m_Renderer->UploadMesh(m_LoadedMeshes, m_LoadedMaterials);
        }

        TR_CORE_INFO("Imported '{}' via viewport drag-and-drop ({} meshes).", path, a_ModelData.Meshes.size());

        return l_FirstEntity;
    }

    void Application::Shutdown()
    {
        TR_CORE_TRACE("Shutting down Vulkan");

        CleanupVulkan();
        
        TR_CORE_TRACE("Vulkan Shutdown Complete");
    }

    //------------------------------------------------------------------------------------------------------------------------------------------------------//

    void Application::InitVulkan()
    {
        TR_CORE_INFO("-------INITIALIZING VULKAN-------");

        CreateInstance();
#ifdef _DEBUG
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
            RenderCommand::Shutdown();
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

#ifdef _DEBUG
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
#ifdef _DEBUG
        l_Layers.push_back("VK_LAYER_KHRONOS_validation");
#endif

#ifdef _DEBUG
        if (!CheckValidationLayerSupport())
        {
            TR_CORE_CRITICAL("Validation layers requested, but not available!");
        }
#endif

        // VkInstanceCreateInfo
        VkInstanceCreateInfo l_CreateInfo{};
        auto l_Extensions = GetRequiredExtensions();

#ifdef _DEBUG
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

#ifdef _DEBUG
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
            TR_CORE_CRITICAL("vkCreateDebugUtilsMessengerEXT not found");

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
            TR_CORE_CRITICAL("Queue family indices not set");
            
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
            TR_CORE_CRITICAL("Failed to create logical Device");
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

    bool Application::IsDeviceSuitable(VkPhysicalDevice device)
    {
        VkPhysicalDeviceProperties l_Properties{};
        vkGetPhysicalDeviceProperties(device, &l_Properties);

        auto a_Indices = FindQueueFamilies(device);

        // Check required device extensions
        uint32_t l_ExtensionCount = 0;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &l_ExtensionCount, nullptr);
        std::vector<VkExtensionProperties> l_AvailableExtensions(l_ExtensionCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &l_ExtensionCount, l_AvailableExtensions.data());

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
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, GetSurface(), &l_FormatCount, nullptr);

            uint32_t l_PresentModeCount = 0;
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, GetSurface(), &l_PresentModeCount, nullptr);

            l_SwapchainAdequate = l_FormatCount > 0 && l_PresentModeCount > 0;
        }

        return a_Indices.IsComplete() && l_ExtensionsSupported && l_SwapchainAdequate;
    }
}