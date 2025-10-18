#pragma once

#include "Renderer/Camera/Camera.h"

#include "ECS/Components/CameraComponent.h"
#include "ECS/Components/TransformComponent.h"
#include "ECS/Registry.h"

namespace Trident
{
    namespace ECS { class Registry; }

    /**
     * @brief Camera implementation backed by ECS components.
     *
     * The runtime camera keeps the view/projection caches synchronised with
     * the owning entity so gameplay systems can update the transform or camera
     * component without poking renderer internals. Whenever the component data
     * changes the cached matrices are rebuilt on demand.
     */
    class RuntimeCamera final : public Camera
    {
    public:
        RuntimeCamera() = default;
        RuntimeCamera(ECS::Registry& registry, ECS::Entity entity);

        void SetRegistry(ECS::Registry* registry);
        void SetEntity(ECS::Entity entity);

        const glm::mat4& GetViewMatrix() const override;
        const glm::mat4& GetProjectionMatrix() const override;
        glm::vec3 GetPosition() const override;
        glm::vec3 GetRotation() const override;

        void SetPosition(const glm::vec3& position) override;
        void SetRotation(const glm::vec3& eulerDegrees) override;

        void SetProjectionType(ProjectionType type) override;
        ProjectionType GetProjectionType() const override;

        void SetFieldOfView(float fieldOfViewDegrees) override;
        float GetFieldOfView() const override;

        void SetOrthographicSize(float size) override;
        float GetOrthographicSize() const override;

        void SetClipPlanes(float nearClip, float farClip) override;
        float GetNearClip() const override;
        float GetFarClip() const override;

        void SetViewportSize(const glm::vec2& viewportSize) override;
        glm::vec2 GetViewportSize() const override { return m_ViewportSize; }

        void Invalidate() override;

    private:
        CameraComponent* GetCameraComponent();
        const CameraComponent* GetCameraComponent() const;
        Transform* GetTransformComponent();
        const Transform* GetTransformComponent() const;
        void UpdateViewMatrix() const;
        void UpdateProjectionMatrix() const;

    private:
        ECS::Registry* m_Registry{ nullptr };   ///< Registry used to resolve camera + transform components.
        ECS::Entity m_Entity{ 0 };               ///< Owning entity for the runtime camera.
        glm::vec2 m_ViewportSize{ 1280.0f, 720.0f }; ///< Cached viewport size for aspect ratio calculations.

        mutable glm::mat4 m_ViewMatrix{ 1.0f };       ///< Cached view matrix rebuilt when transform changes.
        mutable glm::mat4 m_ProjectionMatrix{ 1.0f }; ///< Cached projection matrix rebuilt when component changes.
        mutable bool m_ViewDirty{ true };             ///< Marks the view cache as out of date.
        mutable bool m_ProjectionDirty{ true };       ///< Marks the projection cache as out of date.
    };
}