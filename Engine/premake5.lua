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

	}

    includedirs
	{
		"src",
        "%{IncludeDir.Raylib}"
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

        filter "configurations:Release"
            runtime "Release"
            optimize "on"