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
		UpdateCamera(&m_Camera, CAMERA_FIRST_PERSON); // This feels like cheating but whatever.... I don't give a fuck at this point
	}
}