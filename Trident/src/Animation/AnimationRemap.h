#pragma once

#include "Animation/AnimationData.h"

#include <string_view>

namespace Trident
{
    namespace Animation
    {
        /**
         * @brief Resolve the destination bone index for a transform channel when evaluating a clip.
         *
         * Channels authored against different rigs occasionally carry stale indices once retargeted
         * onto the runtime skeleton. The helper keeps Mixamo-normalised names while also consulting
         * the original source names so editor metadata coming from itch.io packages can be preserved.
         * When no destination bone can be found a warning is emitted and the caller should skip the
         * channel to keep evaluation robust.
         */
        int ResolveChannelBoneIndex(const TransformChannel& channel, const Skeleton& skeleton, std::string_view clipName);
    }
}