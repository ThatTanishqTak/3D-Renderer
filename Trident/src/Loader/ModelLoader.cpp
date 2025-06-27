#include "Loader/ModelLoader.h"

#include "Core/Utilities.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

namespace Trident
{
    namespace Loader
    {
        static void ProcessMesh(aiMesh* mesh, Geometry::Mesh& outMesh)
        {
            for (unsigned i = 0; i < mesh->mNumVertices; ++i)
            {
                Vertex l_Vertex{};
                l_Vertex.Position = { mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z };
                l_Vertex.Color = { 1.0f, 1.0f, 1.0f };
                if (mesh->mTextureCoords[0])
                {
                    l_Vertex.TexCoord = { mesh->mTextureCoords[0][i].x, 1.0f - mesh->mTextureCoords[0][i].y };
                }
                outMesh.Vertices.push_back(l_Vertex);
            }

            for (unsigned i = 0; i < mesh->mNumFaces; ++i)
            {
                const aiFace& l_Face = mesh->mFaces[i];
                for (unsigned j = 0; j < l_Face.mNumIndices; ++j)
                {
                    outMesh.Indices.push_back(static_cast<uint16_t>(l_Face.mIndices[j]));
                }
            }
        }

        Geometry::Mesh ModelLoader::Load(const std::string& filePath)
        {
            Geometry::Mesh l_Mesh{};

            Assimp::Importer l_Importer;
            const aiScene* l_Scene = l_Importer.ReadFile(
                filePath,
                aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_JoinIdenticalVertices);

            if (!l_Scene || l_Scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !l_Scene->mRootNode)
            {
                TR_CORE_CRITICAL("Failed to load model: {} ({})", filePath, l_Importer.GetErrorString());
                return l_Mesh;
            }

            std::function<void(aiNode*)> l_Traverse = [&](aiNode* node)
                {
                    for (unsigned i = 0; i < node->mNumMeshes; ++i)
                    {
                        aiMesh* l_MeshPtr = l_Scene->mMeshes[node->mMeshes[i]];
                        ProcessMesh(l_MeshPtr, l_Mesh);
                    }

                    for (unsigned i = 0; i < node->mNumChildren; ++i)
                    {
                        l_Traverse(node->mChildren[i]);
                    }
                };

            l_Traverse(l_Scene->mRootNode);

            return l_Mesh;
        }
    }
}