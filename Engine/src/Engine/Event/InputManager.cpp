#include "InputManager.h"

#include <raylib.h>
#include <iostream>

namespace Engine
{
	void InputManager::Update()
	{
		if (IsKeyPressed(KEY_ESCAPE))
		{
			m_Window.m_IsRunning = false;
		}
	}
}