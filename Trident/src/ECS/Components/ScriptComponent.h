#pragma once

#include <string>

namespace Trident
{
    /**
     * @brief Describes a high-level script attachment for runtime behaviour.
     *
     * Each entity can reference an external script (Lua, Python, C#, etc.) via a
     * file path or identifier. The engine does not execute the script directly yet;
     * instead, the Scene runtime toggles the execution flags so a scripting layer
     * can consume the state. Future work can integrate a scripting VM that looks up
     * and executes the referenced asset when m_IsRunning becomes true.
     */
    struct ScriptComponent
    {
        /// Path or identifier pointing to the script asset.
        std::string m_ScriptPath;
        /// Automatically start when the scene enters play mode.
        bool m_AutoStart{ true };
        /// Runtime flag toggled by the scene to indicate active execution.
        bool m_IsRunning{ false };
    };
}