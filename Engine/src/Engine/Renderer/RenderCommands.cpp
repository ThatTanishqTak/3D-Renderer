#include "RenderCommands.h"

#include <string>
#include <filesystem>
#include <raylib.h>

namespace Engine
{
	void RenderCommands::DrawGame()
	{
		DrawGrid(1000, 1.0f);
	}

}