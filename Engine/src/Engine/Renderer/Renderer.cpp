#include <raylib.h>

#include "Renderer.h"

namespace Engine
{
	Camera3D camera;

	static Camera3D GetCamera()
	{
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
		render.Draw();



		if (IsKeyPressed(KEY_W))
		{

		}

		if (IsKeyPressed(KEY_A))
		{

		}

		if (IsKeyPressed(KEY_S))
		{

		}

		if (IsKeyPressed(KEY_D))
		{

		}

		EndMode3D();
		EndDrawing();
	}
}