#pragma once

#include <raylib.h>

namespace Engine
{
	class ModelLoader
	{
	public:
		void Init();
		void SHutdown();

		void Update();
		void OpenFileDialog();

	private:
		void Load();
		void Unload();
	};
}