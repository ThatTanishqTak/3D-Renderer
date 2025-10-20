#pragma once

#include "Events/KeyCodes.h"
#include "Events/MouseCodes.h"

#include <array>

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

        // Event hooks -------------------------------------------------------
        void OnKeyPressed(KeyCode key, bool isRepeat);
        void OnKeyReleased(KeyCode key);
        void OnMouseButtonPressed(MouseCode button);
        void OnMouseButtonReleased(MouseCode button);

        /**
         * Reset one-shot edges (pressed, released, repeated) at the end of a
         * frame once consumers have observed them. This ensures short-lived
         * transitions remain accurate without losing the long-lived down state.
         */
        void EndFrame();

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
    };
}