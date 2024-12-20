project "Client"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++17"
    staticruntime "Off"

    targetdir ("%{wks.location}/bin/" .. outputDir .. "/%{prj.name}")
	objdir ("%{wks.location}/bin-int/" .. outputDir .. "/%{prj.name}")

    defines
	{
		"_CRT_SECURE_NO_WARNINGS"
	}

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

        postbuildcommands
        {
            "{COPYFILE} \"imgui.ini\" \"%{cfg.targetdir}\""
        }

    filter "configurations:Debug"
        runtime "Debug"
        symbols "on"

    filter "configurations:Release"
        runtime "Release"
        optimize "on"
        kind "WindowedApp"
        entrypoint "mainCRTStartup"