#pragma once

#include <raylib.h>

namespace Engine
{
	class Camera
	{
	public:
		void Init();
		void Update();

		Camera3D GetCamera() { return m_Camera; }

	private:
		void CameraControl();

	private:
		Camera3D m_Camera;
	};
}