#pragma once

#include <raylib.h>
#include <raygui-cpp.h>

namespace Engine
{
	class ModelLoader
	{
	public:
		void Init();
		void SHutdown();

		void Update();

	private:
		void Load();
		void Unload();
		void OpenFileDialog();
	};
}