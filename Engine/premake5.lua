project "Engine"
    kind "StaticLib"
    language "C++"
    staticruntime "Off"

    targetdir("%{wks.location}/bin/" .. outputDir .. "/%{prj.name}")
	objdir("%{wks.location}/bin-int/" .. outputDir .. "/%{prj.name}")

    files
    {
        "src/**.h",
        "src/**.cpp"
    }

    includedirs
	{
		"src",
		"%{IncludeDir.glfw}",
		"%{IncludeDir.glad}"
    }

    links
	{
		"GLFW",
		"glad",
        "opengl32.lib"
    }

    filter "system:windows"
        systemversion "latest"

    filter "configurations:Debug"
        runtime "Debug"
        symbols "on"

    filter "configurations:Release"
        runtime "Release"
        optimize "on"