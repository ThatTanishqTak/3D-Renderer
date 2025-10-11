#pragma once

#include <string>
#include <filesystem>
#include "Loader/TextureLoader.h"

namespace Trident
{
    class Application;

    namespace AI
    {
        class ONNXRuntime;
    }
}

namespace UI
{
    /**
     * @brief Dedicated panel that exposes asset loading and AI workflow utilities.
     */
    class ContentBrowserPanel
    {
    public:
        ContentBrowserPanel();
        ~ContentBrowserPanel();

        /**
         * @brief Draw the panel each frame.
         */
        void Render();

        /**
         * @brief Provide access to the running engine so scenes can be swapped from the browser.
         */
        void SetEngine(Trident::Application* engine);

        /**
         * @brief Hook up an ONNX runtime to enable model inspection utilities.
         */
        void SetOnnxRuntime(Trident::AI::ONNXRuntime* onnxRuntime);

        Trident::AI::ONNXRuntime* m_OnnxRuntime;

    private:
        void DrawDirectoryTree(const std::filesystem::path& directory);
        void DrawDirectoryGrid(const std::filesystem::path& directory);

    private:
        Trident::Application* m_Engine;

        // Persist the currently dragged asset path so the ImGui payload references stable memory.
        std::string m_DragPayloadBuffer;

        std::string m_AssetsPath;
        std::filesystem::path m_CurrentDirectory;
        std::filesystem::path m_SelectedPath;

        float m_ThumbnailSize;
        float m_ThumbnailPadding;

        bool m_OpenModelDialog;
        bool m_OpenTextureDialog;
        bool m_OpenSceneDialog;
        bool m_OpenOnnxDialog;
        bool m_OnnxLoaded;

        Trident::Loader::TextureData m_FileIcon;
    };
}