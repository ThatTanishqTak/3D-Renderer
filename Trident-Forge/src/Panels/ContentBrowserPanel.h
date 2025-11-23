#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace EditorPanels
{
    /**
     * @brief Simple content browser for navigating project assets.
     *
     * The panel keeps track of a root directory and current directory, and exposes
     * a lightweight callback when a file is activated via double-click. Ownership
     * of any imported assets remains with the host application.
     */
    class ContentBrowserPanel
    {
    public:
        /// @brief Configure the root directory the browser is allowed to navigate under.
        void SetRootDirectory(const std::filesystem::path& rootDirectory);

        /// @brief Optional callback invoked when a file (not directory) is double-clicked.
        void SetOnAssetActivated(const std::function<void(const std::filesystem::path&)>& callback);

        /// @brief Per-frame update hook (reserved for future caching/indexing).
        void Update();

        /// @brief Render the ImGui window for the content browser.
        void Render();

    private:
        void RefreshEntries();

    private:
        std::filesystem::path m_RootDirectory{};
        std::filesystem::path m_CurrentDirectory{};
        std::vector<std::filesystem::directory_entry> m_Entries{};
        bool m_EntriesDirty = true;

        std::function<void(const std::filesystem::path&)> m_OnAssetActivated{};
        std::string m_StatusMessage{ "No content directory configured." };
    };
}