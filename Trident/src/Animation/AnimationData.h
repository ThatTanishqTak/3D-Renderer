#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace Trident
{
    namespace Animation
    {
        /**
         * @brief Stores the values for a vector-based keyframe (translation or scale).
         */
        struct VectorKeyframe
        {
            float m_TimeSeconds = 0.0f;      //!< Timestamp expressed in seconds after normalising Assimp ticks.
            glm::vec3 m_Value{ 0.0f };       //!< Stored vector value for this keyframe.
        };

        /**
         * @brief Stores the values for a quaternion-based keyframe (rotation).
         */
        struct QuaternionKeyframe
        {
            float m_TimeSeconds = 0.0f;      //!< Timestamp expressed in seconds after normalising Assimp ticks.
            glm::quat m_Value{ 1.0f, 0.0f, 0.0f, 0.0f }; //!< Quaternion rotation sampled at the keyframe.
        };

        /**
         * @brief Represents a transform channel describing animation for a single bone.
         */
        struct TransformChannel
        {
            int m_BoneIndex = -1;                               //!< Index into the owning skeleton's bone array.
            std::vector<VectorKeyframe> m_TranslationKeys{};    //!< Translation keyframes sampled in seconds.
            std::vector<QuaternionKeyframe> m_RotationKeys{};   //!< Rotation keyframes sampled in seconds.
            std::vector<VectorKeyframe> m_ScaleKeys{};          //!< Scale keyframes sampled in seconds.
        };

        /**
         * @brief Represents a baked animation clip sourced from the imported asset.
         */
        struct AnimationClip
        {
            std::string m_Name;                                 //!< Clip identifier as reported by the source document.
            float m_DurationSeconds = 0.0f;                     //!< Duration in seconds after normalising ticks.
            float m_TicksPerSecond = 0.0f;                      //!< Original tick rate retained for debugging/reference.
            std::vector<TransformChannel> m_Channels{};         //!< All animation channels targeting individual bones.
        };

        /**
         * @brief Represents a single bone within a skeleton hierarchy.
         */
        struct Bone
        {
            std::string m_Name;                                 //!< Normalised bone name (Mixamo prefixes removed, canonicalised).
            std::string m_SourceName;                           //!< Original bone name as authored in the asset.
            int m_ParentIndex = -1;                             //!< Parent bone index (-1 when the bone is the root).
            std::vector<int> m_Children{};                      //!< Child bone indices for hierarchical traversal.
            glm::mat4 m_LocalBindTransform{ 1.0f };             //!< Bind pose transform relative to the parent bone.
            glm::mat4 m_InverseBindMatrix{ 1.0f };              //!< Inverse bind matrix used for skinning calculations.
        };

        /**
         * @brief Container describing the skeleton extracted from a model.
         */
        struct Skeleton
        {
            int m_RootBoneIndex = -1;                                           //!< Root bone for the hierarchy (-1 when unset).
            std::vector<Bone> m_Bones{};                                        //!< Linear storage of bones for GPU-friendly access.
            std::unordered_map<std::string, int> m_NameToIndex{};               //!< Lookup table from normalised name to bone index.
            std::unordered_map<std::string, int> m_SourceNameToIndex{};         //!< Lookup table from source name to bone index.
        };
    }
}