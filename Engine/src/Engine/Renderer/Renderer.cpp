#include "Renderer.h"

#include <raylib.h>

namespace Engine
{
	static Camera3D GetCamera()
	{
		Camera3D camera;

		camera = { 0 };

		camera.position = { 10.0f, 10.0f, 10.0f };  // Camera position
		camera.target = { 0.0f, 0.0f, 0.0f };       // Camera looking at point
		camera.up = { 0.0f, 1.0f, 0.0f };           // Camera up vector (rotation towards target)
		camera.fovy = 45.0f;                        // Camera field-of-view Y
		camera.projection = CAMERA_PERSPECTIVE;     // Camera projection type

		return camera;
	}

	void Renderer::Draw()
	{
		BeginDrawing();
		ClearBackground(BLACK);
		BeginMode3D(GetCamera());

		// Draw calls
		m_RenderCommands.Draw();

		EndMode3D();
		EndDrawing();
	}
}