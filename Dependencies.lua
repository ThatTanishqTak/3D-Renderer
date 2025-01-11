IncludeDir = {}
IncludeDir["Raylib"] = "%{wks.location}/Engine/vendor/raylib/include"
IncludeDir["rlImGui"] = "%{wks.location}/Engine/vendor/rlImGui"

LibraryDir = {}
LibraryDir["Raylib"] = "%{wks.location}/Engine/vendor/raylib/lib/raylib.lib"
LibraryDir["rlImGuiD"] = "%{wks.location}/Engine/vendor/rlImGui/lib/Debug/rlImGui.lib"
LibraryDir["rlImGuiR"] = "%{wks.location}/Engine/vendor/rlImGui/lib/Release/rlImGui.lib"

Library = {}
Library["Winmm"] = "winmm.lib"