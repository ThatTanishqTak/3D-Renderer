#pragma once

#include <string>
#include <raylib.h>

namespace Engine
{
	class Window
	{
	public:
		Window();

		void Init();
		void Shutdown();

	private:
		int m_WindowWidth;
		int m_WindowHeight;
		std::string m_Title;

		Image m_Icon;
	};
}