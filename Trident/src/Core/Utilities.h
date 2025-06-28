#pragma once

#include <fstream>
#include <filesystem>

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/string_cast.hpp"

// This ignores all warnings raised inside External headers
#pragma warning(push, 0)
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>
#pragma warning(pop)

#include <GLFW/glfw3.h>

namespace Trident
{
	namespace Utilities
	{
		class Log
		{
		public:
			static void Init();

			static std::shared_ptr<spdlog::logger>& GetCoreLogger() { return s_CoreLogger; }
			static std::shared_ptr<spdlog::logger>& GetClientLogger() { return s_ClientLogger; }

		private:
			static std::shared_ptr<spdlog::logger> s_CoreLogger;
			static std::shared_ptr<spdlog::logger> s_ClientLogger;
		};

		//------------------------------------------------------------------------------------------------------------------------------------------------------//

		class FileManagement
		{
		public:
			static std::vector<char> ReadFile(const std::string& filePath);
			static std::string NormalizePath(const std::string& path);
			static std::string GetBaseDirectory(const std::string& filePath);

			static std::vector<char> ReadBinaryFile(const std::string& filePath) { return ReadFile(filePath); }
			static std::string GetExtension(const std::string& filePath);
			static std::string JoinPath(const std::string& base, const std::string& addition);
		};

		//------------------------------------------------------------------------------------------------------------------------------------------------------//

		class Time
		{
		public:
			static void Init();
			static void Update();

			static float GetDeltaTime() { return s_DeltaTime; }
			static float GetTime() { return static_cast<float>(glfwGetTime()); }
			static float GetFPS() { return s_DeltaTime > 0.0f ? 1.0f / s_DeltaTime : 0.0f; }

		private:
			static double s_LastTime;
			static float s_DeltaTime;
		};
	}
}

// Core log macros
#define TR_CORE_TRACE(...) ::Trident::Utilities::Log::GetCoreLogger()->trace(__VA_ARGS__)
#define TR_CORE_INFO(...) ::Trident::Utilities::Log::GetCoreLogger()->info(__VA_ARGS__)
#define TR_CORE_WARN(...) ::Trident::Utilities::Log::GetCoreLogger()->warn(__VA_ARGS__)
#define TR_CORE_ERROR(...) ::Trident::Utilities::Log::GetCoreLogger()->error(__VA_ARGS__)
#define TR_CORE_CRITICAL(...) ::Trident::Utilities::Log::GetCoreLogger()->critical(__VA_ARGS__)

// Client log macros
#define TR_TRACE(...) ::Trident::Utilities::Log::GetClientLogger()->trace(__VA_ARGS__)
#define TR_INFO(...) ::Trident::Utilities::Log::GetClientLogger()->info(__VA_ARGS__)
#define TR_WARN(...) ::Trident::Utilities::Log::GetClientLogger()->warn(__VA_ARGS__)
#define TR_ERROR(...) ::Trident::Utilities::Log::GetClientLogger()->error(__VA_ARGS__)
#define TR_CRITICAL(...) ::Trident::Utilities::Log::GetClientLogger()->critical(__VA_ARGS__)