// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MPSurv : ModuleRules
{
	public MPSurv(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "UMG", "CommonUI", "ModelViewViewModel", "RealtimeMeshComponent", "PCG", "OnlineSubsystem", "OnlineSubsystemUtils", "OnlineSubsystemSteam" });
		PrivateDependencyModuleNames.AddRange(new string[] { "RHI", "Slate", "SlateCore" });
	}
}
