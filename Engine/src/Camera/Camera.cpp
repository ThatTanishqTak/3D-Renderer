#include "Camera.h"

namespace Engine
{
	void Camera::Init()
	{
		m_MoveSpeed = 10.0f;

		m_Camera.position = { 10.0f, 10.0f, 10.0f };
		m_Camera.target = { 0.0f, 0.0f, 0.0f };
		m_Camera.up = { 0.0f, 1.0f, 0.0f };
		m_Camera.fovy = 45.0f;
		m_Camera.projection = CAMERA_PERSPECTIVE;
	}

	void Camera::Update()
	{
		CameraControl();
	}

	void Camera::CameraControl()
	{
		if (IsKeyDown(KEY_W)) { m_Camera.position.z += m_MoveSpeed * GetFrameTime(); }
		if (IsKeyDown(KEY_S)) { m_Camera.position.z -= m_MoveSpeed * GetFrameTime(); }
		if (IsKeyDown(KEY_A)) { m_Camera.position.x -= m_MoveSpeed * GetFrameTime(); }
		if (IsKeyDown(KEY_D)) { m_Camera.position.x += m_MoveSpeed * GetFrameTime(); }
		if (IsKeyDown(KEY_E)) { m_Camera.position.y += m_MoveSpeed * GetFrameTime(); }
		if (IsKeyDown(KEY_Q)) { m_Camera.position.y -= m_MoveSpeed * GetFrameTime(); }
	}
}