#pragma once

#include "Vulkan/Vulkan.h"
#include "Window/Window.h"
#include "Renderer/Renderer.h"
#include "ECS/Registry.h"

#include <vector>
#include <optional>

namespace Trident
{
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
		static ECS::Registry& GetRegistry() { return Get().m_Registry; }

	private:
		void Initialize();
		void Shutdown();

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
		Window m_Window;
		Renderer m_Renderer;
		ECS::Registry m_Registry;

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