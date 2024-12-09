#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>

#include <Window.h>

namespace Engine
{
	GLFWwindow* window;

	void Window::Run()
	{
		while (!glfwWindowShouldClose(window))
		{
			glfwSwapBuffers(window);

			glfwPollEvents();
		}
	}

	void Window::Init()
	{
		glfwInit();
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		
		window = glfwCreateWindow(1920, 1080, "3D Renderer", NULL, NULL);

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

		glViewport(0, 0, 1920, 1080);
	}

	void Window::Shutdown()
	{
		glfwTerminate();
	}
}