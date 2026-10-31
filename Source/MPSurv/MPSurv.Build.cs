// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MPSurv : ModuleRules
{
	public MPSurv(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "RealtimeMeshComponent", "PCG", "OnlineSubsystem", "OnlineSubsystemUtils", "OnlineSubsystemSteam" });
		PrivateDependencyModuleNames.Add("RHI");
	}
}
