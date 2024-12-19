project "Engine"
    kind "StaticLib"
    language "C++"
    cppdialect "C++17"
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
		"%{IncludeDir.raylib}",
        "%{IncludeDir.rlImGui}"
    }

    links
	{
        "%{LibraryDir.raylib}",
        "%{LibraryDir.rlImGui}",
        "%{Library.Winmm}"
    }

    defines
    {
        "_CRT_SECURE_NO_WARNINGS",
    }

    filter "system:windows"
        systemversion "latest"

    filter "configurations:Debug"
        runtime "Debug"
        symbols "on"

    filter "configurations:Release"
        runtime "Release"
        optimize "on"