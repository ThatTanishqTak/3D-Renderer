#include "Loader/ModelLoader.h"

#include "Core/Utilities.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <algorithm>
#include <cctype>
#include <functional>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Trident
{
    namespace Loader
    {
        namespace
        {
            const glm::vec3 s_DefaultNormal{ 0.0f, 1.0f, 0.0f };
            const glm::vec3 s_DefaultTangent{ 1.0f, 0.0f, 0.0f };
            const glm::vec3 s_DefaultBitangent{ 0.0f, 0.0f, 1.0f };
            const glm::vec3 s_DefaultColor{ 1.0f, 1.0f, 1.0f };
            const glm::vec2 s_DefaultTexCoord{ 0.0f, 0.0f };

            glm::mat4 ConvertMatrix(const aiMatrix4x4& source)
            {
                glm::mat4 l_Target{};
                l_Target[0][0] = source.a1; l_Target[1][0] = source.a2; l_Target[2][0] = source.a3; l_Target[3][0] = source.a4;
                l_Target[0][1] = source.b1; l_Target[1][1] = source.b2; l_Target[2][1] = source.b3; l_Target[3][1] = source.b4;
                l_Target[0][2] = source.c1; l_Target[1][2] = source.c2; l_Target[2][2] = source.c3; l_Target[3][2] = source.c4;
                l_Target[0][3] = source.d1; l_Target[1][3] = source.d2; l_Target[2][3] = source.d3; l_Target[3][3] = source.d4;
                return l_Target;
            }

            std::string NormalizeMixamoName(const std::string& sourceName)
            {
                std::string l_Normalized = sourceName;
                const std::string l_MixamoPrefix = "mixamorig:";
                if (l_Normalized.rfind(l_MixamoPrefix, 0) == 0)
                {
                    l_Normalized = l_Normalized.substr(l_MixamoPrefix.size());
                }

                std::string l_Lowercase = l_Normalized;
                std::transform(l_Lowercase.begin(), l_Lowercase.end(), l_Lowercase.begin(), [](unsigned char character)
                    {
                        return static_cast<char>(std::tolower(character));
                    });

                static const std::unordered_map<std::string, std::string> s_MixamoCanonicalNames =
                {
                    { "hips", "Hips" },
                    { "spine", "Spine" },
                    { "spine1", "Spine1" },
                    { "spine2", "Spine2" },
                    { "neck", "Neck" },
                    { "head", "Head" },
                    { "leftshoulder", "LeftShoulder" },
                    { "leftarm", "LeftArm" },
                    { "leftforearm", "LeftForeArm" },
                    { "lefthand", "LeftHand" },
                    { "rightshoulder", "RightShoulder" },
                    { "rightarm", "RightArm" },
                    { "rightforearm", "RightForeArm" },
                    { "righthand", "RightHand" },
                    { "leftupleg", "LeftUpLeg" },
                    { "leftleg", "LeftLeg" },
                    { "leftfoot", "LeftFoot" },
                    { "lefttoe", "LeftToeBase" },
                    { "rightupleg", "RightUpLeg" },
                    { "rightleg", "RightLeg" },
                    { "rightfoot", "RightFoot" },
                    { "righttoe", "RightToeBase" }
                };

                auto a_Found = s_MixamoCanonicalNames.find(l_Lowercase);
                if (a_Found != s_MixamoCanonicalNames.end())
                {
                    return a_Found->second;
                }

                return l_Normalized;
            }
        }

        ModelData ModelLoader::Load(const std::string& filePath)
        {
            ModelData l_ModelData{};
            std::string l_NormalizedPath = Utilities::FileManagement::NormalizePath(filePath);
            if (l_NormalizedPath.empty())
            {
                TR_CORE_CRITICAL("Provided model path is empty");
                return l_ModelData;
            }

            Assimp::Importer l_Importer{};
            const unsigned int l_PostProcessFlags =
                aiProcess_Triangulate |          // Ensure all faces are triangles so the renderer can index predictably
                aiProcess_GenSmoothNormals |     // Generate smooth normals when the source omits them
                aiProcess_CalcTangentSpace |     // Derive tangents and bitangents for normal mapping support
                aiProcess_JoinIdenticalVertices |
                aiProcess_ImproveCacheLocality |
                aiProcess_RemoveRedundantMaterials |
                aiProcess_GenUVCoords |
                aiProcess_TransformUVCoords |
                aiProcess_SortByPType;

            const aiScene* l_Scene = l_Importer.ReadFile(l_NormalizedPath, l_PostProcessFlags);
            if (!l_Scene)
            {
                TR_CORE_CRITICAL("Assimp failed to load model '{}': {}", filePath, l_Importer.GetErrorString());
                return l_ModelData;
            }

            if (!l_Scene->mRootNode)
            {
                TR_CORE_CRITICAL("Model '{}' does not contain a root node", filePath);
                return l_ModelData;
            }

            std::string l_BaseDirectory = Utilities::FileManagement::GetBaseDirectory(l_NormalizedPath);
            std::unordered_map<std::string, int> l_TextureLookup{};
            Animation::Skeleton& l_Skeleton = l_ModelData.m_Skeleton;

            const std::function<int(const std::string&, const aiNode*, const glm::mat4&, bool)> l_AcquireBoneIndex =
                [&](const std::string& sourceName, const aiNode* assimpNode, const glm::mat4& inverseBindMatrix, bool updateInverseBind) -> int
                {
                    const std::string l_NormalizedName = NormalizeMixamoName(sourceName);
                    int l_BoneIndex = -1;

                    auto a_Existing = l_Skeleton.m_NameToIndex.find(l_NormalizedName);
                    if (a_Existing == l_Skeleton.m_NameToIndex.end())
                    {
                        Animation::Bone l_Bone{};
                        l_Bone.m_Name = l_NormalizedName;
                        l_Bone.m_SourceName = sourceName;
                        l_Bone.m_InverseBindMatrix = inverseBindMatrix;
                        if (assimpNode)
                        {
                            l_Bone.m_LocalBindTransform = ConvertMatrix(assimpNode->mTransformation);
                        }

                        l_Skeleton.m_Bones.push_back(l_Bone);
                        l_BoneIndex = static_cast<int>(l_Skeleton.m_Bones.size()) - 1;
                        l_Skeleton.m_NameToIndex.emplace(l_NormalizedName, l_BoneIndex);
                        l_Skeleton.m_SourceNameToIndex.emplace(sourceName, l_BoneIndex);
                    }
                    else
                    {
                        l_BoneIndex = a_Existing->second;
                        Animation::Bone& l_Bone = l_Skeleton.m_Bones[l_BoneIndex];
                        l_Bone.m_SourceName = sourceName;
                        if (updateInverseBind)
                        {
                            l_Bone.m_InverseBindMatrix = inverseBindMatrix;
                        }
                        if (assimpNode)
                        {
                            l_Bone.m_LocalBindTransform = ConvertMatrix(assimpNode->mTransformation);
                        }
                        l_Skeleton.m_SourceNameToIndex.emplace(sourceName, l_BoneIndex);
                    }

                    const aiNode* l_BoneNode = assimpNode;
                    if (!l_BoneNode && l_Scene && l_Scene->mRootNode)
                    {
                        aiString l_AssimpName{ sourceName.c_str() };
                        l_BoneNode = l_Scene->mRootNode->FindNode(l_AssimpName);
                    }

                    if (l_BoneNode && l_BoneNode->mParent)
                    {
                        std::string l_ParentSourceName = l_BoneNode->mParent->mName.C_Str();
                        const glm::mat4 l_Identity{ 1.0f };
                        const int l_ParentIndex = l_AcquireBoneIndex(l_ParentSourceName, l_BoneNode->mParent, l_Identity, false);

                        Animation::Bone& l_CurrentBone = l_Skeleton.m_Bones[l_BoneIndex];
                        if (l_CurrentBone.m_ParentIndex < 0)
                        {
                            l_CurrentBone.m_ParentIndex = l_ParentIndex;
                        }

                        Animation::Bone& l_ParentBone = l_Skeleton.m_Bones[l_ParentIndex];
                        if (std::find(l_ParentBone.m_Children.begin(), l_ParentBone.m_Children.end(), l_BoneIndex) == l_ParentBone.m_Children.end())
                        {
                            l_ParentBone.m_Children.push_back(l_BoneIndex);
                        }
                    }
                    else if (l_Skeleton.m_RootBoneIndex < 0)
                    {
                        l_Skeleton.m_RootBoneIndex = l_BoneIndex;
                    }

                    return l_BoneIndex;
                };


            // Helper that registers a texture path once and returns a stable index for all materials.
            const auto RegisterTexture =
                [&](const aiMaterial* sourceMaterial, aiTextureType a_Type) -> int
                {
                    if (!sourceMaterial || sourceMaterial->GetTextureCount(a_Type) == 0)
                    {
                        return -1;
                    }

                    aiString l_TexturePath{};
                    if (sourceMaterial->GetTexture(a_Type, 0, &l_TexturePath) != aiReturn_SUCCESS)
                    {
                        TR_CORE_WARN("Failed to read texture slot {} from material", static_cast<int>(a_Type));
                        return -1;
                    }

                    std::string l_PathString = l_TexturePath.C_Str();
                    if (l_PathString.empty())
                    {
                        return -1;
                    }

                    if (!l_PathString.empty() && l_PathString[0] == '*')
                    {
                        TR_CORE_WARN("Embedded textures are not yet supported ({}).", l_PathString.c_str());
                        // TODO: Handle embedded textures by extracting them into GPU-ready images.
                        return -1;
                    }

                    std::string l_ResolvedPath = l_BaseDirectory.empty()
                        ? Utilities::FileManagement::NormalizePath(l_PathString)
                        : Utilities::FileManagement::JoinPath(l_BaseDirectory, l_PathString);

                    auto a_Existing = l_TextureLookup.find(l_ResolvedPath);
                    if (a_Existing != l_TextureLookup.end())
                    {
                        return a_Existing->second;
                    }

                    const int l_NewIndex = static_cast<int>(l_ModelData.m_Textures.size());
                    l_ModelData.m_Textures.push_back(l_ResolvedPath);
                    l_TextureLookup.emplace(std::move(l_ResolvedPath), l_NewIndex);

                    return l_NewIndex;
                };

            // Convert every material into the renderer-friendly representation before visiting the scene graph.
            l_ModelData.m_Materials.reserve(l_Scene->mNumMaterials);
            for (unsigned int it_Material = 0; it_Material < l_Scene->mNumMaterials; ++it_Material)
            {
                const aiMaterial* l_AssimpMaterial = l_Scene->mMaterials[it_Material];
                Geometry::Material l_Material{};

                // Base color factor is preferred from PBR data, falling back to diffuse when unavailable.
                aiColor4D l_BaseColor{};
                if (l_AssimpMaterial && l_AssimpMaterial->Get(AI_MATKEY_BASE_COLOR, l_BaseColor) == aiReturn_SUCCESS)
                {
                    l_Material.BaseColorFactor = glm::vec4(l_BaseColor.r, l_BaseColor.g, l_BaseColor.b, l_BaseColor.a);
                }
                else if (l_AssimpMaterial && l_AssimpMaterial->Get(AI_MATKEY_COLOR_DIFFUSE, l_BaseColor) == aiReturn_SUCCESS)
                {
                    l_Material.BaseColorFactor = glm::vec4(l_BaseColor.r, l_BaseColor.g, l_BaseColor.b, l_BaseColor.a);
                }
                else
                {
                    l_Material.BaseColorFactor = glm::vec4(1.0f);
                }

                float l_Metallic = 1.0f;
                if (l_AssimpMaterial && l_AssimpMaterial->Get(AI_MATKEY_METALLIC_FACTOR, l_Metallic) != aiReturn_SUCCESS)
                {
                    // Legacy formats often store metallic information elsewhere, so we retain the default.
                    l_Metallic = 1.0f;
                }
                l_Material.MetallicFactor = l_Metallic;

                float l_Roughness = 1.0f;
                if (l_AssimpMaterial && l_AssimpMaterial->Get(AI_MATKEY_ROUGHNESS_FACTOR, l_Roughness) != aiReturn_SUCCESS)
                {
                    // Shininess maps can approximate roughness, but that conversion requires extra work.
                    // TODO: Convert AI_MATKEY_SHININESS into roughness for legacy Phong materials.
                    l_Roughness = 1.0f;
                }
                l_Material.RoughnessFactor = l_Roughness;

                // Texture slots come from modern PBR slots first, then fall back to classic equivalents.
                l_Material.BaseColorTextureIndex = RegisterTexture(l_AssimpMaterial, aiTextureType_BASE_COLOR);
                if (l_Material.BaseColorTextureIndex < 0)
                {
                    l_Material.BaseColorTextureIndex = RegisterTexture(l_AssimpMaterial, aiTextureType_DIFFUSE);
                }

                l_Material.MetallicRoughnessTextureIndex = RegisterTexture(l_AssimpMaterial, aiTextureType_METALNESS);
                if (l_Material.MetallicRoughnessTextureIndex < 0)
                {
                    l_Material.MetallicRoughnessTextureIndex = RegisterTexture(l_AssimpMaterial, aiTextureType_DIFFUSE_ROUGHNESS);
                }
                if (l_Material.MetallicRoughnessTextureIndex < 0)
                {
                    l_Material.MetallicRoughnessTextureIndex = RegisterTexture(l_AssimpMaterial, aiTextureType_SPECULAR);
                }

                l_Material.NormalTextureIndex = RegisterTexture(l_AssimpMaterial, aiTextureType_NORMALS);
                if (l_Material.NormalTextureIndex < 0)
                {
                    l_Material.NormalTextureIndex = RegisterTexture(l_AssimpMaterial, aiTextureType_HEIGHT);
                }

                // TODO: Surface additional PBR texture slots (emissive, occlusion, clearcoat) for richer shading.

                l_ModelData.m_Materials.push_back(l_Material);
            }

            // Lambda responsible for turning an aiMesh into Geometry::Mesh instances used by the renderer.
            const auto ConvertMesh =
                [&](const aiMesh* assimpMesh) -> Geometry::Mesh
                {
                    Geometry::Mesh l_Mesh{};
                    if (!assimpMesh)
                    {
                        return l_Mesh;
                    }

                    const unsigned int l_VertexCount = assimpMesh->mNumVertices;
                    l_Mesh.Vertices.resize(l_VertexCount);

                    for (unsigned int it_Vertex = 0; it_Vertex < l_VertexCount; ++it_Vertex)
                    {
                        Vertex l_Vertex{};

                        // Positions are mandatory for rendering; missing data defaults to the origin.
                        if (assimpMesh->HasPositions())
                        {
                            const aiVector3D& l_Position = assimpMesh->mVertices[it_Vertex];
                            l_Vertex.Position = glm::vec3(l_Position.x, l_Position.y, l_Position.z);
                        }
                        else
                        {
                            TR_CORE_WARN("Mesh is missing positions - defaulting to origin");
                            l_Vertex.Position = glm::vec3(0.0f);
                        }

                        // Normals are required for lighting; Assimp can generate them when absent.
                        if (assimpMesh->HasNormals())
                        {
                            const aiVector3D& l_Normal = assimpMesh->mNormals[it_Vertex];
                            l_Vertex.Normal = glm::vec3(l_Normal.x, l_Normal.y, l_Normal.z);
                        }
                        else
                        {
                            l_Vertex.Normal = s_DefaultNormal;
                        }

                        // Tangents/bitangents enable normal mapping, with safe defaults otherwise.
                        if (assimpMesh->HasTangentsAndBitangents())
                        {
                            const aiVector3D& l_Tangent = assimpMesh->mTangents[it_Vertex];
                            const aiVector3D& l_Bitangent = assimpMesh->mBitangents[it_Vertex];
                            l_Vertex.Tangent = glm::vec3(l_Tangent.x, l_Tangent.y, l_Tangent.z);
                            l_Vertex.Bitangent = glm::vec3(l_Bitangent.x, l_Bitangent.y, l_Bitangent.z);
                        }
                        else
                        {
                            l_Vertex.Tangent = s_DefaultTangent;
                            l_Vertex.Bitangent = s_DefaultBitangent;
                        }

                        // Vertex colors provide baked lighting or tint information.
                        if (assimpMesh->HasVertexColors(0))
                        {
                            const aiColor4D& l_Color = assimpMesh->mColors[0][it_Vertex];
                            l_Vertex.Color = glm::vec3(l_Color.r, l_Color.g, l_Color.b);
                        }
                        else
                        {
                            l_Vertex.Color = s_DefaultColor;
                        }

                        // Primary UV set feeds standard material sampling.
                        if (assimpMesh->HasTextureCoords(0))
                        {
                            const aiVector3D& l_Uv = assimpMesh->mTextureCoords[0][it_Vertex];
                            l_Vertex.TexCoord = glm::vec2(l_Uv.x, l_Uv.y);
                        }
                        else
                        {
                            l_Vertex.TexCoord = s_DefaultTexCoord;
                        }

                        // TODO: Support multiple UV sets for advanced shading workflows.

                        l_Mesh.Vertices[it_Vertex] = l_Vertex;
                    }

                    if (assimpMesh->HasBones())
                    {
                        const auto a_InsertBoneWeight = [](Vertex& a_TargetVertex, int a_BoneIndex, float a_BoneWeight)
                            {
                                bool l_Assigned = false;
                                for (int it_Influence = 0; it_Influence < static_cast<int>(Vertex::MaxBoneInfluences); ++it_Influence)
                                {
                                    if (a_TargetVertex.m_BoneWeights[it_Influence] == 0.0f)
                                    {
                                        a_TargetVertex.m_BoneIndices[it_Influence] = a_BoneIndex;
                                        a_TargetVertex.m_BoneWeights[it_Influence] = a_BoneWeight;
                                        l_Assigned = true;
                                        break;
                                    }
                                }

                                if (!l_Assigned)
                                {
                                    int l_MinIndex = 0;
                                    float l_MinWeight = a_TargetVertex.m_BoneWeights[0];
                                    for (int it_Influence = 1; it_Influence < static_cast<int>(Vertex::MaxBoneInfluences); ++it_Influence)
                                    {
                                        if (a_TargetVertex.m_BoneWeights[it_Influence] < l_MinWeight)
                                        {
                                            l_MinWeight = a_TargetVertex.m_BoneWeights[it_Influence];
                                            l_MinIndex = it_Influence;
                                        }
                                    }

                                    if (a_BoneWeight > l_MinWeight)
                                    {
                                        a_TargetVertex.m_BoneIndices[l_MinIndex] = a_BoneIndex;
                                        a_TargetVertex.m_BoneWeights[l_MinIndex] = a_BoneWeight;
                                    }
                                }
                            };

                        for (unsigned int it_Bone = 0; it_Bone < assimpMesh->mNumBones; ++it_Bone)
                        {
                            aiBone* l_AssimpBone = assimpMesh->mBones[it_Bone];
                            if (!l_AssimpBone)
                            {
                                continue;
                            }

                            const glm::mat4 l_InverseBindMatrix = ConvertMatrix(l_AssimpBone->mOffsetMatrix);
                            const aiNode* l_BoneNode = nullptr;
                            if (l_Scene && l_Scene->mRootNode)
                            {
                                l_BoneNode = l_Scene->mRootNode->FindNode(l_AssimpBone->mName);
                            }

                            const int l_BoneIndex = l_AcquireBoneIndex(l_AssimpBone->mName.C_Str(), l_BoneNode, l_InverseBindMatrix, true);

                            for (unsigned int it_Weight = 0; it_Weight < l_AssimpBone->mNumWeights; ++it_Weight)
                            {
                                const aiVertexWeight& l_Weight = l_AssimpBone->mWeights[it_Weight];
                                if (l_Weight.mVertexId >= l_VertexCount)
                                {
                                    continue;
                                }

                                Vertex& l_TargetVertex = l_Mesh.Vertices[l_Weight.mVertexId];
                                a_InsertBoneWeight(l_TargetVertex, l_BoneIndex, l_Weight.mWeight);
                            }
                        }

                        for (Vertex& l_Vertex : l_Mesh.Vertices)
                        {
                            float l_TotalWeight = 0.0f;
                            for (int it_Influence = 0; it_Influence < static_cast<int>(Vertex::MaxBoneInfluences); ++it_Influence)
                            {
                                l_TotalWeight += l_Vertex.m_BoneWeights[it_Influence];
                            }

                            if (l_TotalWeight > std::numeric_limits<float>::epsilon())
                            {
                                l_Vertex.m_BoneWeights /= l_TotalWeight;
                            }
                        }

                        // TODO: Support per-vertex influence compression for hardware skinning optimisations.
                    }

                    l_Mesh.Indices.reserve(assimpMesh->mNumFaces * 3u);
                    for (unsigned int it_Face = 0; it_Face < assimpMesh->mNumFaces; ++it_Face)
                    {
                        const aiFace& l_Face = assimpMesh->mFaces[it_Face];
                        if (l_Face.mNumIndices != 3)
                        {
                            TR_CORE_WARN("Encountered non-triangle face with {} indices - skipping", l_Face.mNumIndices);
                            continue;
                        }

                        l_Mesh.Indices.push_back(static_cast<uint32_t>(l_Face.mIndices[0]));
                        l_Mesh.Indices.push_back(static_cast<uint32_t>(l_Face.mIndices[1]));
                        l_Mesh.Indices.push_back(static_cast<uint32_t>(l_Face.mIndices[2]));
                    }

                    if (assimpMesh->mMaterialIndex < l_ModelData.m_Materials.size())
                    {
                        l_Mesh.MaterialIndex = static_cast<int>(assimpMesh->mMaterialIndex);
                    }
                    else
                    {
                        TR_CORE_WARN("Mesh references out-of-range material {}", assimpMesh->mMaterialIndex);
                        l_Mesh.MaterialIndex = -1;
                    }

                    return l_Mesh;
                };

            const std::function<void(const aiNode*)> ProcessNode =
                [&](const aiNode* a_Node)
                {
                    if (!a_Node)
                    {
                        return;
                    }

                    // Convert meshes referenced by the current node.
                    for (unsigned int it_Mesh = 0; it_Mesh < a_Node->mNumMeshes; ++it_Mesh)
                    {
                        const unsigned int l_MeshIndex = a_Node->mMeshes[it_Mesh];
                        if (l_MeshIndex >= l_Scene->mNumMeshes)
                        {
                            TR_CORE_WARN("Node references invalid mesh index {}", l_MeshIndex);
                            continue;
                        }

                        const aiMesh* l_AssimpMesh = l_Scene->mMeshes[l_MeshIndex];
                        Geometry::Mesh l_GeometryMesh = ConvertMesh(l_AssimpMesh);
                        if (!l_GeometryMesh.Vertices.empty())
                        {
                            l_ModelData.m_Meshes.push_back(std::move(l_GeometryMesh));
                        }
                    }

                    // Recurse through children to cover the full scene graph.
                    for (unsigned int it_Child = 0; it_Child < a_Node->mNumChildren; ++it_Child)
                    {
                        ProcessNode(a_Node->mChildren[it_Child]);
                    }
                };

            ProcessNode(l_Scene->mRootNode);

            if (l_Scene->mNumAnimations > 0)
            {
                l_ModelData.m_AnimationClips.reserve(l_Scene->mNumAnimations);

                for (unsigned int it_Animation = 0; it_Animation < l_Scene->mNumAnimations; ++it_Animation)
                {
                    const aiAnimation* l_AssimpAnimation = l_Scene->mAnimations[it_Animation];
                    if (!l_AssimpAnimation)
                    {
                        continue;
                    }

                    Animation::AnimationClip l_Clip{};
                    if (l_AssimpAnimation->mName.length > 0)
                    {
                        l_Clip.m_Name = l_AssimpAnimation->mName.C_Str();
                    }
                    else
                    {
                        l_Clip.m_Name = "Animation_" + std::to_string(it_Animation);
                    }

                    const double l_TicksPerSecond = (l_AssimpAnimation->mTicksPerSecond > 0.0)
                        ? l_AssimpAnimation->mTicksPerSecond
                        : 25.0; // Default to 25fps when the asset omits an explicit tick rate.
                    l_Clip.m_TicksPerSecond = static_cast<float>(l_TicksPerSecond);
                    const double l_DurationTicks = l_AssimpAnimation->mDuration;
                    l_Clip.m_DurationSeconds = l_TicksPerSecond > 0.0
                        ? static_cast<float>(l_DurationTicks / l_TicksPerSecond)
                        : 0.0f;

                    l_Clip.m_Channels.reserve(l_AssimpAnimation->mNumChannels);
                    for (unsigned int it_Channel = 0; it_Channel < l_AssimpAnimation->mNumChannels; ++it_Channel)
                    {
                        aiNodeAnim* l_AssimpChannel = l_AssimpAnimation->mChannels[it_Channel];
                        if (!l_AssimpChannel)
                        {
                            continue;
                        }

                        const std::string l_NormalizedChannelName = NormalizeMixamoName(l_AssimpChannel->mNodeName.C_Str());
                        int l_BoneIndex = -1;

                        auto a_NormalizedLookup = l_Skeleton.m_NameToIndex.find(l_NormalizedChannelName);
                        if (a_NormalizedLookup != l_Skeleton.m_NameToIndex.end())
                        {
                            l_BoneIndex = a_NormalizedLookup->second;
                        }
                        else
                        {
                            auto sourceLookup = l_Skeleton.m_SourceNameToIndex.find(l_AssimpChannel->mNodeName.C_Str());
                            if (sourceLookup != l_Skeleton.m_SourceNameToIndex.end())
                            {
                                l_BoneIndex = sourceLookup->second;
                            }
                            else
                            {
                                const aiNode* l_ChannelNode = l_Scene->mRootNode
                                    ? l_Scene->mRootNode->FindNode(l_AssimpChannel->mNodeName)
                                    : nullptr;
                                l_BoneIndex = l_AcquireBoneIndex(l_AssimpChannel->mNodeName.C_Str(), l_ChannelNode, glm::mat4(1.0f), false);
                            }
                        }

                        if (l_BoneIndex < 0)
                        {
                            continue;
                        }

                        Animation::TransformChannel l_Channel{};
                        l_Channel.m_BoneIndex = l_BoneIndex;

                        l_Channel.m_TranslationKeys.reserve(l_AssimpChannel->mNumPositionKeys);
                        for (unsigned int it_Key = 0; it_Key < l_AssimpChannel->mNumPositionKeys; ++it_Key)
                        {
                            const aiVectorKey& l_Key = l_AssimpChannel->mPositionKeys[it_Key];
                            Animation::VectorKeyframe l_Keyframe{};
                            l_Keyframe.m_TimeSeconds = static_cast<float>(l_Key.mTime / l_TicksPerSecond);
                            l_Keyframe.m_Value = glm::vec3(l_Key.mValue.x, l_Key.mValue.y, l_Key.mValue.z);
                            l_Channel.m_TranslationKeys.push_back(l_Keyframe);
                        }

                        l_Channel.m_RotationKeys.reserve(l_AssimpChannel->mNumRotationKeys);
                        for (unsigned int it_Key = 0; it_Key < l_AssimpChannel->mNumRotationKeys; ++it_Key)
                        {
                            const aiQuatKey& l_Key = l_AssimpChannel->mRotationKeys[it_Key];
                            Animation::QuaternionKeyframe l_Keyframe{};
                            l_Keyframe.m_TimeSeconds = static_cast<float>(l_Key.mTime / l_TicksPerSecond);
                            l_Keyframe.m_Value = glm::quat(l_Key.mValue.w, l_Key.mValue.x, l_Key.mValue.y, l_Key.mValue.z);
                            l_Channel.m_RotationKeys.push_back(l_Keyframe);
                        }

                        l_Channel.m_ScaleKeys.reserve(l_AssimpChannel->mNumScalingKeys);
                        for (unsigned int it_Key = 0; it_Key < l_AssimpChannel->mNumScalingKeys; ++it_Key)
                        {
                            const aiVectorKey& l_Key = l_AssimpChannel->mScalingKeys[it_Key];
                            Animation::VectorKeyframe l_Keyframe{};
                            l_Keyframe.m_TimeSeconds = static_cast<float>(l_Key.mTime / l_TicksPerSecond);
                            l_Keyframe.m_Value = glm::vec3(l_Key.mValue.x, l_Key.mValue.y, l_Key.mValue.z);
                            l_Channel.m_ScaleKeys.push_back(l_Keyframe);
                        }

                        l_Clip.m_Channels.push_back(std::move(l_Channel));
                    }

                    // TODO: Implement animation blending and retargeting hooks to combine multiple clips at runtime.
                    l_ModelData.m_AnimationClips.push_back(std::move(l_Clip));
                }
            }

            return l_ModelData;
        }
    }
}