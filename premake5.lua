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

group "Dependencies"
	include "Engine/vendor/imgui"
group ""