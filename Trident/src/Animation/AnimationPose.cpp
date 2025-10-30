#include "Animation/AnimationPose.h"
#include "Animation/AnimationRemap.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Trident
{
    namespace Animation
    {
        void AnimationPose::Resize(size_t boneCount)
        {
            m_Translations.resize(boneCount, glm::vec3{ 0.0f });
            m_Rotations.resize(boneCount, glm::quat{ 1.0f, 0.0f, 0.0f, 0.0f });
            m_Scales.resize(boneCount, glm::vec3{ 1.0f });
        }

        void AnimationMask::Resize(size_t boneCount)
        {
            m_BoneWeights.resize(boneCount, 1.0f);
        }

        float AnimationMask::GetWeight(size_t boneIndex) const
        {
            if (boneIndex >= m_BoneWeights.size())
            {
                return 1.0f;
            }

            return m_BoneWeights[boneIndex];
        }

        namespace
        {
            glm::vec3 SampleVectorKeys(const std::vector<VectorKeyframe>& keys, float sampleTime, const glm::vec3& defaultValue)
            {
                if (keys.empty())
                {
                    return defaultValue;
                }

                if (keys.size() == 1 || sampleTime <= keys.front().m_TimeSeconds)
                {
                    return keys.front().m_Value;
                }

                for (size_t it_Index = 0; it_Index + 1 < keys.size(); ++it_Index)
                {
                    const VectorKeyframe& l_Current = keys[it_Index];
                    const VectorKeyframe& l_Next = keys[it_Index + 1];
                    if (sampleTime < l_Next.m_TimeSeconds)
                    {
                        const float l_Denominator = l_Next.m_TimeSeconds - l_Current.m_TimeSeconds;
                        const float l_T = (l_Denominator > std::numeric_limits<float>::epsilon()) ? (sampleTime - l_Current.m_TimeSeconds) / l_Denominator : 0.0f;

                        return glm::mix(l_Current.m_Value, l_Next.m_Value, glm::clamp(l_T, 0.0f, 1.0f));
                    }
                }

                return keys.back().m_Value;
            }

            glm::quat SampleQuaternionKeys(const std::vector<QuaternionKeyframe>& keys, float sampleTime, const glm::quat& defaultValue)
            {
                if (keys.empty())
                {
                    return defaultValue;
                }

                if (keys.size() == 1 || sampleTime <= keys.front().m_TimeSeconds)
                {
                    return glm::normalize(keys.front().m_Value);
                }

                for (size_t it_Index = 0; it_Index + 1 < keys.size(); ++it_Index)
                {
                    const QuaternionKeyframe& l_Current = keys[it_Index];
                    const QuaternionKeyframe& l_Next = keys[it_Index + 1];
                    if (sampleTime < l_Next.m_TimeSeconds)
                    {
                        const float l_Denominator = l_Next.m_TimeSeconds - l_Current.m_TimeSeconds;
                        const float l_T = (l_Denominator > std::numeric_limits<float>::epsilon()) ? (sampleTime - l_Current.m_TimeSeconds) / l_Denominator : 0.0f;

                        return glm::normalize(glm::slerp(l_Current.m_Value, l_Next.m_Value, glm::clamp(l_T, 0.0f, 1.0f)));
                    }
                }

                return glm::normalize(keys.back().m_Value);
            }

            struct TransformDecomposition
            {
                glm::vec3 m_Translation{ 0.0f };
                glm::quat m_Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
                glm::vec3 m_Scale{ 1.0f };
            };

            TransformDecomposition DecomposeBindTransform(const Bone& bone)
            {
                TransformDecomposition l_Result{};
                const glm::mat4& l_Local = bone.m_LocalBindTransform;
                l_Result.m_Translation = glm::vec3(l_Local[3]);

                const glm::vec3 l_Column0 = glm::vec3(l_Local[0]);
                const glm::vec3 l_Column1 = glm::vec3(l_Local[1]);
                const glm::vec3 l_Column2 = glm::vec3(l_Local[2]);

                glm::vec3 l_Scale{};
                l_Scale.x = glm::length(l_Column0);
                l_Scale.y = glm::length(l_Column1);
                l_Scale.z = glm::length(l_Column2);

                glm::mat3 l_RotationMatrix{ 1.0f };
                if (l_Scale.x > std::numeric_limits<float>::epsilon())
                {
                    l_RotationMatrix[0] = l_Column0 / l_Scale.x;
                }
                else
                {
                    l_RotationMatrix[0] = l_Column0;
                }

                if (l_Scale.y > std::numeric_limits<float>::epsilon())
                {
                    l_RotationMatrix[1] = l_Column1 / l_Scale.y;
                }
                else
                {
                    l_RotationMatrix[1] = l_Column1;
                }

                if (l_Scale.z > std::numeric_limits<float>::epsilon())
                {
                    l_RotationMatrix[2] = l_Column2 / l_Scale.z;
                }
                else
                {
                    l_RotationMatrix[2] = l_Column2;
                }

                l_Result.m_Rotation = glm::normalize(glm::quat_cast(l_RotationMatrix));
                l_Result.m_Scale = l_Scale;

                return l_Result;
            }
        }

        AnimationPose AnimationPoseUtilities::BuildRestPose(const Skeleton& skeleton)
        {
            AnimationPose l_Result{};
            const size_t l_BoneCount = skeleton.m_Bones.size();
            l_Result.Resize(l_BoneCount);

            for (size_t it_Bone = 0; it_Bone < l_BoneCount; ++it_Bone)
            {
                const Bone& l_Bone = skeleton.m_Bones[it_Bone];
                const TransformDecomposition l_Default = DecomposeBindTransform(l_Bone);
                l_Result.m_Translations[it_Bone] = l_Default.m_Translation;
                l_Result.m_Rotations[it_Bone] = l_Default.m_Rotation;
                l_Result.m_Scales[it_Bone] = l_Default.m_Scale;
            }

            return l_Result;
        }

        AnimationPose AnimationPoseUtilities::SampleClipPose(const Skeleton& skeleton, const AnimationClip& clip, float sampleTimeSeconds)
        {
            AnimationPose l_Result = BuildRestPose(skeleton);
            const size_t l_BoneCount = skeleton.m_Bones.size();

            for (const TransformChannel& it_Channel : clip.m_Channels)
            {
                const int l_EffectiveIndex = ResolveChannelBoneIndex(it_Channel, skeleton, clip.m_Name);
                if (l_EffectiveIndex < 0)
                {
                    continue;
                }

                const size_t l_BoneIndex = static_cast<size_t>(l_EffectiveIndex);
                if (l_BoneIndex >= l_BoneCount)
                {
                    continue;
                }

                l_Result.m_Translations[l_BoneIndex] = SampleVectorKeys(it_Channel.m_TranslationKeys, sampleTimeSeconds, l_Result.m_Translations[l_BoneIndex]);
                l_Result.m_Rotations[l_BoneIndex] = SampleQuaternionKeys(it_Channel.m_RotationKeys, sampleTimeSeconds, l_Result.m_Rotations[l_BoneIndex]);
                l_Result.m_Scales[l_BoneIndex] = SampleVectorKeys(it_Channel.m_ScaleKeys, sampleTimeSeconds, l_Result.m_Scales[l_BoneIndex]);
            }

            return l_Result;
        }

        void AnimationPoseUtilities::BlendPose(AnimationPose& basePose, const AnimationPose& targetPose, float blendWeight, const AnimationMask* mask)
        {
            const float l_Weight = glm::clamp(blendWeight, 0.0f, 1.0f);
            const size_t l_Count = std::min(basePose.m_Translations.size(), targetPose.m_Translations.size());

            for (size_t it_Index = 0; it_Index < l_Count; ++it_Index)
            {
                const float l_Mask = (mask != nullptr) ? mask->GetWeight(it_Index) : 1.0f;
                const float l_FinalWeight = glm::clamp(l_Weight * l_Mask, 0.0f, 1.0f);
                basePose.m_Translations[it_Index] = glm::mix(basePose.m_Translations[it_Index], targetPose.m_Translations[it_Index], l_FinalWeight);
                basePose.m_Rotations[it_Index] = glm::normalize(glm::slerp(basePose.m_Rotations[it_Index], targetPose.m_Rotations[it_Index], l_FinalWeight));
                basePose.m_Scales[it_Index] = glm::mix(basePose.m_Scales[it_Index], targetPose.m_Scales[it_Index], l_FinalWeight);
            }
        }

        void AnimationPoseUtilities::AdditivePose(AnimationPose& basePose, const AnimationPose& additivePose, float additiveWeight, const AnimationMask* mask)
        {
            const size_t l_Count = std::min(basePose.m_Translations.size(), additivePose.m_Translations.size());

            for (size_t it_Index = 0; it_Index < l_Count; ++it_Index)
            {
                const float l_Mask = (mask != nullptr) ? mask->GetWeight(it_Index) : 1.0f;
                const float l_FinalWeight = additiveWeight * l_Mask;
                basePose.m_Translations[it_Index] += additivePose.m_Translations[it_Index] * l_FinalWeight;
                basePose.m_Rotations[it_Index] = glm::normalize(glm::slerp(basePose.m_Rotations[it_Index], basePose.m_Rotations[it_Index] * glm::normalize(additivePose.m_Rotations[it_Index]), l_FinalWeight));
                basePose.m_Scales[it_Index] += additivePose.m_Scales[it_Index] * l_FinalWeight;
            }
        }

        std::vector<glm::mat4> AnimationPoseUtilities::ComposeSkinningMatrices(const Skeleton& skeleton, const AnimationPose& pose)
        {
            const size_t l_BoneCount = skeleton.m_Bones.size();
            std::vector<glm::mat4> l_LocalTransforms(l_BoneCount, glm::mat4{ 1.0f });
            std::vector<glm::mat4> l_GlobalTransforms(l_BoneCount, glm::mat4{ 1.0f });
            std::vector<glm::mat4> l_PoseMatrices(l_BoneCount, glm::mat4{ 1.0f });

            for (size_t it_Bone = 0; it_Bone < l_BoneCount; ++it_Bone)
            {
                const glm::mat4 l_TranslationMatrix = glm::translate(glm::mat4{ 1.0f }, pose.m_Translations[it_Bone]);
                const glm::mat4 l_RotationMatrix = glm::toMat4(glm::normalize(pose.m_Rotations[it_Bone]));
                const glm::mat4 l_ScaleMatrix = glm::scale(glm::mat4{ 1.0f }, pose.m_Scales[it_Bone]);
                l_LocalTransforms[it_Bone] = l_TranslationMatrix * l_RotationMatrix * l_ScaleMatrix;
            }

            struct WorkItem
            {
                int m_BoneIndex = -1;
                glm::mat4 m_ParentMatrix{ 1.0f };
            };

            std::vector<WorkItem> l_Worklist{};
            l_Worklist.reserve(l_BoneCount);

            if (skeleton.m_RootBoneIndex >= 0 && static_cast<size_t>(skeleton.m_RootBoneIndex) < l_BoneCount)
            {
                l_Worklist.push_back({ skeleton.m_RootBoneIndex, glm::mat4{ 1.0f } });
            }
            else
            {
                for (size_t it_Bone = 0; it_Bone < l_BoneCount; ++it_Bone)
                {
                    const Bone& l_Bone = skeleton.m_Bones[it_Bone];
                    if (l_Bone.m_ParentIndex < 0)
                    {
                        l_Worklist.push_back({ static_cast<int>(it_Bone), glm::mat4{ 1.0f } });
                    }
                }

                if (l_Worklist.empty() && l_BoneCount > 0)
                {
                    l_Worklist.push_back({ 0, glm::mat4{ 1.0f } });
                }
            }

            while (!l_Worklist.empty())
            {
                WorkItem l_Item = l_Worklist.back();
                l_Worklist.pop_back();

                if (l_Item.m_BoneIndex < 0)
                {
                    continue;
                }

                const size_t l_Index = static_cast<size_t>(l_Item.m_BoneIndex);
                if (l_Index >= l_BoneCount)
                {
                    continue;
                }

                const glm::mat4 l_Global = l_Item.m_ParentMatrix * l_LocalTransforms[l_Index];
                l_GlobalTransforms[l_Index] = l_Global;

                const Bone& l_Bone = skeleton.m_Bones[l_Index];
                for (int it_Child : l_Bone.m_Children)
                {
                    l_Worklist.push_back({ it_Child, l_Global });
                }
            }

            for (size_t it_Bone = 0; it_Bone < l_BoneCount; ++it_Bone)
            {
                if (l_GlobalTransforms[it_Bone] == glm::mat4{ 1.0f })
                {
                    l_GlobalTransforms[it_Bone] = l_LocalTransforms[it_Bone];
                }

                const glm::mat4& l_InverseBind = skeleton.m_Bones[it_Bone].m_InverseBindMatrix;
                l_PoseMatrices[it_Bone] = l_GlobalTransforms[it_Bone] * l_InverseBind;
            }

            return l_PoseMatrices;
        }
    }
}