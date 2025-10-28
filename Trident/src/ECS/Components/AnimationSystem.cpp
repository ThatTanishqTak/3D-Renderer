#include "ECS/AnimationSystem.h"

#include "Animation/AnimationAssetService.h"
#include "ECS/Registry.h"
#include "ECS/Components/MeshComponent.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/common.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>

namespace Trident
{
    namespace ECS
    {
        namespace
        {
            /// @brief Describes a decomposed transform, making it easier to blend channels.
            struct TransformDecomposition
            {
                glm::vec3 m_Translation{ 0.0f };
                glm::quat m_Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
                glm::vec3 m_Scale{ 1.0f };
            };

            /// @brief Break a bind-pose matrix into translation, rotation, and scale components.
            TransformDecomposition DecomposeBindTransform(const Animation::Bone& bone)
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

            /// @brief Sample vector keyframes while blending between surrounding keys.
            glm::vec3 SampleVectorKeys(const std::vector<Animation::VectorKeyframe>& keys, float sampleTime, const glm::vec3& defaultValue)
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
                    const Animation::VectorKeyframe& l_Current = keys[it_Index];
                    const Animation::VectorKeyframe& l_Next = keys[it_Index + 1];
                    if (sampleTime < l_Next.m_TimeSeconds)
                    {
                        const float l_Denominator = l_Next.m_TimeSeconds - l_Current.m_TimeSeconds;
                        const float l_T = (l_Denominator > std::numeric_limits<float>::epsilon()) ?
                            (sampleTime - l_Current.m_TimeSeconds) / l_Denominator : 0.0f;
                        return glm::mix(l_Current.m_Value, l_Next.m_Value, glm::clamp(l_T, 0.0f, 1.0f));
                    }
                }

                return keys.back().m_Value;
            }

            /// @brief Sample quaternion keyframes while blending between surrounding keys.
            glm::quat SampleQuaternionKeys(const std::vector<Animation::QuaternionKeyframe>& keys, float sampleTime, const glm::quat& defaultValue)
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
                    const Animation::QuaternionKeyframe& l_Current = keys[it_Index];
                    const Animation::QuaternionKeyframe& l_Next = keys[it_Index + 1];
                    if (sampleTime < l_Next.m_TimeSeconds)
                    {
                        const float l_Denominator = l_Next.m_TimeSeconds - l_Current.m_TimeSeconds;
                        const float l_T = (l_Denominator > std::numeric_limits<float>::epsilon()) ?
                            (sampleTime - l_Current.m_TimeSeconds) / l_Denominator : 0.0f;
                        return glm::normalize(glm::slerp(l_Current.m_Value, l_Next.m_Value, glm::clamp(l_T, 0.0f, 1.0f)));
                    }
                }

                return glm::normalize(keys.back().m_Value);
            }

        }

        void AnimationSystem::RefreshCachedHandles(AnimationComponent& component, Animation::AnimationAssetService& service)
        {
            std::hash<std::string> l_Hasher{};

            const bool l_HasSkeletonId = !component.m_SkeletonAssetId.empty();
            const size_t l_SkeletonHash = l_HasSkeletonId ? l_Hasher(component.m_SkeletonAssetId) : 0;
            if (!l_HasSkeletonId)
            {
                component.m_SkeletonAssetHandle = Animation::AnimationAssetService::s_InvalidHandle;
                component.m_SkeletonAssetHash = 0;
            }
            else if (component.m_SkeletonAssetHandle == Animation::AnimationAssetService::s_InvalidHandle || l_SkeletonHash != component.m_SkeletonAssetHash)
            {
                component.m_SkeletonAssetHash = l_SkeletonHash;
                component.m_SkeletonAssetHandle = service.AcquireSkeleton(component.m_SkeletonAssetId);
            }

            const bool l_HasAnimationId = !component.m_AnimationAssetId.empty();
            const size_t l_AnimationHash = l_HasAnimationId ? l_Hasher(component.m_AnimationAssetId) : 0;
            if (!l_HasAnimationId)
            {
                component.m_AnimationAssetHandle = Animation::AnimationAssetService::s_InvalidHandle;
                component.m_AnimationAssetHash = 0;
                component.m_CurrentClipIndex = Animation::AnimationAssetService::s_InvalidHandle;
            }
            else if (component.m_AnimationAssetHandle == Animation::AnimationAssetService::s_InvalidHandle || l_AnimationHash != component.m_AnimationAssetHash)
            {
                component.m_AnimationAssetHash = l_AnimationHash;
                component.m_AnimationAssetHandle = service.AcquireAnimationLibrary(component.m_AnimationAssetId);
                component.m_CurrentClipIndex = Animation::AnimationAssetService::s_InvalidHandle;
            }

            const bool l_HasClipName = !component.m_CurrentClip.empty();
            const size_t l_ClipHash = l_HasClipName ? l_Hasher(component.m_CurrentClip) : 0;
            if (!l_HasClipName || component.m_AnimationAssetHandle == Animation::AnimationAssetService::s_InvalidHandle)
            {
                component.m_CurrentClipIndex = Animation::AnimationAssetService::s_InvalidHandle;
                component.m_CurrentClipHash = 0;
            }
            else if (l_ClipHash != component.m_CurrentClipHash)
            {
                component.m_CurrentClipHash = l_ClipHash;
                component.m_CurrentClipIndex = service.ResolveClipIndex(component.m_AnimationAssetHandle, component.m_CurrentClip);
            }
        }

        void AnimationSystem::InitialisePose(AnimationComponent& component)
        {
            // The helper primes the pose cache before the first runtime tick. Future animation blending
            // or state-machine logic can extend this entry point to layer multiple clips or active states.
            component.m_BoneMatrices.clear();
            SampleClipPose(component, 0.0f, component.m_BoneMatrices);
        }

        void AnimationSystem::Update(Registry& registry, float deltaTime)
        {
            const std::vector<Entity>& l_Entities = registry.GetEntities();
            for (Entity it_Entity : l_Entities)
            {
                if (!registry.HasComponent<AnimationComponent>(it_Entity))
                {
                    continue;
                }

                if (!registry.HasComponent<MeshComponent>(it_Entity))
                {
                    // TODO: Extend support for skinned decals or other render paths once the renderer exposes them.
                    continue;
                }

                UpdateComponent(registry, it_Entity, deltaTime);
            }
        }

        void AnimationSystem::UpdateComponent(Registry& registry, Entity entity, float deltaTime)
        {
            AnimationComponent& l_Component = registry.GetComponent<AnimationComponent>(entity);
            if (!l_Component.m_IsPlaying)
            {
                return;
            }

            Animation::AnimationAssetService& l_AssetService = Animation::AnimationAssetService::Get();
            AnimationSystem::RefreshCachedHandles(l_Component, l_AssetService);

            const float l_ScaledDeltaTime = deltaTime * l_Component.m_PlaybackSpeed;
            l_Component.m_CurrentTime += l_ScaledDeltaTime;

            const float l_ClipDuration = ResolveClipDuration(l_Component);
            if (l_ClipDuration > 0.0f)
            {
                if (l_Component.m_CurrentTime > l_ClipDuration)
                {
                    if (l_Component.m_IsLooping)
                    {
                        l_Component.m_CurrentTime = std::fmod(l_Component.m_CurrentTime, l_ClipDuration);
                    }
                    else
                    {
                        l_Component.m_CurrentTime = l_ClipDuration;
                        l_Component.m_IsPlaying = false;
                    }
                }
            }

            SampleClipPose(l_Component, l_Component.m_CurrentTime, l_Component.m_BoneMatrices);
        }

        float AnimationSystem::ResolveClipDuration(const AnimationComponent& component)
        {
            const Animation::AnimationClip* l_Clip = Animation::AnimationAssetService::Get().GetClip(component.m_AnimationAssetHandle, component.m_CurrentClipIndex);
            if (l_Clip != nullptr)
            {
                return l_Clip->m_DurationSeconds;
            }

            return 0.0f;
        }

        size_t AnimationSystem::ResolveSkeletonBoneCount(const AnimationComponent& component)
        {
            const Animation::Skeleton* l_Skeleton = Animation::AnimationAssetService::Get().GetSkeleton(component.m_SkeletonAssetHandle);
            if (l_Skeleton != nullptr && !l_Skeleton->m_Bones.empty())
            {
                return l_Skeleton->m_Bones.size();
            }

            // Fall back to the cached pose size to avoid reallocating every frame.
            if (!component.m_BoneMatrices.empty())
            {
                return component.m_BoneMatrices.size();
            }

            return 1;
        }

        void AnimationSystem::SampleClipPose(const AnimationComponent& component, float sampleTime, std::vector<glm::mat4>& outBoneMatrices)
        {
            Animation::AnimationAssetService& l_Service = Animation::AnimationAssetService::Get();
            const Animation::Skeleton* l_Skeleton = l_Service.GetSkeleton(component.m_SkeletonAssetHandle);
            const Animation::AnimationClip* l_Clip = l_Service.GetClip(component.m_AnimationAssetHandle, component.m_CurrentClipIndex);

            if (l_Skeleton == nullptr || l_Skeleton->m_Bones.empty())
            {
                const size_t l_FallbackCount = std::max<size_t>(component.m_BoneMatrices.empty() ? 1 : component.m_BoneMatrices.size(), 1);
                outBoneMatrices.assign(l_FallbackCount, glm::mat4{ 1.0f });
                return;
            }

            const size_t l_BoneCount = l_Skeleton->m_Bones.size();
            outBoneMatrices.resize(l_BoneCount, glm::mat4{ 1.0f });

            std::vector<glm::vec3> l_Translations(l_BoneCount);
            std::vector<glm::quat> l_Rotations(l_BoneCount);
            std::vector<glm::vec3> l_Scales(l_BoneCount);

            for (size_t it_Bone = 0; it_Bone < l_BoneCount; ++it_Bone)
            {
                const Animation::Bone& l_Bone = l_Skeleton->m_Bones[it_Bone];
                const TransformDecomposition l_Default = DecomposeBindTransform(l_Bone);
                l_Translations[it_Bone] = l_Default.m_Translation;
                l_Rotations[it_Bone] = l_Default.m_Rotation;
                l_Scales[it_Bone] = l_Default.m_Scale;
            }

            if (l_Clip != nullptr)
            {
                for (const Animation::TransformChannel& it_Channel : l_Clip->m_Channels)
                {
                    if (it_Channel.m_BoneIndex < 0)
                    {
                        continue;
                    }

                    const size_t l_BoneIndex = static_cast<size_t>(it_Channel.m_BoneIndex);
                    if (l_BoneIndex >= l_BoneCount)
                    {
                        continue;
                    }

                    l_Translations[l_BoneIndex] = SampleVectorKeys(it_Channel.m_TranslationKeys, sampleTime, l_Translations[l_BoneIndex]);
                    l_Rotations[l_BoneIndex] = SampleQuaternionKeys(it_Channel.m_RotationKeys, sampleTime, l_Rotations[l_BoneIndex]);
                    l_Scales[l_BoneIndex] = SampleVectorKeys(it_Channel.m_ScaleKeys, sampleTime, l_Scales[l_BoneIndex]);
                }
            }

            std::vector<glm::mat4> l_LocalTransforms(l_BoneCount, glm::mat4{ 1.0f });
            for (size_t it_Bone = 0; it_Bone < l_BoneCount; ++it_Bone)
            {
                const glm::mat4 l_TranslationMatrix = glm::translate(glm::mat4{ 1.0f }, l_Translations[it_Bone]);
                const glm::mat4 l_RotationMatrix = glm::toMat4(glm::normalize(l_Rotations[it_Bone]));
                const glm::mat4 l_ScaleMatrix = glm::scale(glm::mat4{ 1.0f }, l_Scales[it_Bone]);
                l_LocalTransforms[it_Bone] = l_TranslationMatrix * l_RotationMatrix * l_ScaleMatrix;
            }

            std::vector<glm::mat4> l_GlobalTransforms(l_BoneCount, glm::mat4{ 1.0f });

            struct WorkItem
            {
                int m_BoneIndex = -1;
                glm::mat4 m_ParentMatrix{ 1.0f };
            };

            std::vector<WorkItem> l_Worklist{};
            if (l_Skeleton->m_RootBoneIndex >= 0 && static_cast<size_t>(l_Skeleton->m_RootBoneIndex) < l_BoneCount)
            {
                l_Worklist.push_back({ l_Skeleton->m_RootBoneIndex, glm::mat4{ 1.0f } });
            }
            else
            {
                for (size_t it_Bone = 0; it_Bone < l_BoneCount; ++it_Bone)
                {
                    const Animation::Bone& l_Bone = l_Skeleton->m_Bones[it_Bone];
                    if (l_Bone.m_ParentIndex < 0)
                    {
                        l_Worklist.push_back({ static_cast<int>(it_Bone), glm::mat4{ 1.0f } });
                    }
                }

                if (l_Worklist.empty())
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

                const Animation::Bone& l_Bone = l_Skeleton->m_Bones[l_Index];
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

                const glm::mat4& l_InverseBind = l_Skeleton->m_Bones[it_Bone].m_InverseBindMatrix;
                outBoneMatrices[it_Bone] = l_GlobalTransforms[it_Bone] * l_InverseBind;
            }

            // Future work: blend multiple clips or feed state machines once the editor exposes authoring tools.
        }
    }
}