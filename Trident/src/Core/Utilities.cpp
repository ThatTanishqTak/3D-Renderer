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

		std::vector<char> FileManagement::ReadFile(const std::string& filePath)
		{
			std::ifstream l_File(filePath, std::ios::ate | std::ios::binary);
			
			if (!l_File.is_open())
			{
				TR_CORE_CRITICAL("Failed to open file: " + filePath);
				
				return {};
			}

			size_t l_Size = (size_t)l_File.tellg();
			std::vector<char> l_Buffer(l_Size);
			
			l_File.seekg(0);
			l_File.read(l_Buffer.data(), l_Size);
			l_File.close();
			
			return l_Buffer;
		}

		std::chrono::time_point<std::chrono::high_resolution_clock> Time::s_LastFrameTime;
		float Time::s_DeltaTime = 0.0f;

		void Time::Init()
		{
			s_LastFrameTime = std::chrono::high_resolution_clock::now();
			s_DeltaTime = 0.0f;
		}

		void Time::Update()
		{
			auto a_CurrentTime = std::chrono::high_resolution_clock::now();

			s_DeltaTime = std::chrono::duration<float, std::chrono::seconds::period>(a_CurrentTime - s_LastFrameTime).count();
			s_LastFrameTime = a_CurrentTime;
		}
	}
}