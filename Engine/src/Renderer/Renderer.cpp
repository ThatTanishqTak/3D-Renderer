#include "Renderer.h"

namespace Engine
{
	void Renderer::Init()
	{
		m_Camera.Init();
		m_RenderCommand.Init();
	}

	void Renderer::Shutdown()
	{
		m_RenderCommand.Shutdown();
	}

	void Renderer::Update()
	{
		// Update
		m_Camera.Update();
	}

	void Renderer::Render()
	{
		BeginDrawing();
		ClearBackground(BLACK);

		BeginMode3D(m_Camera.GetCamera());

		m_RenderCommand.RenderGrid();
		m_RenderCommand.RenderScene(); // 3D models (right now just a generic red cube )

		EndMode3D();

		m_RenderCommand.RenderUI(); // UI after 3D (so that it overlaps the scene)

		EndDrawing();
	}
}