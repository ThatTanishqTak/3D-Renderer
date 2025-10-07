#pragma once

#include <string>

namespace UI
{
	class ContentBrowserPanel
	{
	public:
		ContentBrowserPanel();
		~ContentBrowserPanel();

		void Render();

	private:
		std::string m_Path;
	};
}