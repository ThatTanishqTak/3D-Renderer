#include "RenderCommands.h"

#include <raylib.h>

namespace Engine
{
	Vector3 cubePosition = { 0.0f, 0.0f, 0.0f };

	void RenderCommands::Draw()
	{
		DrawCube(cubePosition, 2.0f, 2.0f, 2.0f, RED);
		DrawCubeWires(cubePosition, 2.0f, 2.0f, 2.0f, MAROON);

		DrawGrid(10, 1.0f);
	}
}