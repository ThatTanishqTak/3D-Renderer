#include "Animation/AnimationRemap.h"

#include "Core/Utilities.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace Trident
{
    namespace Animation
    {
        namespace
        {
            std::string NormaliseSourceName(std::string name)
            {
                // Keep behaviour aligned with the loader: trim whitespace and drop the Mixamo prefix when present.
                auto a_IsSpace = [](unsigned char character)
                    {
                        return std::isspace(character) != 0;
                    };

                name.erase(name.begin(), std::find_if(name.begin(), name.end(), [a_IsSpace](unsigned char character)
                    {
                        return !a_IsSpace(character);
                    }));
                name.erase(std::find_if(name.rbegin(), name.rend(), [a_IsSpace](unsigned char character)
                    {
                        return !a_IsSpace(character);
                    }).base(), name.end());

                constexpr std::string_view s_MixamoPrefix = "mixamorig:";
                if (name.size() > s_MixamoPrefix.size() && name.compare(0, s_MixamoPrefix.size(), s_MixamoPrefix) == 0)
                {
                    name.erase(0, s_MixamoPrefix.size());
                }

                return name;
            }
        }

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
                auto a_SourceIt = skeleton.m_SourceNameToIndex.find(l_SourceName);
                if (a_SourceIt != skeleton.m_SourceNameToIndex.end())
                {
                    l_RemappedIndex = a_SourceIt->second;
                }
                else
                {
                    const std::string l_Normalised = NormaliseSourceName(l_SourceName);
                    auto a_NormalisedIt = skeleton.m_NameToIndex.find(l_Normalised);
                    if (a_NormalisedIt != skeleton.m_NameToIndex.end())
                    {
                        l_RemappedIndex = a_NormalisedIt->second;
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