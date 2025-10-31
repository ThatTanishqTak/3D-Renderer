#include "Animation/AnimationSourceRegistry.h"

#include "Core/Utilities.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace Trident
{
    namespace Animation
    {
        namespace
        {
            /**
             * @brief Normalise asset identifiers so profile lookups remain deterministic.
             */
            std::string NormaliseAssetKey(const std::string& assetId)
            {
                if (assetId.empty())
                {
                    return {};
                }

                std::string l_Normalised = Utilities::FileManagement::NormalizePath(assetId);
                if (l_Normalised.empty())
                {
                    return assetId;
                }

                return l_Normalised;
            }
        }

        AnimationSourceRegistry& AnimationSourceRegistry::Get()
        {
            static AnimationSourceRegistry s_Instance{};
            return s_Instance;
        }

        AnimationSourceRegistry::AnimationSourceRegistry()
        {
            // Default profile mirrors historical Mixamo behaviour while stripping common namespaces from other pipelines.
            m_DefaultProfile.m_Name = "Default";
            m_DefaultProfile.m_Prefixes = { "mixamorig:", "armature|", "armature/", "armature:" };
            m_DefaultProfile.m_Aliases.clear();
            m_DefaultProfile.m_TrimWhitespace = true;
            m_DefaultProfile.m_RemoveNamespaceTokens = true;
            m_DefaultProfile.m_CaseInsensitivePrefixes = true;
            m_DefaultProfile.m_ForceLowerCase = true;
            m_DefaultProfile.m_RemoveInternalWhitespace = true;

            m_Profiles.emplace(m_DefaultProfile.m_Name, m_DefaultProfile);
        }

        void AnimationSourceRegistry::RegisterProfile(const AnimationSourceProfile& profile)
        {
            if (profile.m_Name.empty())
            {
                TR_CORE_WARN("AnimationSourceRegistry::RegisterProfile received an empty name.");
                return;
            }

            AnimationSourceProfile l_Profile = profile;
            std::unordered_map<std::string, std::string> l_CanonicalAliases{};
            for (const auto& it_Alias : profile.m_Aliases)
            {
                const std::string l_Key = ApplyProfile(it_Alias.first, l_Profile, false);
                const std::string l_Value = ApplyProfile(it_Alias.second, l_Profile, false);
                if (!l_Key.empty() && !l_Value.empty())
                {
                    l_CanonicalAliases[l_Key] = l_Value;
                }
            }
            l_Profile.m_Aliases = std::move(l_CanonicalAliases);

            m_Profiles[l_Profile.m_Name] = std::move(l_Profile);
        }

        void AnimationSourceRegistry::AssignProfileToAsset(const std::string& assetId, const std::string& profileName)
        {
            if (assetId.empty())
            {
                return;
            }

            const std::string l_ProfileName = profileName.empty() ? m_DefaultProfile.m_Name : profileName;
            const AnimationSourceProfile& l_Profile = ResolveProfileByName(l_ProfileName);
            std::string l_NormalisedAsset = NormaliseAssetKey(assetId);
            if (l_NormalisedAsset.empty())
            {
                l_NormalisedAsset = assetId;
            }

            m_AssetProfiles[l_NormalisedAsset] = l_Profile.m_Name;
        }

        void AnimationSourceRegistry::RegisterAlias(const std::string& profileName, const std::string& alias, const std::string& canonicalTarget)
        {
            const std::string l_ProfileName = profileName.empty() ? m_DefaultProfile.m_Name : profileName;
            auto a_ProfileIt = m_Profiles.find(l_ProfileName);
            if (a_ProfileIt == m_Profiles.end())
            {
                TR_CORE_WARN("AnimationSourceRegistry::RegisterAlias missing profile '{}'.", l_ProfileName.c_str());
                return;
            }

            AnimationSourceProfile& l_Profile = a_ProfileIt->second;
            const std::string l_Key = ApplyProfile(alias, l_Profile, false);
            const std::string l_Value = ApplyProfile(canonicalTarget, l_Profile, false);
            if (l_Key.empty() || l_Value.empty())
            {
                return;
            }

            l_Profile.m_Aliases[l_Key] = l_Value;
        }

        std::string AnimationSourceRegistry::NormaliseBoneName(const std::string& boneName, const std::string& assetId) const
        {
            const AnimationSourceProfile& l_Profile = ResolveProfileInternal(assetId);
            return ApplyProfile(boneName, l_Profile, true);
        }

        std::string AnimationSourceRegistry::NormaliseBoneNameWithProfile(const std::string& boneName, const std::string& profileName) const
        {
            const AnimationSourceProfile& l_Profile = ResolveProfileByName(profileName);
            return ApplyProfile(boneName, l_Profile, true);
        }

        std::string AnimationSourceRegistry::ResolveProfileName(const std::string& assetId) const
        {
            const AnimationSourceProfile& l_Profile = ResolveProfileInternal(assetId);
            return l_Profile.m_Name;
        }

        const AnimationSourceProfile& AnimationSourceRegistry::ResolveProfileInternal(const std::string& assetId) const
        {
            if (!assetId.empty())
            {
                std::string l_Normalised = NormaliseAssetKey(assetId);
                if (l_Normalised.empty())
                {
                    l_Normalised = assetId;
                }

                auto a_ProfileIt = m_AssetProfiles.find(l_Normalised);
                if (a_ProfileIt != m_AssetProfiles.end())
                {
                    return ResolveProfileByName(a_ProfileIt->second);
                }
            }

            return m_DefaultProfile;
        }

        const AnimationSourceProfile& AnimationSourceRegistry::ResolveProfileByName(const std::string& profileName) const
        {
            if (!profileName.empty())
            {
                auto a_ProfileIt = m_Profiles.find(profileName);
                if (a_ProfileIt != m_Profiles.end())
                {
                    return a_ProfileIt->second;
                }

                TR_CORE_WARN("AnimationSourceRegistry: profile '{}' not found. Falling back to default.", profileName.c_str());
            }

            return m_DefaultProfile;
        }

        std::string AnimationSourceRegistry::ApplyProfile(const std::string& boneName, const AnimationSourceProfile& profile, bool allowAlias) const
        {
            if (boneName.empty())
            {
                return {};
            }

            auto a_IsSpace = [](unsigned char character)
                {
                    return std::isspace(character) != 0;
                };

            std::string l_Working = boneName;
            if (profile.m_TrimWhitespace)
            {
                l_Working.erase(l_Working.begin(), std::find_if(l_Working.begin(), l_Working.end(), [a_IsSpace](unsigned char character)
                    {
                        return !a_IsSpace(character);
                    }));
                l_Working.erase(std::find_if(l_Working.rbegin(), l_Working.rend(), [a_IsSpace](unsigned char character)
                    {
                        return !a_IsSpace(character);
                    }).base(), l_Working.end());
            }

            std::string l_Candidate = l_Working;
            if (!profile.m_Prefixes.empty())
            {
                for (const std::string& it_Prefix : profile.m_Prefixes)
                {
                    if (it_Prefix.empty() || l_Candidate.size() < it_Prefix.size())
                    {
                        continue;
                    }

                    bool l_Matches = true;
                    for (size_t it_Index = 0; it_Index < it_Prefix.size(); ++it_Index)
                    {
                        unsigned char l_NameCharacter = static_cast<unsigned char>(l_Candidate[it_Index]);
                        unsigned char l_PrefixCharacter = static_cast<unsigned char>(it_Prefix[it_Index]);

                        if (profile.m_CaseInsensitivePrefixes)
                        {
                            l_NameCharacter = static_cast<unsigned char>(std::tolower(l_NameCharacter));
                            l_PrefixCharacter = static_cast<unsigned char>(std::tolower(l_PrefixCharacter));
                        }

                        if (l_NameCharacter != l_PrefixCharacter)
                        {
                            l_Matches = false;
                            break;
                        }
                    }

                    if (l_Matches)
                    {
                        l_Candidate.erase(0, it_Prefix.size());
                        break;
                    }
                }
            }

            if (profile.m_RemoveNamespaceTokens)
            {
                const size_t l_Token = l_Candidate.find_first_of(":|/");
                if (l_Token != std::string::npos)
                {
                    l_Candidate.erase(0, l_Token + 1);
                }
            }

            if (profile.m_TrimWhitespace)
            {
                l_Candidate.erase(l_Candidate.begin(), std::find_if(l_Candidate.begin(), l_Candidate.end(), [a_IsSpace](unsigned char character)
                    {
                        return !a_IsSpace(character);
                    }));
                l_Candidate.erase(std::find_if(l_Candidate.rbegin(), l_Candidate.rend(), [a_IsSpace](unsigned char character)
                    {
                        return !a_IsSpace(character);
                    }).base(), l_Candidate.end());
            }

            if (profile.m_RemoveInternalWhitespace)
            {
                std::string l_Compact{};
                l_Compact.reserve(l_Candidate.size());
                for (char it_Character : l_Candidate)
                {
                    if (a_IsSpace(static_cast<unsigned char>(it_Character)))
                    {
                        continue;
                    }

                    l_Compact.push_back(it_Character);
                }

                l_Candidate = std::move(l_Compact);
            }

            if (profile.m_ForceLowerCase)
            {
                std::transform(l_Candidate.begin(), l_Candidate.end(), l_Candidate.begin(), [](unsigned char character)
                    {
                        return static_cast<char>(std::tolower(character));
                    });
            }

            if (l_Candidate.empty())
            {
                l_Candidate = profile.m_ForceLowerCase ? l_Working : boneName;
                if (profile.m_ForceLowerCase)
                {
                    std::transform(l_Candidate.begin(), l_Candidate.end(), l_Candidate.begin(), [](unsigned char character)
                        {
                            return static_cast<char>(std::tolower(character));
                        });
                }
            }

            if (allowAlias)
            {
                auto a_Alias = profile.m_Aliases.find(l_Candidate);
                if (a_Alias != profile.m_Aliases.end())
                {
                    return a_Alias->second;
                }
            }

            return l_Candidate;
        }
    }
}