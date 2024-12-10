#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>

#include "Window.h"
#include "Specification.h"
#include "Renderer.h"

namespace Engine
{
	GLFWwindow* window;
	Renderer renderer;

	Window::Window() : m_WindowWidth(specs.Width), m_WindowHeight(specs.Height), m_Title(specs.Title)
	{

	}

	void Window::Init()
	{
		glfwInit();
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

		window = glfwCreateWindow(m_WindowWidth, m_WindowHeight, m_Title.c_str(), NULL, NULL);

		if (!window)
		{
			std::cout << "Failed to create GLFW window" << std::endl;

			glfwTerminate();
		}
		glfwMakeContextCurrent(window);

		if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
		{
			std::cout << "Failed to load GLAD" << std::endl;

			glfwTerminate();
		}

		glViewport(0, 0, m_WindowWidth, m_WindowHeight);
	}

	void Window::Shutdown()
	{
		glfwTerminate();
	}

	void Window::Run()
	{
		while (!glfwWindowShouldClose(window))
		{
			glClearColor(0.8f, 0.3f, 0.3f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);

			glfwSwapBuffers(window);

			renderer.Draw();

			glfwPollEvents();
		}
	}
}