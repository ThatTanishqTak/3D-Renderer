IncludeDir = {}
IncludeDir["Raylib"] = "%{wks.location}/Engine/vendor/raylib/include"
IncludeDir["Raygui"] = "%{wks.location}/Engine/vendor/raygui/src"
IncludeDir["Rayguicpp"] = "%{wks.location}/Engine/vendor/raygui-cpp/include"

LibraryDir = {}
LibraryDir["Raylib"] = "%{wks.location}/Engine/vendor/raylib/lib/raylib.lib"
LibraryDir["RayguicppD"] = "%{wks.location}/Engine/vendor/raygui-cpp/bin/Debug-windows-x86_64/Rayguicpp/Rayguicpp.lib"
LibraryDir["RayguicppR"] = "%{wks.location}/Engine/vendor/raygui-cpp/bin/Release-windows-x86_64/Rayguicpp/Rayguicpp.lib"

Library = {}
Library["Winmm"] = "winmm.lib"