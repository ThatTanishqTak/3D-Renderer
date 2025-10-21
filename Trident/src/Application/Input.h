#pragma once

#include "Events/KeyCodes.h"
#include "Events/MouseCodes.h"

#include <array>

#include <glm/vec2.hpp>

namespace Trident
{
    /**
     * Centralized keyboard and mouse state tracker that translates raw window
     * callbacks into high-level queries (down/pressed/released/repeat). This keeps
     * polling logic simple for gameplay and editor code while leaving room to
     * extend toward controllers and text input later on.
     */
    class Input final
    {
    public:
        /**
         * Access the global input manager instance.
         */
        static Input& Get();

        Input(const Input&) = delete;
        Input(Input&&) = delete;
        Input& operator=(const Input&) = delete;
        Input& operator=(Input&&) = delete;

        // Query helpers -----------------------------------------------------
        bool IsKeyDown(KeyCode key) const;
        bool IsKeyPressed(KeyCode key) const;
        bool IsKeyReleased(KeyCode key) const;
        bool IsKeyRepeated(KeyCode key) const;

        bool IsMouseButtonDown(MouseCode button) const;
        bool IsMouseButtonPressed(MouseCode button) const;
        bool IsMouseButtonReleased(MouseCode button) const;
        bool WasMouseButtonPressed(MouseCode button) const;
        bool WasMouseButtonReleased(MouseCode button) const;

        glm::vec2 GetMousePosition() const;
        glm::vec2 GetMouseDelta() const;
        glm::vec2 GetScrollDelta() const;
        bool HasMousePosition() const;

        void BeginFrame();
        void EndFrame();

        void SetUICapture(bool wantMouse, bool wantKeyboard);

        // Event hooks -------------------------------------------------------
        void OnKeyPressed(KeyCode key, bool isRepeat);
        void OnKeyReleased(KeyCode key);
        void OnMouseButtonPressed(MouseCode button);
        void OnMouseButtonReleased(MouseCode button);
        void OnMouseMoved(float x, float y);
        void OnMouseScrolled(float xoff, float yoff);

    private:
        Input();

        template<typename CodeType, size_t Count>
        static bool IsCodeValid(CodeType code)
        {
            const size_t l_Index = static_cast<size_t>(code);
            return l_Index < Count;
        }

        static constexpr size_t s_MaxKeys = 512;           ///< Mirrors GLFW's key range with slack for future bindings.
        static constexpr size_t s_MaxMouseButtons = 8;     ///< Covers primary/extra mouse buttons.

        std::array<bool, s_MaxKeys> m_CurrentKeyState{};
        std::array<bool, s_MaxKeys> m_PreviousKeyState{};
        std::array<bool, s_MaxKeys> m_KeyPressed{};
        std::array<bool, s_MaxKeys> m_KeyReleased{};
        std::array<bool, s_MaxKeys> m_KeyRepeated{};

        std::array<bool, s_MaxMouseButtons> m_CurrentMouseState{};
        std::array<bool, s_MaxMouseButtons> m_PreviousMouseState{};
        std::array<bool, s_MaxMouseButtons> m_MousePressed{};
        std::array<bool, s_MaxMouseButtons> m_MouseReleased{};

        glm::vec2 m_CurrentMousePosition{ 0.0f, 0.0f };
        glm::vec2 m_PreviousMousePosition{ 0.0f, 0.0f };
        glm::vec2 m_MouseDelta{ 0.0f, 0.0f };
        glm::vec2 m_ScrollDelta{ 0.0f, 0.0f };

        bool m_HasMousePosition = false;
        bool m_FrameActive = false;
        bool m_WantCaptureMouse = false;
        bool m_WantCaptureKeyboard = false;
    };
}