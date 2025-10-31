#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace Trident
{
    namespace Animation
    {
        /**
         * @brief Describes how bone names authored inside an animation asset should be canonicalised.
         *
         * The profile encapsulates the heuristics required to normalise bone names coming from
         * different DCC packages or marketplaces. Prefix stripping, whitespace trimming and
         * case-normalisation are all configurable so the runtime can operate across Mixamo,
         * Unreal, Unity or bespoke rigs without special casing in the loader.
         */
        struct AnimationSourceProfile
        {
            std::string m_Name;                                                     //!< Unique identifier for the profile.
            std::vector<std::string> m_Prefixes{};                                  //!< Prefixes stripped from bone names.
            std::unordered_map<std::string, std::string> m_Aliases{};               //!< Canonical name remaps expressed in canonical form.
            bool m_TrimWhitespace{ true };                                          //!< Whether to trim leading/trailing whitespace.
            bool m_RemoveNamespaceTokens{ true };                                   //!< Whether to discard namespace separators such as ':' or '|'.
            bool m_CaseInsensitivePrefixes{ true };                                 //!< Whether prefix removal should ignore case.
            bool m_ForceLowerCase{ true };                                          //!< Whether canonical names should be converted to lowercase.
            bool m_RemoveInternalWhitespace{ true };                                //!< Whether whitespace characters should be removed entirely.
        };

        /**
         * @brief Global registry exposing canonical bone name utilities for the loader and runtime.
         *
         * The registry keeps track of the normalisation profile assigned to each asset path so the
         * model importer and runtime remapping logic can use consistent rules. Profiles can be
         * registered at startup or via tooling, ensuring newly authored animation libraries remain
         * compatible with existing skeletons. A sensible default profile keeps legacy Mixamo content
         * functioning out of the box.
         */
        class AnimationSourceRegistry
        {
        public:
            static AnimationSourceRegistry& Get();

            void RegisterProfile(const AnimationSourceProfile& profile);
            void AssignProfileToAsset(const std::string& assetId, const std::string& profileName);
            void RegisterAlias(const std::string& profileName, const std::string& alias, const std::string& canonicalTarget);

            [[nodiscard]] std::string NormaliseBoneName(const std::string& boneName, const std::string& assetId) const;
            [[nodiscard]] std::string NormaliseBoneNameWithProfile(const std::string& boneName, const std::string& profileName) const;
            [[nodiscard]] std::string ResolveProfileName(const std::string& assetId) const;

        private:
            AnimationSourceRegistry();

            const AnimationSourceProfile& ResolveProfileInternal(const std::string& assetId) const;
            const AnimationSourceProfile& ResolveProfileByName(const std::string& profileName) const;
            std::string ApplyProfile(const std::string& boneName, const AnimationSourceProfile& profile, bool allowAlias) const;

            AnimationSourceProfile m_DefaultProfile;                                 //!< Built-in profile keeping legacy rigs functional.
            std::unordered_map<std::string, AnimationSourceProfile> m_Profiles;      //!< Named profiles registered by tooling.
            std::unordered_map<std::string, std::string> m_AssetProfiles;            //!< Asset identifier to profile mapping.
        };
    }
}