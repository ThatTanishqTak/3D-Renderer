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

		m_RenderCommand.RenderUI(); // UI before 3D (ofcourse)

		BeginMode3D(m_Camera.GetCamera());

		m_RenderCommand.Render(); // 3D models (right now just a generic red cube )

		EndMode3D();
		EndDrawing();
	}
}