#include "Loader/ModelLoader.h"
#include "Core/Utilities.h"

#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>

#include <filesystem>

namespace fs = std::filesystem;

namespace Trident
{
    namespace Loader
    {
        namespace
        {
            template<typename T>
            static void CopyData(const tinygltf::Model& model, const tinygltf::Accessor& accessor, std::vector<T>& out)
            {
                if (accessor.count == 0) return;
                const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
                const tinygltf::Buffer& buffer = model.buffers[view.buffer];
                const T* data = reinterpret_cast<const T*>(&buffer.data[accessor.byteOffset + view.byteOffset]);
                out.assign(data, data + accessor.count);
            }
        }

        std::vector<Geometry::Mesh> ModelLoader::Load(const std::string& filePath)
        {
            std::vector<Geometry::Mesh> meshes{};
            fs::path path = Utilities::FileManagement::NormalizePath(filePath);
            if (path.extension() != ".gltf" && path.extension() != ".glb")
            {
                TR_CORE_CRITICAL("Unsupported model format: {}", filePath);
                return meshes;
            }

            tinygltf::TinyGLTF loader;
            tinygltf::Model model;
            std::string err, warn;
            bool result = false;
            if (path.extension() == ".glb")
                result = loader.LoadBinaryFromFile(&model, &err, &warn, path.string());
            else
                result = loader.LoadASCIIFromFile(&model, &err, &warn, path.string());

            if (!warn.empty())
                TR_CORE_WARN("{}", warn);
            if (!err.empty())
                TR_CORE_ERROR("{}", err);
            if (!result)
            {
                TR_CORE_CRITICAL("Failed to load model: {}", filePath);
                return meshes;
            }

            for (const auto& m : model.meshes)
            {
                for (const auto& p : m.primitives)
                {
                    Geometry::Mesh mesh{};

                    const tinygltf::Accessor& posAcc = model.accessors[p.attributes.at("POSITION")];
                    CopyData(model, posAcc, mesh.Vertices);

                    if (p.indices >= 0)
                    {
                        const tinygltf::Accessor& idxAcc = model.accessors[p.indices];
                        CopyData(model, idxAcc, mesh.Indices);
                    }

                    if (auto it = p.attributes.find("TEXCOORD_0"); it != p.attributes.end())
                    {
                        const tinygltf::Accessor& uvAcc = model.accessors[it->second];
                        std::vector<glm::vec2> uvs;
                        CopyData(model, uvAcc, uvs);
                        for (size_t i = 0; i < mesh.Vertices.size() && i < uvs.size(); ++i)
                            mesh.Vertices[i].TexCoord = uvs[i];
                    }

                    if (auto it = p.attributes.find("COLOR_0"); it != p.attributes.end())
                    {
                        const tinygltf::Accessor& colAcc = model.accessors[it->second];
                        std::vector<glm::vec3> cols;
                        CopyData(model, colAcc, cols);
                        for (size_t i = 0; i < mesh.Vertices.size() && i < cols.size(); ++i)
                            mesh.Vertices[i].Color = cols[i];
                    }
                    else
                    {
                        for (auto& v : mesh.Vertices)
                            v.Color = { 1.0f, 1.0f, 1.0f };
                    }

                    meshes.push_back(std::move(mesh));
                }
            }

            return meshes;
        }
    }
}