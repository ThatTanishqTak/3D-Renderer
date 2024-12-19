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
		m_RenderCommands.ModelLoading("../../../../Client/Models/scene.gltf");
		m_RenderCommands.TextureLoading("../../../../Client/Models/textures/color1_o_baseColor.png");
		m_Interface.Init();
	}

	void Renderer::Shutdown()
	{
		m_RenderCommands.ModelUnloading();
		m_RenderCommands.TextureUnloading();
		m_Interface.Shutdown();
	}

	void Renderer::Update()
	{
		ClearBackground(BLACK);
		BeginDrawing();
		{
			m_Interface.Update();

			BeginMode3D(GetCamera());
			{
				m_RenderCommands.DrawGame();
				m_RenderCommands.ModelDrawing();
			
				EndMode3D();
			}
			EndDrawing();
		}
	}
}