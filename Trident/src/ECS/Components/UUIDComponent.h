#pragma once

#include "Core/Utilities.h"

namespace Trident
{
    /**
     * @brief Associates a stable UUID with each entity.
     *
     * ImGui widgets rely on this component to supply unique IDs even when multiple entities share the same
     * display name. UUIDs persist through serialization so saved scenes reopen with consistent identifiers.
     */
    struct UUIDComponent
    {
        Utilities::UUID m_ID{};
    };
}