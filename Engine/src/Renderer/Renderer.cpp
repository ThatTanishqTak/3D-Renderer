#include "Renderer.h"

namespace Engine
{
	void Renderer::Init()
	{
		camera.position = { 10.0f, 10.0f, 10.0f };
		camera.target = { 0.0f, 0.0f, 0.0f };
		camera.up = { 0.0f, 1.0f, 0.0f };
		camera.fovy = 45.0f;
		camera.projection = CAMERA_PERSPECTIVE;
	}

	void Renderer::Shutdown()
	{

	}

	void Renderer::Update()
	{
		BeginDrawing();
		ClearBackground(BLACK);
		BeginMode3D(camera);

		DrawGrid(100, 1.0f);

		EndMode3D();
		EndDrawing();
	}
}