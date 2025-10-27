#pragma once

#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <new>
#include <deque>
#include <optional>
#include <unordered_set>

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/string_cast.hpp"

// This ignores all warnings raised inside External headers
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

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

        // Thread-safe buffer that captures log entries for the editor console.
        class ConsoleLog
        {
        public:
            struct Entry
            {
                spdlog::level::level_enum Level = spdlog::level::info;             // Severity used for filtering and styling.
                std::chrono::system_clock::time_point Timestamp{};                  // Moment the message was recorded.
                std::string Message;                                                // Final formatted log message.
            };

        public:
            static void PushEntry(spdlog::level::level_enum level, std::string message);
            static std::vector<Entry> GetSnapshot();
            static void Clear();

        private:
            static void PruneIfNeeded();

        private:
            static std::mutex s_BufferMutex;                                           // Synchronises access to the buffer.
            static std::deque<Entry> s_Buffer;                                         // Ring buffer that stores the captured messages.
            static size_t s_MaxEntries;                                                // Hard cap that prevents unbounded growth.
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

        // Service responsible for polling selected directories and turning file edits into reload tasks.
        class FileWatcher
        {
        public:
            enum class WatchType
            {
                Unknown,
                Shader,
                Model,
                Texture
            };

            enum class ReloadStatus
            {
                Detected,
                Queued,
                Success,
                Failed
            };

            struct ReloadEvent
            {
                uint64_t Id = 0;                                             // Unique identifier used by the UI and renderer.
                WatchType Type = WatchType::Unknown;                          // Which subsystem should handle the reload.
                std::string Path;                                            // File that triggered the reload.
                std::filesystem::file_time_type Timestamp{};                 // Timestamp captured when the change was detected.
                ReloadStatus Status = ReloadStatus::Detected;                 // Current processing state.
                std::string Message;                                         // Optional diagnostic message populated after processing.
            };

        public:
            static FileWatcher& Get();

            void RegisterDefaultDirectories();
            void RegisterWatch(const std::filesystem::path& directory, WatchType type, const std::vector<std::string>& extensions);

            void Poll();

            const std::vector<ReloadEvent>& GetEvents() const { return m_Events; }

            std::optional<ReloadEvent> PopPendingEvent();
            void QueueEvent(uint64_t eventId);
            void MarkEventSuccess(uint64_t eventId, const std::string& message);
            void MarkEventFailure(uint64_t eventId, const std::string& message);

            void EnableAutoReload(bool enabled) { m_AutoReload = enabled; }
            bool IsAutoReloadEnabled() const { return m_AutoReload; }

        private:
            struct WatchDirectory
            {
                std::filesystem::path Directory;                                             // Root directory being observed.
                WatchType Type = WatchType::Unknown;                                         // Type of reload triggered by this directory.
                std::vector<std::string> Extensions;                                         // Lower-case extensions allowed in this watch.
                std::unordered_map<std::string, std::filesystem::file_time_type> KnownFiles;  // Cached timestamps per file.
                bool ReportedMissing = false;                                                // Prevent spamming the log when folders are absent.
            };

            FileWatcher() = default;

            void ScanDirectory(WatchDirectory& watch);
            bool ShouldTrackFile(const WatchDirectory& watch, const std::filesystem::path& path) const;
            ReloadEvent& CreateEvent(const std::string& path, WatchType type, std::filesystem::file_time_type timestamp);
            void TransitionEvent(uint64_t eventId, ReloadStatus status, const std::string& message = {});

        private:
            std::vector<WatchDirectory> m_Watches;
            std::vector<ReloadEvent> m_Events;
            std::unordered_map<uint64_t, size_t> m_EventLookup;
            std::deque<uint64_t> m_PendingQueue;
            uint64_t m_NextEventId = 1;
            bool m_AutoReload = true;
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

		//------------------------------------------------------------------------------------------------------------------------------------------------------//

		class Allocation
		{
		public:
			static void ResetFrame();
			static void Increment();
			static size_t GetFrameCount();
			static void* Malloc(std::size_t size, const char* file, int line);
		};
	}
}

void* operator new(std::size_t size, const char* file, int line);
void operator delete(void* ptr, const char* file, int line) noexcept;
void* operator new(std::size_t size);
void operator delete(void* ptr) noexcept;
void* operator new[](std::size_t size);
void operator delete[](void* ptr) noexcept;

#ifndef _DEBUG
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
#else
// Core log macros
#define TR_CORE_TRACE(...)
#define TR_CORE_INFO(...) 
#define TR_CORE_WARN(...) 
#define TR_CORE_ERROR(...)
#define TR_CORE_CRITICAL(...) 

// Client log macros
#define TR_TRACE(...)
#define TR_INFO(...) 
#define TR_WARN(...) 
#define TR_ERROR(...)
#define TR_CRITICAL(...) 
#endif

#define TR_MALLOC(size) ::Trident::Utilities::Allocation::Malloc(size, __FILE__, __LINE__)
#define TR_NEW(TYPE, ...) new(__FILE__, __LINE__) TYPE(__VA_ARGS__)

#define BIT(x) (1 << x)