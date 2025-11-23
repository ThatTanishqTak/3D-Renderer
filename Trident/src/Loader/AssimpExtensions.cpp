#include "Loader/AssimpExtensions.h"

#include <assimp/Importer.hpp>

#include <algorithm>
#include <cctype>
#include <mutex>
#include <sstream>

namespace Trident
{
    namespace Loader
    {
        namespace AssimpExtensions
        {
            namespace
            {
                // Converts the semicolon-delimited extension list reported by Assimp into a
                // normalized, lowercase vector that matches std::filesystem::path::extension output.
                std::vector<std::string> CreateExtensionList()
                {
                    Assimp::Importer l_Importer{};
                    aiString l_ExtensionsRaw;
                    l_Importer.GetExtensionList(l_ExtensionsRaw);

                    std::vector<std::string> l_Extensions{};
                    std::stringstream l_Stream(l_ExtensionsRaw.C_Str());
                    std::string l_Token{};

                    while (std::getline(l_Stream, l_Token, ';'))
                    {
                        l_Token.erase(std::remove_if(l_Token.begin(), l_Token.end(), [](unsigned char a_Char)
                            {
                                return std::isspace(a_Char);
                            }), l_Token.end());

                        if (l_Token.empty())
                        {
                            continue;
                        }

                        if (l_Token[0] == '*')
                        {
                            l_Token.erase(0, 1);
                        }

                        if (!l_Token.empty() && l_Token[0] != '.')
                        {
                            l_Token.insert(l_Token.begin(), '.');
                        }

                        std::transform(l_Token.begin(), l_Token.end(), l_Token.begin(), [](unsigned char a_Char)
                            {
                                return static_cast<char>(std::tolower(a_Char));
                            });

                        l_Extensions.push_back(std::move(l_Token));
                    }

                    return l_Extensions;
                }
            }

            const std::vector<std::string>& GetNormalizedExtensions()
            {
                static std::once_flag s_ExtensionOnceFlag{};
                static std::vector<std::string> s_SupportedExtensions{};

                std::call_once(s_ExtensionOnceFlag, []()
                    {
                        // The query only happens once per process so subsequent scene loads reuse
                        // the cached data instead of repeatedly instantiating Assimp importers.
                        s_SupportedExtensions = CreateExtensionList();
                        // TODO: Persist the extension list between application runs if startup becomes expensive.
                    });

                return s_SupportedExtensions;
            }
        }
    }
}