#pragma once

#include <string>

namespace Trident
{
    struct CameraComponent
    {
        // Human-readable identifier surfaced in editor combo boxes.
        std::string Name{ "Camera" };
        // Projection properties pulled by the renderer whenever this camera drives a viewport.
        float FieldOfView = 45.0f;
        float NearClip = 0.1f;
        float FarClip = 100.0f;
    };
}