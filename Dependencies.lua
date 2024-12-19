IncludeDir = {}
IncludeDir["raylib"] = "%{wks.location}/Engine/vendor/raylib/include"
IncludeDir["rlImGui"] = "%{wks.location}/Engine/vendor/rlImGui/include"

LibraryDir = {}
LibraryDir["raylib"] = "%{wks.location}/Engine/vendor/raylib/lib/raylib.lib"
LibraryDir["rlImGui"] = "%{wks.location}/Engine/vendor/rlImGui/lib/rlImGui.lib"

Library = {}
Library["Winmm"] = "winmm.lib"