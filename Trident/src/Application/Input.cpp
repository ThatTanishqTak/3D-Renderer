#include "Application/Input.h"

#include <algorithm>

namespace Trident
{
    Input& Input::Get()
    {
        static Input s_Instance{};
        return s_Instance;
    }

    Input::Input() = default;

    bool Input::IsKeyDown(KeyCode key) const
    {
        if (!IsCodeValid<KeyCode, s_MaxKeys>(key))
        {
            return false;
        }

        return m_CurrentKeyState[static_cast<size_t>(key)];
    }

    bool Input::IsKeyPressed(KeyCode key) const
    {
        if (!IsCodeValid<KeyCode, s_MaxKeys>(key))
        {
            return false;
        }

        return m_KeyPressed[static_cast<size_t>(key)];
    }

    bool Input::IsKeyReleased(KeyCode key) const
    {
        if (!IsCodeValid<KeyCode, s_MaxKeys>(key))
        {
            return false;
        }

        return m_KeyReleased[static_cast<size_t>(key)];
    }

    bool Input::IsKeyRepeated(KeyCode key) const
    {
        if (!IsCodeValid<KeyCode, s_MaxKeys>(key))
        {
            return false;
        }

        return m_KeyRepeated[static_cast<size_t>(key)];
    }

    bool Input::IsMouseButtonDown(MouseCode button) const
    {
        if (!IsCodeValid<MouseCode, s_MaxMouseButtons>(button))
        {
            return false;
        }

        return m_CurrentMouseState[static_cast<size_t>(button)];
    }

    bool Input::IsMouseButtonPressed(MouseCode button) const
    {
        if (!IsCodeValid<MouseCode, s_MaxMouseButtons>(button))
        {
            return false;
        }

        return m_MousePressed[static_cast<size_t>(button)];
    }

    bool Input::IsMouseButtonReleased(MouseCode button) const
    {
        if (!IsCodeValid<MouseCode, s_MaxMouseButtons>(button))
        {
            return false;
        }

        return m_MouseReleased[static_cast<size_t>(button)];
    }

    void Input::OnKeyPressed(KeyCode key, bool isRepeat)
    {
        if (!IsCodeValid<KeyCode, s_MaxKeys>(key))
        {
            return;
        }

        const size_t l_Index = static_cast<size_t>(key);
        const bool l_WasDown = m_CurrentKeyState[l_Index];

        m_CurrentKeyState[l_Index] = true;
        if (!l_WasDown)
        {
            // First transition into the down state generates a pressed edge.
            m_KeyPressed[l_Index] = true;
        }

        if (isRepeat)
        {
            // GLFW treats repeat events as additional presses while the key is held.
            m_KeyRepeated[l_Index] = true;
        }
    }

    void Input::OnKeyReleased(KeyCode key)
    {
        if (!IsCodeValid<KeyCode, s_MaxKeys>(key))
        {
            return;
        }

        const size_t l_Index = static_cast<size_t>(key);
        const bool l_WasDown = m_CurrentKeyState[l_Index];

        m_CurrentKeyState[l_Index] = false;
        if (l_WasDown)
        {
            m_KeyReleased[l_Index] = true;
        }
    }

    void Input::OnMouseButtonPressed(MouseCode button)
    {
        if (!IsCodeValid<MouseCode, s_MaxMouseButtons>(button))
        {
            return;
        }

        const size_t l_Index = static_cast<size_t>(button);
        const bool l_WasDown = m_CurrentMouseState[l_Index];

        m_CurrentMouseState[l_Index] = true;
        if (!l_WasDown)
        {
            m_MousePressed[l_Index] = true;
        }
    }

    void Input::OnMouseButtonReleased(MouseCode button)
    {
        if (!IsCodeValid<MouseCode, s_MaxMouseButtons>(button))
        {
            return;
        }

        const size_t l_Index = static_cast<size_t>(button);
        const bool l_WasDown = m_CurrentMouseState[l_Index];

        m_CurrentMouseState[l_Index] = false;
        if (l_WasDown)
        {
            m_MouseReleased[l_Index] = true;
        }
    }

    void Input::EndFrame()
    {
        m_PreviousKeyState = m_CurrentKeyState;
        m_PreviousMouseState = m_CurrentMouseState;

        std::fill(m_KeyPressed.begin(), m_KeyPressed.end(), false);
        std::fill(m_KeyReleased.begin(), m_KeyReleased.end(), false);
        std::fill(m_KeyRepeated.begin(), m_KeyRepeated.end(), false);

        std::fill(m_MousePressed.begin(), m_MousePressed.end(), false);
        std::fill(m_MouseReleased.begin(), m_MouseReleased.end(), false);

        // TODO: When controller support arrives, this is where we can reset gamepad
        // edges alongside the keyboard and mouse state.
    }
}