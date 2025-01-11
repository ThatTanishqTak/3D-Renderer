#pragma once

#include <string>
#include <raylib.h>

#include "WindowSpecification.h"

namespace Engine
{
	class Window
	{
	public:
		void Init();
		void Shutdown();

		void Update();

	public:
		int m_WindowWidth;
		int m_WindowHeight;

		bool m_IsRunning;

	private:
		WindowSpecification m_WindowSpecs;

		std::string m_Title;

		Image m_Icon;
	};
}