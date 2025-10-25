#pragma once

#include "Renderer/Camera/Camera.h"

namespace Trident
{
    /**
     * @brief Stores camera properties for ECS driven entities.
     *
     * The component is intentionally lightweight and serialisation friendly;
     * future versions can extend it with exposure values, post-processing
     * toggles, or physical camera attributes (lens shift, sensor size, etc.).
     * Once runtime playback returns, these settings will once again feed a
     * dedicated camera path alongside the editor preview.
     */
    struct CameraComponent
    {
        Camera::ProjectionType m_ProjectionType{ Camera::ProjectionType::Perspective }; ///< Projection mode used for rendering.
        float m_FieldOfView{ 60.0f };              ///< Vertical field of view in degrees when perspective is active.
        float m_OrthographicSize{ 20.0f };         ///< Height of the orthographic frustum in world units.
        float m_NearClip{ 0.1f };                  ///< Near clipping plane distance.
        float m_FarClip{ 1000.0f };                ///< Far clipping plane distance.
        bool m_Primary{ false };                   ///< Marks the camera as the primary runtime view when none is specified.
        bool m_FixedAspectRatio{ false };          ///< When true the runtime camera keeps its stored aspect ratio.
        float m_AspectRatio{ 16.0f / 9.0f };       ///< Optional override used when m_FixedAspectRatio is enabled.
    };
}