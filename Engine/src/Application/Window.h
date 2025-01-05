#pragma once

#include <string>
#include <raylib.h>

#include "Specification.h"

namespace Engine
{
	class Window
	{
	public:
		Window();

		void Init();
		void Shutdown();

		void Update();

	public:
		int m_WindowWidth;
		int m_WindowHeight;

		bool m_IsRunning;

	private:
		ApplicationSpecification m_ApplicationSpecs;

		std::string m_Title;

		Image m_Icon;
	};
}