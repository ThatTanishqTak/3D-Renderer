#pragma once

#include "Renderer/Camera/Camera.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Trident
{
    /**
     * @brief Editor-oriented camera supporting orbit and fly navigation.
     */
    class EditorCamera final : public Camera
    {
    public:
        EditorCamera();

        const glm::mat4& GetViewMatrix() const override;
        const glm::mat4& GetProjectionMatrix() const override;

        glm::vec3 GetPosition() const override { return m_Position; }
        glm::vec3 GetRotation() const override { return m_Rotation; }

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

        // Optimized direction getters using cached orientation
        glm::vec3 GetForwardDirection() const;
        glm::vec3 GetRightDirection() const;
        glm::vec3 GetUpDirection() const;
        glm::quat GetOrientation() const { return m_Orientation; }

        glm::mat4 GetViewProjectionMatrix() const { return m_ProjectionMatrix * m_ViewMatrix; }

    private:
        void RecalculateViewMatrix();
        void RecalculateProjectionMatrix();
        void RecalculateOrientation();

    private:
        glm::vec3 m_Position{ 0.0f, 0.0f, 5.0f };
        glm::vec3 m_Rotation{ 0.0f };
        glm::quat m_Orientation{ 1.0f, 0.0f, 0.0f, 0.0f }; // Cached orientation

        glm::vec2 m_ViewportSize{ 1280.0f, 720.0f };
        float m_FieldOfView{ 60.0f };
        float m_OrthographicSize{ 20.0f };
        float m_NearClip{ 0.1f };
        float m_FarClip{ 1000.0f };
        ProjectionType m_ProjectionType{ ProjectionType::Perspective };

        glm::mat4 m_ViewMatrix{ 1.0f };
        glm::mat4 m_ProjectionMatrix{ 1.0f };
    };
}