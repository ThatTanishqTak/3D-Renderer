#include "Animation/AnimationAssetService.h"

#include "Core/Utilities.h"
#include "Loader/ModelLoader.h"

#include <utility>

namespace Trident
{
    namespace Animation
    {
        namespace
        {
            /// @brief Helper creating readable traces when assets fail to load.
            void ReportMissingAsset(const std::string& assetId)
            {
                TR_CORE_WARN("Animation asset '{}' could not be loaded. Falling back to identity pose.", assetId.c_str());
            }
        }

        AnimationAssetService& AnimationAssetService::Get()
        {
            static AnimationAssetService s_Instance{};
            return s_Instance;
        }

        size_t AnimationAssetService::AcquireSkeleton(const std::string& skeletonAssetId)
        {
            if (skeletonAssetId.empty())
            {
                return s_InvalidHandle;
            }

            AssetRecord* l_Record = LoadAssetIfNeeded(skeletonAssetId);
            if (l_Record == nullptr)
            {
                ReportMissingAsset(skeletonAssetId);
                return s_InvalidHandle;
            }

            return l_Record->m_Handle;
        }

        size_t AnimationAssetService::AcquireAnimationLibrary(const std::string& animationAssetId)
        {
            if (animationAssetId.empty())
            {
                return s_InvalidHandle;
            }

            AssetRecord* l_Record = LoadAssetIfNeeded(animationAssetId);
            if (l_Record == nullptr)
            {
                ReportMissingAsset(animationAssetId);
                return s_InvalidHandle;
            }

            return l_Record->m_Handle;
        }

        size_t AnimationAssetService::ResolveClipIndex(size_t animationHandle, const std::string& clipName) const
        {
            if (animationHandle == s_InvalidHandle || clipName.empty())
            {
                return s_InvalidHandle;
            }

            auto a_RecordIt = m_Assets.find(animationHandle);
            if (a_RecordIt == m_Assets.end())
            {
                return s_InvalidHandle;
            }

            const AssetRecord& l_Record = a_RecordIt->second;
            auto a_ClipIt = l_Record.m_ClipLookup.find(clipName);
            if (a_ClipIt == l_Record.m_ClipLookup.end())
            {
                TR_CORE_WARN("Clip '{}' was not found inside animation asset '{}'.", clipName.c_str(), l_Record.m_AssetId.c_str());
                return s_InvalidHandle;
            }

            return a_ClipIt->second;
        }

        const Skeleton* AnimationAssetService::GetSkeleton(size_t skeletonHandle) const
        {
            if (skeletonHandle == s_InvalidHandle)
            {
                return nullptr;
            }

            auto a_RecordIt = m_Assets.find(skeletonHandle);
            if (a_RecordIt == m_Assets.end())
            {
                return nullptr;
            }

            return &a_RecordIt->second.m_Skeleton;
        }

        const std::vector<AnimationClip>* AnimationAssetService::GetAnimationClips(size_t animationHandle) const
        {
            if (animationHandle == s_InvalidHandle)
            {
                return nullptr;
            }

            auto a_RecordIt = m_Assets.find(animationHandle);
            if (a_RecordIt == m_Assets.end())
            {
                return nullptr;
            }

            return &a_RecordIt->second.m_Clips;
        }

        const AnimationClip* AnimationAssetService::GetClip(size_t animationHandle, size_t clipIndex) const
        {
            const std::vector<AnimationClip>* l_Clips = GetAnimationClips(animationHandle);
            if (l_Clips == nullptr)
            {
                return nullptr;
            }

            if (clipIndex == s_InvalidHandle || clipIndex >= l_Clips->size())
            {
                return nullptr;
            }

            return &(*l_Clips)[clipIndex];
        }

        AnimationAssetService::AssetRecord* AnimationAssetService::LoadAssetIfNeeded(const std::string& assetId)
        {
            auto a_Existing = m_IdToHandle.find(assetId);
            if (a_Existing != m_IdToHandle.end())
            {
                size_t l_Handle = a_Existing->second;
                auto a_RecordIt = m_Assets.find(l_Handle);
                if (a_RecordIt != m_Assets.end())
                {
                    return &a_RecordIt->second;
                }
            }

            Loader::ModelData l_ModelData = Loader::ModelLoader::Load(assetId);
            if (l_ModelData.m_Skeleton.m_Bones.empty() && l_ModelData.m_AnimationClips.empty())
            {
                return nullptr;
            }

            size_t l_Handle = m_NextHandle++;
            AssetRecord l_Record{};
            l_Record.m_AssetId = assetId;
            l_Record.m_Handle = l_Handle;
            l_Record.m_Skeleton = std::move(l_ModelData.m_Skeleton);
            l_Record.m_Clips = std::move(l_ModelData.m_AnimationClips);

            for (size_t it_Index = 0; it_Index < l_Record.m_Clips.size(); ++it_Index)
            {
                const AnimationClip& l_Clip = l_Record.m_Clips[it_Index];
                l_Record.m_ClipLookup.emplace(l_Clip.m_Name, it_Index);
            }

            auto a_InsertResult = m_Assets.emplace(l_Handle, std::move(l_Record));
            if (!a_InsertResult.second)
            {
                return nullptr;
            }

            m_IdToHandle[assetId] = l_Handle;
            return &a_InsertResult.first->second;
        }
    }
}