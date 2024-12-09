project "Client"
    kind "ConsoleApp"
    language "C++"
    staticruntime "Off"

    targetdir("bin/" .. outputDir .. "/%{prj.name}")
	objdir("bin-int/" .. outputDir .. "/%{prj.name}")

    files
    {
        "src/**.h",
        "src/**.cpp"
    }

    includedirs
    {
        "%{wks.location}/Engine/src",
		"%{wks.location}/Engine/vendor"
    }

    links
    {
        "Engine"
    }

    filter "system:windows"
    systemversion "latest"

    filter "configurations:Debug"
        runtime "Debug"
        symbols "on"

    filter "configurations:Release"
        runtime "Release"
        optimize "on"
        kind "WindowedApp"
        entrypoint "mainCRTStartup"