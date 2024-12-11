#include "InputManager.h"

#include <raylib.h>
#include <iostream>

namespace Engine
{
	void InputManager::OnUpdate()
	{
		if (int keyCode = GetKeyPressed())
		{
			std::cout << keyCode << std::endl;
		}
	}
}