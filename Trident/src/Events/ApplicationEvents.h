#pragma once

#include "Events/Events.h"

#include <sstream>
#include <string>
#include <vector>

namespace Trident
{
	class WindowResizeEvent : public Events
	{
	public:
		WindowResizeEvent(unsigned int width, unsigned int height) : m_Width(width), m_Height(height)
		{

		}

		unsigned int GetWidth() const { return m_Width; }
		unsigned int GetHeight() const { return m_Height; }

		std::string ToString() const override
		{
			std::stringstream ss;
			ss << "WindowResizeEvent: " << m_Width << ", " << m_Height;

			return ss.str();
		}

		EVENT_CLASS_TYPE(WindowResize)
			EVENT_CLASS_CATEGORY(EventCategoryApplication)
	private:
		unsigned int m_Width, m_Height;
	};

	class WindowCloseEvent : public Events
	{
	public:
		WindowCloseEvent() = default;

		EVENT_CLASS_TYPE(WindowClose)
			EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

	class FileDropEvent : public Events
	{
	public:
		explicit FileDropEvent(std::vector<std::string> paths) : m_Paths(std::move(paths))
		{

		}

		const std::vector<std::string>& GetPaths() const { return m_Paths; }

		std::string ToString() const override
		{
			std::stringstream l_Stream;
			l_Stream << "FileDropEvent: ";
			bool l_FirstPath = true;
			for (const std::string& it_Path : m_Paths)
			{
				if (!l_FirstPath)
				{
					l_Stream << ", ";
				}
				l_Stream << it_Path;
				l_FirstPath = false;
			}

			return l_Stream.str();
		}

		EVENT_CLASS_TYPE(FileDrop)
			EVENT_CLASS_CATEGORY(EventCategoryApplication)

	private:
		std::vector<std::string> m_Paths;
	};
}