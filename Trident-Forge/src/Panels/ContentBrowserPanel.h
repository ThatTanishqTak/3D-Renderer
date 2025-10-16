#pragma once

#include <filesystem>
#include <vector>

// The ContentBrowserPanel exposes the on-disk Assets directory so artists and designers can
// drag-and-drop content directly into the scene. The class keeps track of the active folder
// and renders the hierarchy inside an ImGui window.
class ContentBrowserPanel
{
public:
    // Construct the panel by resolving the root Assets directory relative to the working directory.
    ContentBrowserPanel();

    // Allow the panel to refresh any cached state before rendering (reserved for future enhancements).
    void Update();

    // Draw the ImGui window representing the content browser.
    void Render();

private:
    // Helper that renders a collection of directory entries using a tiled layout.
    void DrawEntryGrid(const std::vector<std::filesystem::directory_entry>& entries, bool highlightAsFolder);

    // Root directory that bounds all navigation. Prevents the browser from climbing outside Assets/.
    std::filesystem::path m_RootDirectory;
    // Directory currently displayed in the panel.
    std::filesystem::path m_CurrentDirectory;
};