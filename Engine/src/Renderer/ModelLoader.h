#pragma once

#include <raylib.h>

#include <string>
#include <filesystem>

namespace Engine
{
	class ModelLoader
	{
	public:
		void Load();
		void Unload();

		void GetFilePath();

	private:
		std::filesystem::path m_FilePath;
	};
}