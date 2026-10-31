// Copyright (c) 2015-2025 TriAxis Games, L.L.C. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class RealtimeMeshComponent : ModuleRules
{
    public RealtimeMeshComponent(ReadOnlyTargetRules rules) : base(rules)
    {
        //IWYUSupport = IWYUSupport.None;
        bLegacyPublicIncludePaths = false;
        // Shared PCH is built with RHI_RAYTRACING=0 (project RayTracingMode=Disabled). This module
        // must parse FPrimitiveSceneProxy with RHI_RAYTRACING=1, so it cannot use that PCH.
        PCHUsage = PCHUsageMode.NoPCHs;
        bUseUnity = false;
        //IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        
        PublicDefinitions.Add("WITH_REALTIME_MESH=1");
        PublicDefinitions.Add("IS_REALTIME_MESH_LIBRARY=1");

        // The installed editor binary is built with the ray tracing virtuals on FPrimitiveSceneProxy.
        // Project RayTracingMode=Disabled leaves this macro at 0, which drops 11 slots from the scene
        // proxy vtable. The renderer then calls GetInstanceDataUpdateTaskInfo past the end of it.
        // Private so dependents that use the shared PCH (where this macro is 0) do not recompile the overrides.
        PrivateDefinitions.Add("RHI_RAYTRACING=1");

        // This is to access RayTracing Definitions
        PrivateIncludePaths.Add(Path.Combine(EngineDirectory, "Shaders", "Shared"));
        PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public", "Interface"));

        //PublicDefinitions.Add("REALTIME_MESH_INTERFACE_ROOT=");
        
        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core", 
                "CoreUObject", 
                "GeometryCore",
                "RenderCore",
                "RHI",
            }
            );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Engine",
                "NavigationSystem",
                "PhysicsCore",
				"DeveloperSettings",
                "Projects",
                "Chaos",
                "ChaosCore",
            }
            );
    }
}