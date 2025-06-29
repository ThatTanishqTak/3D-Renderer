#pragma once

#include "Renderer.h"

namespace Trident
{
    class RenderCommand
    {
    public:
        static void Init();
        static void Shutdown();
        static void DrawFrame();
        static void RecreateSwapchain();

        static void SetTransform(const Transform& props);
        static void SetViewport(const ViewportInfo& info);

        static Transform GetTransform();
        static ViewportInfo GetViewport();
        static uint32_t GetCurrentFrame();
    };
}