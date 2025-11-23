#pragma once

#include "Renderer/Camera/Camera.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Trident
{
    /**
     * @brief Editor-oriented camera supporting orbit and fly navigation.
     *
     * The editor camera stores its own transform data and exposes simple
     * setters so panels can hook it into gizmo or viewport interactions. It
     * maintains cached view and projection matrices to minimise recomputation,
     * invalidating them whenever a property changes. Future revisions can add
     * input-driven damping or acceleration to smooth the motion.
     */
    class EditorCamera final : public Camera
    {
    public:
        EditorCamera();

        const glm::mat4& GetViewMatrix() const override;
        const glm::mat4& GetProjectionMatrix() const override;
        glm::vec3 GetPosition() const override;
        glm::vec3 GetRotation() const override;

        void SetPosition(const glm::vec3& position) override;
        void SetRotation(const glm::vec3& eulerDegrees) override;

        void SetProjectionType(ProjectionType type) override;
        ProjectionType GetProjectionType() const override { return m_ProjectionType; }

        void SetFieldOfView(float fieldOfViewDegrees) override;
        float GetFieldOfView() const override { return m_FieldOfView; }

        void SetOrthographicSize(float size) override;
        float GetOrthographicSize() const override { return m_OrthographicSize; }

        void SetClipPlanes(float nearClip, float farClip) override;
        float GetNearClip() const override { return m_NearClip; }
        float GetFarClip() const override { return m_FarClip; }

        void SetViewportSize(const glm::vec2& viewportSize) override;
        glm::vec2 GetViewportSize() const override { return m_ViewportSize; }

        void Invalidate() override;

        /// Returns a forward direction vector derived from the current rotation.
        glm::vec3 GetForwardDirection() const;
        /// Returns a right direction vector derived from the current rotation.
        glm::vec3 GetRightDirection() const;
        /// Returns an up direction vector derived from the current rotation.
        glm::vec3 GetUpDirection() const;
        /// Builds and returns the orientation quaternion for the current rotation.
        glm::quat GetOrientation() const;

        /// Convenience accessor returning the combined view-projection matrix.
        glm::mat4 GetViewProjectionMatrix() const { return GetProjectionMatrix() * GetViewMatrix(); }

    private:
        void UpdateViewMatrix() const;
        void UpdateProjectionMatrix() const;
        glm::quat BuildOrientation() const;

    private:
        glm::vec3 m_Position{ 0.0f, 0.0f, 5.0f };   ///< Camera location used to seed the view matrix.
        glm::vec3 m_Rotation{ 0.0f };                ///< Euler rotation in degrees for simple gizmo integration.
        glm::vec2 m_ViewportSize{ 1280.0f, 720.0f }; ///< Backing viewport dimensions for aspect ratio calculations.
        float m_FieldOfView{ 60.0f };                ///< Perspective vertical field of view in degrees.
        float m_OrthographicSize{ 20.0f };           ///< Height of the orthographic frustum in world units.
        float m_NearClip{ 0.1f };                    ///< Near clipping plane distance.
        float m_FarClip{ 1000.0f };                  ///< Far clipping plane distance.
        ProjectionType m_ProjectionType{ ProjectionType::Perspective }; ///< Active projection mode.

        mutable glm::mat4 m_ViewMatrix{ 1.0f };          ///< Cached view matrix rebuilt on demand.
        mutable glm::mat4 m_ProjectionMatrix{ 1.0f };    ///< Cached projection matrix rebuilt when configuration changes.
        mutable bool m_ViewDirty{ true };                ///< Flag telling GetViewMatrix() to rebuild the cache.
        mutable bool m_ProjectionDirty{ true };          ///< Flag telling GetProjectionMatrix() to rebuild the cache.
    };
}