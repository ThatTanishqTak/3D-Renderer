include "Dependencies.lua"

workspace "3D Renderer"
    architecture "x86_64"
    startproject "Client"

    configurations
    {
        "Debug",
        "Release"
    }

    flags
	{
		"MultiProcessorCompile"
	}

outputDir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

include "Engine"
include "Client"

include "Engine/vendor/raygui-cpp"