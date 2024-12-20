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

	void Renderer::Init()
	{
		m_Interface.Init();
	}

	void Renderer::Shutdown()
	{
		m_Interface.Shutdown();
	}

	void Renderer::Render()
	{
		ClearBackground(BLACK);
		BeginDrawing();
		{
			m_Interface.Render();

			BeginMode3D(GetCamera());
		
			m_RenderCommands.DrawGame();

			EndMode3D();
			EndDrawing();
		}
	}
}