#pragma once

#include <string>

namespace Engine
{
	class Window
	{
	public:
		Window();

		void Init();
		void Shutdown();

		void Run();

	private:
		int m_WindowWidth;
		int m_WindowHeight;
		std::string m_Title;
	};
}