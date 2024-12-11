#include <raylib.h>

#include "Renderer.h"

namespace Engine
{
	void Renderer::Draw()
	{
		BeginDrawing();
		ClearBackground(RED);

		// Draw calls
		// render.OnDraw();

		EndDrawing();
	}
}