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
		postbuildcommands
		{
			"{MKDIR} \"%{cfg.targetdir}/Resources\"",
			"{MKDIR} \"%{cfg.targetdir}/Resources/Icon\"",
			"{MKDIR} \"%{cfg.targetdir}/Resources/Assets\"",

			"{COPYDIR} ../Client/Resources/ %{cfg.targetdir}/Resources",
			"{COPYDIR} ../Client/Resources/Icon/ %{cfg.targetdir}/Resources/Icon",
			"{COPYDIR} ../Client/Resources/Assets/ %{cfg.targetdir}/Resources/Assets",

			--if _ACTION == "build" or "rebuild" or "clean" then
			--	os.remove("%{wks.location}/bin/" .. outputDir .. "/%{prj.name}/*.pdb")
			--end
		}

	    filter "configurations:Debug"
		    runtime "Debug"
		    symbols "on"

        filter "configurations:Release"
			kind "WindowedApp"
			entrypoint "mainCRTStartup"
		    runtime "Release"
		    optimize "on"