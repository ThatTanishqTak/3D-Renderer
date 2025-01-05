project "Rayguicpp"
	kind "StaticLib"
	language "C++"
	cppdialect "C++17"
	staticruntime "off"

	targetdir ("bin/" .. outputDir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputDir .. "/%{prj.name}")

    files
	{
		"include/**.h",
		"src/**.cpp"
    }

    defines
	{

	}

    includedirs
    {
        "%{IncludeDir.Raylib}",
        "%{IncludeDir.Raygui}",
        "%{IncludeDir.Rayguicpp}"
    }
    
    filter "system:windows"
		systemversion "latest"

        filter "configurations:Debug"
            runtime "Debug"
            symbols "on"

        filter "configurations:Release"
            runtime "Release"
            optimize "on"