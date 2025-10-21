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

    void Input::BeginFrame()
    {
        if (m_FrameActive)
        {
            return;
        }

        m_FrameActive = true;

        m_PreviousKeyState = m_CurrentKeyState;
        m_PreviousMouseState = m_CurrentMouseState;
        m_PreviousMousePosition = m_CurrentMousePosition;

        std::fill(m_KeyPressed.begin(), m_KeyPressed.end(), false);
        std::fill(m_KeyReleased.begin(), m_KeyReleased.end(), false);
        std::fill(m_KeyRepeated.begin(), m_KeyRepeated.end(), false);

        std::fill(m_MousePressed.begin(), m_MousePressed.end(), false);
        std::fill(m_MouseReleased.begin(), m_MouseReleased.end(), false);

        m_MouseDelta = glm::vec2{ 0.0f, 0.0f };
        m_ScrollDelta = glm::vec2{ 0.0f, 0.0f };
    }

    void Input::EndFrame()
    {
        m_FrameActive = false;
    }

    bool Input::IsKeyDown(KeyCode key) const
    {
        if (!IsCodeValid<KeyCode, s_MaxKeys>(key))
        {
            return false;
        }

        if (m_WantCaptureKeyboard)
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

        if (m_WantCaptureKeyboard)
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

        if (m_WantCaptureKeyboard)
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

        if (m_WantCaptureKeyboard)
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

        if (m_WantCaptureMouse)
        {
            return false;
        }

        return m_CurrentMouseState[static_cast<size_t>(button)];
    }

    bool Input::IsMouseButtonPressed(MouseCode button) const
    {
        return WasMouseButtonPressed(button);
    }

    bool Input::IsMouseButtonReleased(MouseCode button) const
    {
        return WasMouseButtonReleased(button);
    }

    bool Input::WasMouseButtonPressed(MouseCode button) const
    {
        if (!IsCodeValid<MouseCode, s_MaxMouseButtons>(button))
        {
            return false;
        }

        if (m_WantCaptureMouse)
        {
            return false;
        }

        return m_MousePressed[static_cast<size_t>(button)];
    }

    bool Input::WasMouseButtonReleased(MouseCode button) const
    {
        if (!IsCodeValid<MouseCode, s_MaxMouseButtons>(button))
        {
            return false;
        }

        if (m_WantCaptureMouse)
        {
            return false;
        }

        return m_MouseReleased[static_cast<size_t>(button)];
    }

    glm::vec2 Input::GetMousePosition() const
    {
        return m_CurrentMousePosition;
    }

    glm::vec2 Input::GetMouseDelta() const
    {
        if (m_WantCaptureMouse)
        {
            return glm::vec2{ 0.0f, 0.0f };
        }

        return m_MouseDelta;
    }

    glm::vec2 Input::GetScrollDelta() const
    {
        if (m_WantCaptureMouse)
        {
            return glm::vec2{ 0.0f, 0.0f };
        }

        return m_ScrollDelta;
    }

    bool Input::HasMousePosition() const
    {
        return m_HasMousePosition;
    }

    void Input::SetUICapture(bool wantMouse, bool wantKeyboard)
    {
        m_WantCaptureMouse = wantMouse;
        m_WantCaptureKeyboard = wantKeyboard;
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

    void Input::OnMouseMoved(float x, float y)
    {
        const glm::vec2 l_NewPosition{ x, y };

        if (!m_HasMousePosition)
        {
            m_HasMousePosition = true;
            m_CurrentMousePosition = l_NewPosition;
            m_PreviousMousePosition = l_NewPosition;

            return;
        }

        m_PreviousMousePosition = m_CurrentMousePosition;
        m_CurrentMousePosition = l_NewPosition;
        m_MouseDelta += (m_CurrentMousePosition - m_PreviousMousePosition);
    }

    void Input::OnMouseScrolled(float xoff, float yoff)
    {
        m_ScrollDelta += glm::vec2{ xoff, yoff };
    }
}