#include "Loader/ModelLoader.h"

#include "Core/Utilities.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Trident
{
    namespace Loader
    {
        namespace
        {
            constexpr unsigned int s_AssimpFlags =
                aiProcess_Triangulate |
                aiProcess_JoinIdenticalVertices |
                aiProcess_CalcTangentSpace |
                aiProcess_GenSmoothNormals |
                aiProcess_LimitBoneWeights |
                aiProcess_ImproveCacheLocality |
                aiProcess_RemoveRedundantMaterials |
                aiProcess_SortByPType |
                aiProcess_GenUVCoords |
                aiProcess_OptimizeMeshes |
                aiProcess_ValidateDataStructure;

            glm::mat4 ConvertMatrix(const aiMatrix4x4& a_Matrix)
            {
                glm::mat4 l_Matrix = glm::transpose(glm::make_mat4(&a_Matrix.a1));
                return l_Matrix;
            }

            glm::vec3 ConvertVector(const aiVector3D& a_Vector)
            {
                return { a_Vector.x, a_Vector.y, a_Vector.z };
            }

            glm::vec4 ConvertColor(const aiColor4D& a_Color)
            {
                return { a_Color.r, a_Color.g, a_Color.b, a_Color.a };
            }

            std::string NormalizeBoneName(std::string a_Name)
            {
                auto a_IsSpace = [](unsigned char a_Char)
                    {
                        return std::isspace(a_Char) != 0;
                    };

                a_Name.erase(a_Name.begin(), std::find_if(a_Name.begin(), a_Name.end(), [a_IsSpace](unsigned char a_Char)
                    {
                        return !a_IsSpace(a_Char);
                    }));
                a_Name.erase(std::find_if(a_Name.rbegin(), a_Name.rend(), [a_IsSpace](unsigned char a_Char)
                    {
                        return !a_IsSpace(a_Char);
                    }).base(), a_Name.end());

                constexpr std::string_view s_MixamoPrefix = "mixamorig:";
                if (a_Name.size() > s_MixamoPrefix.size() && a_Name.compare(0, s_MixamoPrefix.size(), s_MixamoPrefix) == 0)
                {
                    a_Name.erase(0, s_MixamoPrefix.size());
                }

                return a_Name;
            }

            const aiNode* FindNode(const std::unordered_map<std::string, const aiNode*>& a_NodeLookup, const std::string& a_Name)
            {
                auto a_It = a_NodeLookup.find(a_Name);
                return a_It != a_NodeLookup.end() ? a_It->second : nullptr;
            }

            void AssignBoneWeight(Vertex& a_Vertex, int a_BoneIndex, float a_Weight)
            {
                if (a_BoneIndex < 0 || a_Weight <= 0.0f)
                {
                    return;
                }

                for (int it_Index = 0; it_Index < static_cast<int>(Vertex::MaxBoneInfluences); ++it_Index)
                {
                    if (a_Vertex.m_BoneWeights[it_Index] <= 0.0f)
                    {
                        a_Vertex.m_BoneIndices[it_Index] = a_BoneIndex;
                        a_Vertex.m_BoneWeights[it_Index] = a_Weight;
                        return;
                    }
                }

                int l_MinIndex = 0;
                float l_MinWeight = a_Vertex.m_BoneWeights[0];
                for (int it_Index = 1; it_Index < static_cast<int>(Vertex::MaxBoneInfluences); ++it_Index)
                {
                    if (a_Vertex.m_BoneWeights[it_Index] < l_MinWeight)
                    {
                        l_MinWeight = a_Vertex.m_BoneWeights[it_Index];
                        l_MinIndex = it_Index;
                    }
                }

                if (a_Weight > l_MinWeight)
                {
                    a_Vertex.m_BoneIndices[l_MinIndex] = a_BoneIndex;
                    a_Vertex.m_BoneWeights[l_MinIndex] = a_Weight;
                }
            }

            void NormaliseBoneWeights(Vertex& a_Vertex)
            {
                const float l_TotalWeight = a_Vertex.m_BoneWeights.x + a_Vertex.m_BoneWeights.y + a_Vertex.m_BoneWeights.z + a_Vertex.m_BoneWeights.w;
                if (l_TotalWeight <= 0.0f)
                {
                    return;
                }

                const float l_InvTotal = 1.0f / l_TotalWeight;
                for (int it_Index = 0; it_Index < static_cast<int>(Vertex::MaxBoneInfluences); ++it_Index)
                {
                    a_Vertex.m_BoneWeights[it_Index] *= l_InvTotal;
                }
            }

            int EnsureBoneExists(const std::string& a_SourceName,
                Animation::Skeleton& a_Skeleton,
                const std::unordered_map<std::string, const aiNode*>& a_NodeLookup,
                std::unordered_map<std::string, int>& a_BoneLookup)
            {
                if (a_SourceName.empty())
                {
                    return -1;
                }

                auto a_Existing = a_BoneLookup.find(a_SourceName);
                if (a_Existing != a_BoneLookup.end())
                {
                    return a_Existing->second;
                }

                Animation::Bone l_Bone{};
                l_Bone.m_SourceName = a_SourceName;
                l_Bone.m_Name = NormalizeBoneName(a_SourceName);
                l_Bone.m_LocalBindTransform = glm::mat4(1.0f);
                l_Bone.m_InverseBindMatrix = glm::mat4(1.0f);

                if (const aiNode* l_Node = FindNode(a_NodeLookup, a_SourceName))
                {
                    l_Bone.m_LocalBindTransform = ConvertMatrix(l_Node->mTransformation);
                }

                const int l_NewIndex = static_cast<int>(a_Skeleton.m_Bones.size());
                a_Skeleton.m_Bones.push_back(l_Bone);
                a_Skeleton.m_NameToIndex[l_Bone.m_Name] = l_NewIndex;
                a_Skeleton.m_SourceNameToIndex[l_Bone.m_SourceName] = l_NewIndex;
                a_BoneLookup.emplace(a_SourceName, l_NewIndex);
                return l_NewIndex;
            }

            int ResolveTextureIndex(const aiMaterial* a_Material,
                aiTextureType a_Type,
                const std::filesystem::path& a_ModelDirectory,
                std::vector<std::string>& a_Textures,
                std::unordered_map<std::string, int>& a_TextureLookup)
            {
                aiString l_Path{};
                if (a_Material->GetTexture(a_Type, 0, &l_Path) != AI_SUCCESS)
                {
                    return -1;
                }

                if (l_Path.length == 0)
                {
                    return -1;
                }

                std::filesystem::path l_TexturePath{ l_Path.C_Str() };
                if (l_TexturePath.empty() || l_TexturePath.string().front() == '*')
                {
                    TR_CORE_WARN("Embedded textures are not supported ({}).", l_Path.C_Str());
                    return -1;
                }

                if (!l_TexturePath.is_absolute())
                {
                    l_TexturePath = a_ModelDirectory / l_TexturePath;
                }

                std::string l_Normalised = Utilities::FileManagement::NormalizePath(l_TexturePath.string());
                if (l_Normalised.empty())
                {
                    return -1;
                }

                auto a_Found = a_TextureLookup.find(l_Normalised);
                if (a_Found != a_TextureLookup.end())
                {
                    return a_Found->second;
                }

                const int l_NewIndex = static_cast<int>(a_Textures.size());
                a_Textures.push_back(l_Normalised);
                a_TextureLookup.emplace(l_Normalised, l_NewIndex);
                return l_NewIndex;
            }

            void FinaliseSkeletonHierarchy(Animation::Skeleton& a_Skeleton,
                const std::unordered_map<std::string, const aiNode*>& a_NodeLookup,
                const std::unordered_map<std::string, int>& a_BoneLookup)
            {
                for (size_t it_Index = 0; it_Index < a_Skeleton.m_Bones.size(); ++it_Index)
                {
                    Animation::Bone& l_Bone = a_Skeleton.m_Bones[it_Index];
                    if (l_Bone.m_SourceName.empty())
                    {
                        continue;
                    }

                    const aiNode* l_Node = FindNode(a_NodeLookup, l_Bone.m_SourceName);
                    const aiNode* l_ParentNode = l_Node != nullptr ? l_Node->mParent : nullptr;

                    int l_ParentIndex = -1;
                    while (l_ParentNode != nullptr)
                    {
                        std::string l_ParentName = l_ParentNode->mName.C_Str();
                        auto a_ParentIt = a_BoneLookup.find(l_ParentName);
                        if (a_ParentIt != a_BoneLookup.end())
                        {
                            l_ParentIndex = a_ParentIt->second;
                            break;
                        }

                        l_ParentNode = l_ParentNode->mParent;
                    }

                    l_Bone.m_ParentIndex = l_ParentIndex;
                    if (l_ParentIndex >= 0)
                    {
                        Animation::Bone& l_Parent = a_Skeleton.m_Bones[static_cast<size_t>(l_ParentIndex)];
                        l_Parent.m_Children.push_back(static_cast<int>(it_Index));
                    }
                    else if (a_Skeleton.m_RootBoneIndex < 0)
                    {
                        a_Skeleton.m_RootBoneIndex = static_cast<int>(it_Index);
                    }
                }
            }
        }

        ModelData ModelLoader::Load(const std::string& filePath)
        {
            ModelData l_ModelData{};
            if (filePath.empty())
            {
                TR_CORE_WARN("ModelLoader::Load received an empty file path.");
                return l_ModelData;
            }

            std::string l_NormalisedPath = Utilities::FileManagement::NormalizePath(filePath);
            if (l_NormalisedPath.empty())
            {
                TR_CORE_ERROR("Failed to normalise model path: {}", filePath.c_str());
                return l_ModelData;
            }

            std::filesystem::path l_ModelPath{ l_NormalisedPath };
            if (!std::filesystem::exists(l_ModelPath))
            {
                TR_CORE_ERROR("Model file not found: {}", l_NormalisedPath.c_str());
                return l_ModelData;
            }

            std::filesystem::path l_ModelDirectory = l_ModelPath.parent_path();

            Assimp::Importer l_Importer{};
            const aiScene* l_Scene = l_Importer.ReadFile(l_NormalisedPath, s_AssimpFlags);
            if (l_Scene == nullptr || (l_Scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0)
            {
                TR_CORE_ERROR("Assimp failed to import '{}': {}", l_NormalisedPath.c_str(), l_Importer.GetErrorString());
                return l_ModelData;
            }

            std::unordered_map<std::string, const aiNode*> l_NodeLookup{};
            std::function<void(const aiNode*)> l_PopulateNodeLookup = [&](const aiNode* a_Node)
                {
                    if (a_Node == nullptr)
                    {
                        return;
                    }

                    l_NodeLookup.emplace(a_Node->mName.C_Str(), a_Node);
                    for (unsigned int it_Child = 0; it_Child < a_Node->mNumChildren; ++it_Child)
                    {
                        l_PopulateNodeLookup(a_Node->mChildren[it_Child]);
                    }
                };
            l_PopulateNodeLookup(l_Scene->mRootNode);

            std::unordered_map<std::string, int> l_BoneLookup{};

            std::unordered_map<std::string, int> l_TextureLookup{};
            l_ModelData.m_Textures.reserve(static_cast<size_t>(l_Scene->mNumMaterials));
            l_ModelData.m_Materials.reserve(static_cast<size_t>(l_Scene->mNumMaterials));

            for (unsigned int it_Material = 0; it_Material < l_Scene->mNumMaterials; ++it_Material)
            {
                const aiMaterial* l_AssimpMaterial = l_Scene->mMaterials[it_Material];
                if (l_AssimpMaterial == nullptr)
                {
                    continue;
                }

                Geometry::Material l_Material{};

                aiColor4D l_BaseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
                if (l_AssimpMaterial->Get(AI_MATKEY_BASE_COLOR, l_BaseColor) != AI_SUCCESS)
                {
                    l_AssimpMaterial->Get(AI_MATKEY_COLOR_DIFFUSE, l_BaseColor);
                }
                l_Material.BaseColorFactor = ConvertColor(l_BaseColor);

                float l_Metallic = 1.0f;
                float l_Roughness = 1.0f;
                l_AssimpMaterial->Get(AI_MATKEY_METALLIC_FACTOR, l_Metallic);
                l_AssimpMaterial->Get(AI_MATKEY_ROUGHNESS_FACTOR, l_Roughness);
                l_Material.MetallicFactor = l_Metallic;
                l_Material.RoughnessFactor = l_Roughness;

                l_Material.BaseColorTextureIndex = ResolveTextureIndex(l_AssimpMaterial, aiTextureType_BASE_COLOR, l_ModelDirectory, l_ModelData.m_Textures, l_TextureLookup);
                if (l_Material.BaseColorTextureIndex < 0)
                {
                    l_Material.BaseColorTextureIndex = ResolveTextureIndex(l_AssimpMaterial, aiTextureType_DIFFUSE, l_ModelDirectory, l_ModelData.m_Textures, l_TextureLookup);
                }

                l_Material.MetallicRoughnessTextureIndex = ResolveTextureIndex(l_AssimpMaterial, aiTextureType_METALNESS, l_ModelDirectory, l_ModelData.m_Textures, l_TextureLookup);
                if (l_Material.MetallicRoughnessTextureIndex < 0)
                {
                    l_Material.MetallicRoughnessTextureIndex = ResolveTextureIndex(l_AssimpMaterial, aiTextureType_DIFFUSE_ROUGHNESS, l_ModelDirectory, l_ModelData.m_Textures, l_TextureLookup);
                }

                l_Material.NormalTextureIndex = ResolveTextureIndex(l_AssimpMaterial, aiTextureType_NORMALS, l_ModelDirectory, l_ModelData.m_Textures, l_TextureLookup);

                l_ModelData.m_Materials.emplace_back(std::move(l_Material));
            }

            l_ModelData.m_Meshes.reserve(static_cast<size_t>(l_Scene->mNumMeshes));
            std::vector<size_t> l_MeshIndexMap(l_Scene->mNumMeshes, std::numeric_limits<size_t>::max());

            for (unsigned int it_Mesh = 0; it_Mesh < l_Scene->mNumMeshes; ++it_Mesh)
            {
                const aiMesh* l_AssimpMesh = l_Scene->mMeshes[it_Mesh];
                if (l_AssimpMesh == nullptr)
                {
                    continue;
                }

                Geometry::Mesh l_Mesh{};
                l_Mesh.MaterialIndex = l_AssimpMesh->mMaterialIndex >= 0 ? static_cast<int>(l_AssimpMesh->mMaterialIndex) : -1;
                l_Mesh.Vertices.resize(static_cast<size_t>(l_AssimpMesh->mNumVertices));

                for (unsigned int it_Vertex = 0; it_Vertex < l_AssimpMesh->mNumVertices; ++it_Vertex)
                {
                    Vertex& l_Vertex = l_Mesh.Vertices[it_Vertex];
                    if (l_AssimpMesh->HasPositions())
                    {
                        l_Vertex.Position = ConvertVector(l_AssimpMesh->mVertices[it_Vertex]);
                    }
                    if (l_AssimpMesh->HasNormals())
                    {
                        l_Vertex.Normal = ConvertVector(l_AssimpMesh->mNormals[it_Vertex]);
                    }
                    if (l_AssimpMesh->HasTangentsAndBitangents())
                    {
                        l_Vertex.Tangent = ConvertVector(l_AssimpMesh->mTangents[it_Vertex]);
                        l_Vertex.Bitangent = ConvertVector(l_AssimpMesh->mBitangents[it_Vertex]);
                    }
                    if (l_AssimpMesh->HasTextureCoords(0))
                    {
                        l_Vertex.TexCoord = { l_AssimpMesh->mTextureCoords[0][it_Vertex].x, l_AssimpMesh->mTextureCoords[0][it_Vertex].y };
                    }
                    if (l_AssimpMesh->HasVertexColors(0))
                    {
                        l_Vertex.Color = { l_AssimpMesh->mColors[0][it_Vertex].r, l_AssimpMesh->mColors[0][it_Vertex].g, l_AssimpMesh->mColors[0][it_Vertex].b };
                    }
                    else
                    {
                        l_Vertex.Color = glm::vec3(1.0f);
                    }
                }

                l_Mesh.Indices.reserve(static_cast<size_t>(l_AssimpMesh->mNumFaces) * 3);
                for (unsigned int it_Face = 0; it_Face < l_AssimpMesh->mNumFaces; ++it_Face)
                {
                    const aiFace& l_Face = l_AssimpMesh->mFaces[it_Face];
                    if (l_Face.mNumIndices != 3)
                    {
                        continue;
                    }

                    l_Mesh.Indices.push_back(l_Face.mIndices[0]);
                    l_Mesh.Indices.push_back(l_Face.mIndices[1]);
                    l_Mesh.Indices.push_back(l_Face.mIndices[2]);
                }

                if (l_AssimpMesh->HasBones())
                {
                    for (unsigned int it_Bone = 0; it_Bone < l_AssimpMesh->mNumBones; ++it_Bone)
                    {
                        const aiBone* l_AssimpBone = l_AssimpMesh->mBones[it_Bone];
                        if (l_AssimpBone == nullptr)
                        {
                            continue;
                        }

                        const std::string l_BoneName = l_AssimpBone->mName.C_Str();
                        const int l_BoneIndex = EnsureBoneExists(l_BoneName, l_ModelData.m_Skeleton, l_NodeLookup, l_BoneLookup);
                        if (l_BoneIndex < 0)
                        {
                            continue;
                        }

                        Animation::Bone& l_Bone = l_ModelData.m_Skeleton.m_Bones[static_cast<size_t>(l_BoneIndex)];
                        l_Bone.m_InverseBindMatrix = ConvertMatrix(l_AssimpBone->mOffsetMatrix);

                        for (unsigned int it_Weight = 0; it_Weight < l_AssimpBone->mNumWeights; ++it_Weight)
                        {
                            const aiVertexWeight& l_Weight = l_AssimpBone->mWeights[it_Weight];
                            if (l_Weight.mVertexId >= l_AssimpMesh->mNumVertices)
                            {
                                continue;
                            }

                            Vertex& l_Vertex = l_Mesh.Vertices[l_Weight.mVertexId];
                            AssignBoneWeight(l_Vertex, l_BoneIndex, l_Weight.mWeight);
                        }
                    }
                }

                for (Vertex& it_Vertex : l_Mesh.Vertices)
                {
                    NormaliseBoneWeights(it_Vertex);
                }

                l_MeshIndexMap[it_Mesh] = l_ModelData.m_Meshes.size();
                l_ModelData.m_Meshes.emplace_back(std::move(l_Mesh));
            }

            l_ModelData.m_MeshInstances.clear();
            l_ModelData.m_MeshInstances.reserve(static_cast<size_t>(l_Scene->mNumMeshes));

            const std::function<void(const aiNode*, const glm::mat4&)> l_VisitNode = [&](const aiNode* a_Node, const glm::mat4& a_ParentTransform)
                {
                    if (a_Node == nullptr)
                    {
                        return;
                    }

                    const glm::mat4 l_Local = ConvertMatrix(a_Node->mTransformation);
                    const glm::mat4 l_ModelMatrix = a_ParentTransform * l_Local;

                    for (unsigned int it_MeshIndex = 0; it_MeshIndex < a_Node->mNumMeshes; ++it_MeshIndex)
                    {
                        const unsigned int l_AssimpMeshIndex = a_Node->mMeshes[it_MeshIndex];
                        if (l_AssimpMeshIndex >= l_MeshIndexMap.size())
                        {
                            continue;
                        }

                        const size_t l_ModelMeshIndex = l_MeshIndexMap[l_AssimpMeshIndex];
                        if (l_ModelMeshIndex == std::numeric_limits<size_t>::max())
                        {
                            continue;
                        }

                        MeshInstance l_Instance{};
                        l_Instance.m_MeshIndex = l_ModelMeshIndex;
                        l_Instance.m_ModelMatrix = l_ModelMatrix;
                        l_Instance.m_NodeName = a_Node->mName.C_Str();
                        l_ModelData.m_MeshInstances.emplace_back(std::move(l_Instance));
                    }

                    for (unsigned int it_Child = 0; it_Child < a_Node->mNumChildren; ++it_Child)
                    {
                        l_VisitNode(a_Node->mChildren[it_Child], l_ModelMatrix);
                    }
                };
            l_VisitNode(l_Scene->mRootNode, glm::mat4(1.0f));

            l_ModelData.m_AnimationClips.reserve(static_cast<size_t>(l_Scene->mNumAnimations));
            for (unsigned int it_Animation = 0; it_Animation < l_Scene->mNumAnimations; ++it_Animation)
            {
                const aiAnimation* l_AssimpAnimation = l_Scene->mAnimations[it_Animation];
                if (l_AssimpAnimation == nullptr)
                {
                    continue;
                }

                Animation::AnimationClip l_Clip{};
                l_Clip.m_Name = l_AssimpAnimation->mName.C_Str();
                if (l_Clip.m_Name.empty())
                {
                    l_Clip.m_Name = "Clip" + std::to_string(it_Animation);
                }

                const double l_TicksPerSecond = l_AssimpAnimation->mTicksPerSecond > 0.0 ? l_AssimpAnimation->mTicksPerSecond : 25.0;
                l_Clip.m_TicksPerSecond = static_cast<float>(l_TicksPerSecond);
                const double l_DurationSeconds = l_TicksPerSecond > 0.0 ? l_AssimpAnimation->mDuration / l_TicksPerSecond : 0.0;
                l_Clip.m_DurationSeconds = static_cast<float>(l_DurationSeconds);

                l_Clip.m_Channels.reserve(static_cast<size_t>(l_AssimpAnimation->mNumChannels));
                for (unsigned int it_Channel = 0; it_Channel < l_AssimpAnimation->mNumChannels; ++it_Channel)
                {
                    const aiNodeAnim* l_AssimpChannel = l_AssimpAnimation->mChannels[it_Channel];
                    if (l_AssimpChannel == nullptr)
                    {
                        continue;
                    }

                    const std::string l_ChannelBoneName = l_AssimpChannel->mNodeName.C_Str();
                    const int l_BoneIndex = EnsureBoneExists(l_ChannelBoneName, l_ModelData.m_Skeleton, l_NodeLookup, l_BoneLookup);
                    if (l_BoneIndex < 0)
                    {
                        continue;
                    }

                    Animation::TransformChannel l_Channel{};
                    l_Channel.m_BoneIndex = l_BoneIndex;

                    l_Channel.m_TranslationKeys.reserve(static_cast<size_t>(l_AssimpChannel->mNumPositionKeys));
                    for (unsigned int it_Key = 0; it_Key < l_AssimpChannel->mNumPositionKeys; ++it_Key)
                    {
                        const aiVectorKey& l_Key = l_AssimpChannel->mPositionKeys[it_Key];
                        Animation::VectorKeyframe l_Keyframe{};
                        l_Keyframe.m_TimeSeconds = static_cast<float>(l_Key.mTime / l_TicksPerSecond);
                        l_Keyframe.m_Value = ConvertVector(l_Key.mValue);
                        l_Channel.m_TranslationKeys.emplace_back(std::move(l_Keyframe));
                    }

                    l_Channel.m_RotationKeys.reserve(static_cast<size_t>(l_AssimpChannel->mNumRotationKeys));
                    for (unsigned int it_Key = 0; it_Key < l_AssimpChannel->mNumRotationKeys; ++it_Key)
                    {
                        const aiQuatKey& l_Key = l_AssimpChannel->mRotationKeys[it_Key];
                        Animation::QuaternionKeyframe l_Keyframe{};
                        l_Keyframe.m_TimeSeconds = static_cast<float>(l_Key.mTime / l_TicksPerSecond);
                        const aiQuaternion& l_Quat = l_Key.mValue;
                        l_Keyframe.m_Value = glm::quat(l_Quat.w, l_Quat.x, l_Quat.y, l_Quat.z);
                        l_Channel.m_RotationKeys.emplace_back(std::move(l_Keyframe));
                    }

                    l_Channel.m_ScaleKeys.reserve(static_cast<size_t>(l_AssimpChannel->mNumScalingKeys));
                    for (unsigned int it_Key = 0; it_Key < l_AssimpChannel->mNumScalingKeys; ++it_Key)
                    {
                        const aiVectorKey& l_Key = l_AssimpChannel->mScalingKeys[it_Key];
                        Animation::VectorKeyframe l_Keyframe{};
                        l_Keyframe.m_TimeSeconds = static_cast<float>(l_Key.mTime / l_TicksPerSecond);
                        l_Keyframe.m_Value = ConvertVector(l_Key.mValue);
                        l_Channel.m_ScaleKeys.emplace_back(std::move(l_Keyframe));
                    }

                    l_Clip.m_Channels.emplace_back(std::move(l_Channel));
                }

                l_ModelData.m_AnimationClips.emplace_back(std::move(l_Clip));
            }

            FinaliseSkeletonHierarchy(l_ModelData.m_Skeleton, l_NodeLookup, l_BoneLookup);

            if (l_ModelData.m_Skeleton.m_RootBoneIndex < 0 && !l_ModelData.m_Skeleton.m_Bones.empty())
            {
                l_ModelData.m_Skeleton.m_RootBoneIndex = 0;
            }

            return l_ModelData;
        }
    }
}