#pragma once

#include <string>
#include <glm/glm.hpp>

namespace Trident
{
    // Defines the type of projection the renderer should create for this camera.
    enum class ProjectionType
    {
        Perspective,
        Orthographic
    };

    struct CameraComponent
    {
        // Human-readable identifier surfaced in editor combo boxes.
        std::string Name{ "Camera" };
        // Projection properties pulled by the renderer whenever this camera drives a viewport.
        float FieldOfView = 45.0f;
        float NearClip = 0.1f;
        float FarClip = 100.0f;
        // Choose between perspective and orthographic projections.
        ProjectionType Projection = ProjectionType::Perspective;
        // Controls the size of the vertical frustum for orthographic cameras. Width is derived from the aspect ratio.
        float OrthographicSize = 10.0f;
        // Allow designers to pin a specific aspect ratio regardless of the viewport dimensions.
        bool OverrideAspectRatio = false;
        float AspectRatio = 16.0f / 9.0f;
        // Advanced hook so tools or scripts can supply a fully custom projection matrix.
        bool UseCustomProjection = false;
        glm::mat4 CustomProjection{ 1.0f };
    };
}