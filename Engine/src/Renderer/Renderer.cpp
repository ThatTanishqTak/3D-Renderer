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
		BeginDrawing();
		ClearBackground(BLACK);
		BeginMode3D(m_Camera.GetCamera());

		m_RenderCommand.Render();

		EndMode3D();
		EndDrawing();
	}
}