#include "Utilities.h"

#include "Loader/AssimpExtensions.h"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/base_sink.h>

namespace Trident
{
    namespace Utilities
    {
        namespace
        {
            // Custom sink that forwards every log record into the ImGui console buffer.
            class ImGuiLogSink final : public spdlog::sinks::base_sink<std::mutex>
            {
            protected:
                void sink_it_(const spdlog::details::log_msg& a_Message) override
                {
                    spdlog::memory_buf_t l_Buffer;
                    formatter_->format(a_Message, l_Buffer);

                    ConsoleLog::PushEntry(a_Message.level, fmt::to_string(l_Buffer));
                }

                void flush_() override
                {
                    // The console buffer is memory backed, so there is nothing to flush explicitly.
                }
            };
        }

        std::shared_ptr<spdlog::logger> Log::s_CoreLogger;
        std::shared_ptr<spdlog::logger> Log::s_ClientLogger;
        std::mutex ConsoleLog::s_BufferMutex;
        std::deque<ConsoleLog::Entry> ConsoleLog::s_Buffer;
        size_t ConsoleLog::s_MaxEntries = 2000;

        void ConsoleLog::PushEntry(spdlog::level::level_enum level, std::string message)
        {
            // Guard access so log calls from multiple threads do not corrupt the buffer.
            std::lock_guard<std::mutex> l_Lock(s_BufferMutex);

            Entry l_Entry{};
            l_Entry.Level = level;
            l_Entry.Timestamp = std::chrono::system_clock::now();
            l_Entry.Message = std::move(message);

            s_Buffer.push_back(std::move(l_Entry));
            PruneIfNeeded();
        }

        std::vector<ConsoleLog::Entry> ConsoleLog::GetSnapshot()
        {
            std::lock_guard<std::mutex> l_Lock(s_BufferMutex);
            return { s_Buffer.begin(), s_Buffer.end() };
        }

        void ConsoleLog::Clear()
        {
            std::lock_guard<std::mutex> l_Lock(s_BufferMutex);
            s_Buffer.clear();
        }

        void ConsoleLog::PruneIfNeeded()
        {
            if (s_Buffer.size() <= s_MaxEntries)
            {
                return;
            }

            const size_t l_Overflow = s_Buffer.size() - s_MaxEntries;
            for (size_t l_Index = 0; l_Index < l_Overflow; ++l_Index)
            {
                s_Buffer.pop_front();
            }
        }

        void Log::Init()
        {
            // Reset the in-memory console buffer each time logging is initialised so sessions start cleanly.
            ConsoleLog::Clear();

            std::vector<spdlog::sink_ptr> logSinks;
            logSinks.emplace_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
            logSinks.emplace_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>("Trident.log", true));
            logSinks.emplace_back(std::make_shared<ImGuiLogSink>());

            logSinks[0]->set_pattern("%^[%T] %n: %v%$");
            logSinks[1]->set_pattern("[%T] [%l] %n: %v");
            logSinks[2]->set_pattern("[%l] %n: %v");

            s_CoreLogger = std::make_shared<spdlog::logger>("TRIDENT", begin(logSinks), end(logSinks));
            spdlog::register_logger(s_CoreLogger);
            s_CoreLogger->set_level(spdlog::level::trace);
            s_CoreLogger->flush_on(spdlog::level::trace);

            s_ClientLogger = std::make_shared<spdlog::logger>("TRIDENT-FORGE", begin(logSinks), end(logSinks));
            spdlog::register_logger(s_ClientLogger);
            s_ClientLogger->set_level(spdlog::level::trace);
            s_ClientLogger->flush_on(spdlog::level::trace);
        }

        //------------------------------------------------------------------------------------------------------------------------------------------------------//

        // Helper used to normalise file extensions to lower-case for comparisons.
        std::string ToLower(std::string a_Value)
        {
            std::transform(a_Value.begin(), a_Value.end(), a_Value.begin(), [](unsigned char a_Char) { return static_cast<char>(std::tolower(a_Char)); });

            return a_Value;
        }

        FileWatcher& FileWatcher::Get()
        {
            static FileWatcher s_Instance{}; // Lifetime of the watcher service spans the application run.
            return s_Instance;
        }

        void FileWatcher::RegisterDefaultDirectories()
        {
            static const std::vector<std::string> s_ShaderExtensions{ ".vert", ".frag", ".comp", ".geom", ".tesc", ".tese" };
            static const std::vector<std::string> s_TextureExtensions{ ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".ktx", ".hdr", ".dds" };

            // Model hot-reload support mirrors Assimp's importer capabilities so we avoid hard-coding the format list.
            const std::vector<std::string>& l_AssimpExtensions = Loader::AssimpExtensions::GetNormalizedExtensions();

            // Guard against environments where Assimp is unavailable by falling back to a minimal, well-tested set of extensions.
            // TODO: Expand the fallback list or persist the generated list if offline cache becomes necessary.
            const std::vector<std::string>* l_ModelExtensions = &l_AssimpExtensions;
            if (l_AssimpExtensions.empty())
            {
                static const std::vector<std::string> s_MinimalModelExtensions{ ".gltf", ".glb", ".fbx" };
                l_ModelExtensions = &s_MinimalModelExtensions;
            }

            RegisterWatch(std::filesystem::path("Assets") / "Shaders", WatchType::Shader, s_ShaderExtensions);
            RegisterWatch(std::filesystem::path("Assets") / "Models", WatchType::Model, *l_ModelExtensions);
            RegisterWatch(std::filesystem::path("Assets") / "Textures", WatchType::Texture, s_TextureExtensions);
        }

        void FileWatcher::RegisterWatch(const std::filesystem::path& directory, WatchType type, const std::vector<std::string>& extensions)
        {
            WatchDirectory l_Watch{};
            l_Watch.Directory = directory;
            l_Watch.Type = type;
            l_Watch.Extensions.reserve(extensions.size());

            for (const std::string& it_Extension : extensions)
            {
                l_Watch.Extensions.push_back(ToLower(it_Extension));
            }

            m_Watches.push_back(std::move(l_Watch));
        }

        void FileWatcher::Poll()
        {
            for (WatchDirectory& it_Watch : m_Watches)
            {
                ScanDirectory(it_Watch);
            }
        }

        std::optional<FileWatcher::ReloadEvent> FileWatcher::PopPendingEvent()
        {
            if (m_PendingQueue.empty())
            {
                return std::nullopt;
            }

            uint64_t l_Id = m_PendingQueue.front();
            m_PendingQueue.pop_front();

            auto l_It = m_EventLookup.find(l_Id);
            if (l_It == m_EventLookup.end())
            {
                return std::nullopt;
            }

            return m_Events[l_It->second];
        }

        void FileWatcher::QueueEvent(uint64_t eventId)
        {
            auto l_It = m_EventLookup.find(eventId);
            if (l_It == m_EventLookup.end())
            {
                TR_CORE_WARN("Requested reload for unknown event id {}", eventId);
                return;
            }

            ReloadEvent& l_Event = m_Events[l_It->second];
            if (l_Event.Status == ReloadStatus::Queued)
            {
                return;
            }

            TransitionEvent(eventId, ReloadStatus::Queued);
            m_PendingQueue.push_back(eventId);
        }

        void FileWatcher::MarkEventSuccess(uint64_t eventId, const std::string& message)
        {
            TransitionEvent(eventId, ReloadStatus::Success, message);
        }

        void FileWatcher::MarkEventFailure(uint64_t eventId, const std::string& message)
        {
            TransitionEvent(eventId, ReloadStatus::Failed, message);
        }

        void FileWatcher::ScanDirectory(WatchDirectory& watch)
        {
            std::error_code l_Error{};
            if (!std::filesystem::exists(watch.Directory, l_Error))
            {
                if (!watch.ReportedMissing)
                {
                    TR_CORE_WARN("Watch directory '{}' not found", watch.Directory.string());
                    watch.ReportedMissing = true;
                }

                return;
            }

            if (watch.ReportedMissing)
            {
                TR_CORE_INFO("Watch directory '{}' is now available", watch.Directory.string());
                watch.ReportedMissing = false;
            }

            std::unordered_set<std::string> l_Seen{};

            std::filesystem::directory_options l_Options = std::filesystem::directory_options::skip_permission_denied;
            for (const std::filesystem::directory_entry& it_Entry : std::filesystem::recursive_directory_iterator(watch.Directory, l_Options, l_Error))
            {
                if (l_Error)
                {
                    TR_CORE_WARN("Failed to iterate '{}' (error: {})", watch.Directory.string(), l_Error.message());
                    l_Error.clear();
                    continue;
                }

                if (!it_Entry.is_regular_file(l_Error))
                {
                    continue;
                }

                const std::filesystem::path& l_Path = it_Entry.path();
                if (!ShouldTrackFile(watch, l_Path))
                {
                    continue;
                }

                std::string l_Key = Utilities::FileManagement::NormalizePath(l_Path.string());
                auto l_WriteTime = std::filesystem::last_write_time(l_Path, l_Error);
                if (l_Error)
                {
                    TR_CORE_WARN("Failed to query timestamp for '{}' (error: {})", l_Key, l_Error.message());
                    l_Error.clear();
                    continue;
                }

                l_Seen.insert(l_Key);

                auto l_Known = watch.KnownFiles.find(l_Key);
                if (l_Known == watch.KnownFiles.end())
                {
                    watch.KnownFiles.insert({ l_Key, l_WriteTime });
                    ReloadEvent& l_Event = CreateEvent(l_Key, watch.Type, l_WriteTime);
                    TR_CORE_INFO("Detected new file '{}' for hot reload", l_Key);

                    if (m_AutoReload)
                    {
                        QueueEvent(l_Event.Id);
                    }

                    continue;
                }

                if (l_Known->second != l_WriteTime)
                {
                    l_Known->second = l_WriteTime;
                    ReloadEvent& l_Event = CreateEvent(l_Key, watch.Type, l_WriteTime);
                    TR_CORE_INFO("Detected modification for '{}'", l_Key);

                    if (m_AutoReload)
                    {
                        QueueEvent(l_Event.Id);
                    }
                }
            }

            for (auto l_It = watch.KnownFiles.begin(); l_It != watch.KnownFiles.end();)
            {
                if (!l_Seen.contains(l_It->first))
                {
                    TR_CORE_WARN("Previously tracked file '{}' disappeared from '{}'", l_It->first, watch.Directory.string());
                    l_It = watch.KnownFiles.erase(l_It);
                }
                else
                {
                    ++l_It;
                }
            }
        }

        bool FileWatcher::ShouldTrackFile(const WatchDirectory& watch, const std::filesystem::path& path) const
        {
            if (watch.Extensions.empty())
            {
                return true;
            }

            std::string l_Extension = ToLower(path.extension().string());
            for (const std::string& it_Extension : watch.Extensions)
            {
                if (l_Extension == it_Extension)
                {
                    return true;
                }
            }

            return false;
        }

        FileWatcher::ReloadEvent& FileWatcher::CreateEvent(const std::string& path, WatchType type, std::filesystem::file_time_type timestamp)
        {
            ReloadEvent l_Event{};
            l_Event.Id = m_NextEventId++;
            l_Event.Type = type;
            l_Event.Path = path;
            l_Event.Timestamp = timestamp;

            m_Events.push_back(l_Event);
            m_EventLookup.insert({ l_Event.Id, m_Events.size() - 1 });

            return m_Events.back();
        }

        void FileWatcher::TransitionEvent(uint64_t eventId, ReloadStatus status, const std::string& message)
        {
            auto l_It = m_EventLookup.find(eventId);
            if (l_It == m_EventLookup.end())
            {
                return;
            }

            ReloadEvent& l_Event = m_Events[l_It->second];
            l_Event.Status = status;
            l_Event.Message = message;
        }

        //------------------------------------------------------------------------------------------------------------------------------------------------------//

        std::vector<char> FileManagement::ReadFile(const std::string& filePath)
        {
            std::ifstream l_File(filePath, std::ios::ate | std::ios::binary);

            if (!l_File.is_open())
            {
                TR_CORE_CRITICAL("Failed to open file: {}", filePath);

                return {};
            }

            size_t l_Size = (size_t)l_File.tellg();
            std::vector<char> l_Buffer(l_Size);

            l_File.seekg(0);
            l_File.read(l_Buffer.data(), l_Size);
            l_File.close();

            return l_Buffer;
        }

        std::string FileManagement::NormalizePath(const std::string& path)
        {
            std::filesystem::path l_Path(path);
            return l_Path.lexically_normal().generic_string();
        }

        std::string FileManagement::GetBaseDirectory(const std::string& filePath)
        {
            std::filesystem::path l_Path(filePath);
            return l_Path.has_parent_path() ? l_Path.parent_path().string() : "";
        }

        std::string FileManagement::GetExtension(const std::string& filePath)
        {
            std::filesystem::path l_Path(filePath);
            return l_Path.has_extension() ? l_Path.extension().string() : "";
        }

        std::string FileManagement::JoinPath(const std::string& base, const std::string& addition)
        {
            std::filesystem::path l_Path = std::filesystem::path(base) / addition;
            return NormalizePath(l_Path.string());
        }

        //------------------------------------------------------------------------------------------------------------------------------------------------------//

        double Time::s_LastTime = 0.0;
        float Time::s_DeltaTime = 0.0f;

        void Time::Init()
        {
            glfwSetTime(0.0);

            s_LastTime = 0.0;
            s_DeltaTime = 0.0f;
        }

        void Time::Update()
        {
            double current = glfwGetTime();

            s_DeltaTime = static_cast<float>(current - s_LastTime);
            s_LastTime = current;
        }

        //------------------------------------------------------------------------------------------------------------------------------------------------------//

        static std::atomic_size_t s_FrameCount{ 0 };

        void Allocation::ResetFrame()
        {
            s_FrameCount.store(0, std::memory_order_relaxed);
        }

        void Allocation::Increment()
        {
            s_FrameCount.fetch_add(1, std::memory_order_relaxed);
        }

        size_t Allocation::GetFrameCount()
        {
            return s_FrameCount.load(std::memory_order_relaxed);
        }

        void* Allocation::Malloc(std::size_t size, const char* /*file*/, int /*line*/)
        {
            Increment();

            return std::malloc(size);
        }
    }
}

void* operator new(std::size_t size, const char* /*file*/, int /*line*/)
{
	Trident::Utilities::Allocation::Increment();

	return ::operator new(size);
}

void operator delete(void* ptr, const char* /*file*/, int /*line*/) noexcept
{
	::operator delete(ptr);
}

void* operator new(std::size_t size)
{
	Trident::Utilities::Allocation::Increment();

	if (void* ptr = std::malloc(size))
	{
		return ptr;
	}
	
	throw std::bad_alloc();
}

void operator delete(void* ptr) noexcept
{
	std::free(ptr);
}

void* operator new[](std::size_t size)
{
	Trident::Utilities::Allocation::Increment();
	if (void* ptr = std::malloc(size))
	{
		return ptr;
	}

	throw std::bad_alloc();
}

void operator delete[](void* ptr) noexcept
{
	std::free(ptr);
}