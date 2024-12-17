project "Engine"
    kind "StaticLib"
    language "C++"
    cppdialect "C++17"
    staticruntime "Off"

    targetdir("%{wks.location}/bin/" .. outputDir .. "/%{prj.name}")
	objdir("%{wks.location}/bin-int/" .. outputDir .. "/%{prj.name}")

    --pchheader("Core.h")
    --pchsource("src/Core.h")

    files
    {
        "src/**.h",
        "src/**.cpp"
    }

    includedirs
	{
		"src",
		"%{IncludeDir.raylib}",
        "%{IncludeDir.rlImGui}",
        "%{IncludeDir.ImGui}"
    }

    links
	{
        "%{Library.Winmm}",
        "%{LibraryDir.raylib}",
        "%{LibraryDir.rlImGui}",
        "ImGui"
    }

    defines
    {
        "RAYGUI_IMPLEMENTATION",
        "_CRT_SECURE_NO_WARNINGS"
    }

    filter "system:windows"
        systemversion "latest"

    filter "configurations:Debug"
        runtime "Debug"
        symbols "on"

    filter "configurations:Release"
        runtime "Release"
        optimize "on"