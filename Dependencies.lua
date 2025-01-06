IncludeDir = {}
IncludeDir["Raylib"] = "%{wks.location}/Engine/vendor/raylib/include"
IncludeDir["Raygui"] = "%{wks.location}/Engine/vendor/raygui/src"
IncludeDir["RayguiWrapper"] = "%{wks.location}/Engine/vendor/raygui-cpp/include"

LibraryDir = {}
LibraryDir["Raylib"] = "%{wks.location}/Engine/vendor/raylib/lib/raylib.lib"
LibraryDir["RayguiWrapperD"] = "%{wks.location}/Engine/vendor/raygui-cpp/bin/Debug-windows-x86_64/RayguiWrapper/RayguiWrapper.lib"
LibraryDir["RayguiWrapperR"] = "%{wks.location}/Engine/vendor/raygui-cpp/bin/Release-windows-x86_64/RayguiWrapper/RayguiWrapper.lib"

Library = {}
Library["Winmm"] = "winmm.lib"