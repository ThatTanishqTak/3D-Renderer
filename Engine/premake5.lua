project "Engine"
	kind "StaticLib"
	language "C++"
	cppdialect "C++17"
	staticruntime "off"

	targetdir ("%{wks.location}/bin/" .. outputDir .. "/%{prj.name}")
	objdir ("%{wks.location}/bin-int/" .. outputDir .. "/%{prj.name}")

    files
	{
		"src/**.h",
		"src/**.cpp"
    }

    defines
	{
        "_CRT_SECURE_NO_DEPRECATE",
        "_CRT_SECURE_NO_WARNINGS"
	}

    includedirs
	{
		"src",
        "%{IncludeDir.Raylib}",
        "%{IncludeDir.Raygui}",
        "%{IncludeDir.Rayguicpp}",
    }

    links
	{
        "%{LibraryDir.Raylib}",
        "%{Library.Winmm}"
    }
    
    filter "system:windows"
		systemversion "latest"

        filter "configurations:Debug"
            runtime "Debug"
            symbols "on"
            links
            {
                "%{LibraryDir.RayguicppD}"
            }

        filter "configurations:Release"
            runtime "Release"
            optimize "on"
            links
            {
                "%{LibraryDir.RayguicppR}"
            }