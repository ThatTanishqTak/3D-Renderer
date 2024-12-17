IncludeDir = {}
IncludeDir["raylib"] = "%{wks.location}/Engine/vendor/raylib/include"
IncludeDir["rlImgui"] = "%{wks.location}/Engine/vendor/rlImGui/include"
IncludeDir["ImGui"] = "%{wks.location}/Engine/vendor/imgui"

LibraryDir = {}
LibraryDir["raylib"] = "%{wks.location}/Engine/vendor/raylib/lib/raylib.lib"
LibraryDir["rlImGui"] = "%{wks.location}/Engine/vendor/rlImGui/lib/rlImGui.lib"

Library = {}
Library["gdi32"] = "gdi32.lib"
Library["Winmm"] = "winmm.lib"