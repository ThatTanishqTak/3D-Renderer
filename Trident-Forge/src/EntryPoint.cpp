// Application entry point for Trident-Forge

#include "Application.h"
#include "ApplicationLayer.h"

#include <memory>

int main()
{
    try
    {
        auto a_ApplicationLayer = std::make_unique<ApplicationLayer>();
        // TODO: Support stacking multiple layers or toggling editor/runtime selections here.

        Trident::Application l_App(std::move(a_ApplicationLayer));

        l_App.Run();
    }

    catch (const std::exception& e)
    {
        // Log any exception and report GLFW errors
        TR_CRITICAL("[Fatal] {}", e.what());

        const char* l_Description = nullptr;
        int l_Code = glfwGetError(&l_Description);

        if (l_Description)
        {
            TR_CRITICAL("[GLFW error {}] {}", l_Code, l_Description);
        }

        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}