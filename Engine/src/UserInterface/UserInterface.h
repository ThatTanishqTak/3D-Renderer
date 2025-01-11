#pragma once

#include "../Renderer/ModelSpecification.h"

namespace Engine
{
	class UserInterface
	{
	public:
		void Init();
		void Shutdown();

		void Update();

	private:
		ModelSpecification m_ModelSpecification;
	};
}