project "Client"
    kind "ConsoleApp"
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
        "%{wks.location}/Engine/src/**",
		"%{wks.location}/Engine/vendor/**"
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
			kind "WindowedApp"
			entrypoint "mainCRTStartup"
		    runtime "Release"
		    optimize "on"