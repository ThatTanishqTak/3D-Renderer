#include "Camera.h"

namespace Engine
{
	void Camera::Init()
	{
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
		UpdateCameraPro(&m_Camera,
			// Movement key and speed
		{ 
		    IsKeyDown(KEY_W) * 0.1f - IsKeyDown(KEY_S) * 0.1f,
		    IsKeyDown(KEY_D) * 0.1f - IsKeyDown(KEY_A) * 0.1f,
		    IsKeyDown(KEY_E) * 0.1f - IsKeyDown(KEY_Q) * 0.1f
		},
			// Camera rotation 
		{
			GetMouseDelta().x * 0.05f,
		    GetMouseDelta().y * 0.05f,                              
		    0.0f
		},
			// Camera zoom
		{
		   -GetMouseWheelMove() * 2.0f
		});
	}
}