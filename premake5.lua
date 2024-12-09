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

group "Dependencies"
	include "Engine/vendor/GLFW"
	include "Engine/vendor/glad"
group ""


include "Engine"
include "Client"