#include "Utilities.h"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace Trident
{
	namespace Utilities
	{
		std::shared_ptr<spdlog::logger> Log::s_CoreLogger;
		std::shared_ptr<spdlog::logger> Log::s_ClientLogger;

		void Log::Init()
		{
			std::vector<spdlog::sink_ptr> logSinks;
			logSinks.emplace_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
			logSinks.emplace_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>("Trident.log", true));

			logSinks[0]->set_pattern("%^[%T] %n: %v%$");
			logSinks[1]->set_pattern("[%T] [%l] %n: %v");

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