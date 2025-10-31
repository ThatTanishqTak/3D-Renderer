#include "Animation/AnimationRemap.h"

#include "Animation/AnimationSourceRegistry.h"
#include "Core/Utilities.h"

#include <string>
#include <string_view>

namespace Trident
{
    namespace Animation
    {
        int ResolveChannelBoneIndex(const TransformChannel& channel, const Skeleton& skeleton, std::string_view clipName)
        {
            const size_t l_BoneCount = skeleton.m_Bones.size();
            const int l_OriginalIndex = channel.m_BoneIndex;
            const bool l_IndexInRange = l_OriginalIndex >= 0 && static_cast<size_t>(l_OriginalIndex) < l_BoneCount;
            const std::string& l_SourceName = channel.m_SourceBoneName;

            if (l_IndexInRange)
            {
                if (l_SourceName.empty())
                {
                    return l_OriginalIndex;
                }

                const Bone& l_Bone = skeleton.m_Bones[static_cast<size_t>(l_OriginalIndex)];
                if (l_Bone.m_SourceName == l_SourceName || l_Bone.m_Name == l_SourceName)
                {
                    return l_OriginalIndex;
                }
            }

            int l_RemappedIndex = -1;
            if (!l_SourceName.empty())
            {
                // First attempt a direct lookup – rigs authored with canonical names already normalised will hit this fast path.
                auto a_DirectIt = skeleton.m_NameToIndex.find(l_SourceName);
                if (a_DirectIt != skeleton.m_NameToIndex.end())
                {
                    l_RemappedIndex = a_DirectIt->second;
                }
                else
                {
                    // Fall back to the global registry so every channel is canonicalised using the assigned source profile.
                    const AnimationSourceRegistry& l_Registry = AnimationSourceRegistry::Get();
                    const std::string l_Normalised = !skeleton.m_SourceAssetId.empty()
                        ? l_Registry.NormaliseBoneName(l_SourceName, skeleton.m_SourceAssetId)
                        : l_Registry.NormaliseBoneNameWithProfile(l_SourceName, skeleton.m_SourceProfile);

                    if (!l_Normalised.empty())
                    {
                        auto a_NormalisedIt = skeleton.m_NameToIndex.find(l_Normalised);
                        if (a_NormalisedIt != skeleton.m_NameToIndex.end())
                        {
                            l_RemappedIndex = a_NormalisedIt->second;
                        }
                    }
                }
            }

            if (l_RemappedIndex < 0 || static_cast<size_t>(l_RemappedIndex) >= l_BoneCount)
            {
                const std::string l_ClipLabel = clipName.empty() ? std::string("<unnamed>") : std::string(clipName);
                TR_CORE_WARN("Failed to map animation channel targeting '{}' while sampling clip '{}' (stale index {}).", l_SourceName.c_str(), l_ClipLabel.c_str(), l_OriginalIndex);

                return -1;
            }

            return l_RemappedIndex;
        }
    }
}