IncludeDir = {}
IncludeDir["raylib"] = "%{wks.location}/Engine/vendor/raylib/include"
IncludeDir["rlImGui"] = "%{wks.location}/Engine/vendor/rlImGui/include"

LibraryDir = {}
LibraryDir["raylib"] = "%{wks.location}/Engine/vendor/raylib/lib/raylib.lib"
LibraryDir["rlImGuiDebug"] = "%{wks.location}/Engine/vendor/rlImGui/lib/Debug/rlImGui.lib"
LibraryDir["rlImGuiRelese"] = "%{wks.location}/Engine/vendor/rlImGui/lib/Release/rlImGui.lib"

Library = {}
Library["Winmm"] = "winmm.lib"