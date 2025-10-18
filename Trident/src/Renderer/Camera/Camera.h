#pragma once

#include <glm/glm.hpp>

namespace Trident
{
    /**
     * @brief Abstract camera interface describing common rendering controls.
     *
     * Rendering subsystems interact with cameras purely through this contract
     * so editor and runtime implementations can evolve independently. Each
     * accessor returns cached data where possible to avoid redundant
     * recalculations. Future revisions can extend the interface with exposure
     * controls or jitter injection for temporal anti-aliasing.
     */
    class Camera
    {
    public:
        /// Enumerates the projection modes supported by the renderer.
        enum class ProjectionType
        {
            Perspective = 0,
            Orthographic,
        };

        virtual ~Camera() = default;

        /// Returns the cached view matrix describing the camera orientation.
        virtual const glm::mat4& GetViewMatrix() const = 0;
        /// Returns the cached projection matrix based on the configured frustum.
        virtual const glm::mat4& GetProjectionMatrix() const = 0;
        /// Provides the world-space position for lighting calculations.
        virtual glm::vec3 GetPosition() const = 0;
        /// Provides the current rotation in Euler angles so tools can present it.
        virtual glm::vec3 GetRotation() const = 0;

        /// Updates the world position and invalidates the cached view matrix.
        virtual void SetPosition(const glm::vec3& position) = 0;
        /// Updates the camera rotation (degrees) and invalidates the view cache.
        virtual void SetRotation(const glm::vec3& eulerDegrees) = 0;

        /// Chooses between perspective and orthographic projection modes.
        virtual void SetProjectionType(ProjectionType type) = 0;
        /// Returns the active projection mode.
        virtual ProjectionType GetProjectionType() const = 0;

        /// Adjusts the vertical field of view in degrees (perspective only).
        virtual void SetFieldOfView(float fieldOfViewDegrees) = 0;
        /// Returns the vertical field of view in degrees.
        virtual float GetFieldOfView() const = 0;

        /// Sets the visible height in world units when using orthographic mode.
        virtual void SetOrthographicSize(float size) = 0;
        /// Returns the orthographic height in world units.
        virtual float GetOrthographicSize() const = 0;

        /// Configures the near/far clipping planes.
        virtual void SetClipPlanes(float nearClip, float farClip) = 0;
        /// Returns the currently configured near clip distance.
        virtual float GetNearClip() const = 0;
        /// Returns the currently configured far clip distance.
        virtual float GetFarClip() const = 0;

        /// Updates the target viewport dimensions used to build the projection matrix.
        virtual void SetViewportSize(const glm::vec2& viewportSize) = 0;
        /// Returns the cached viewport dimensions.
        virtual glm::vec2 GetViewportSize() const = 0;

        /// Forces derived classes to refresh any cached data on demand.
        virtual void Invalidate() = 0;
    };
}