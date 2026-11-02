// Copyright Epic Games, Inc. All Rights Reserved.

#include "Terrain/WorldGenSettings.h"

UWorldGenSettings::UWorldGenSettings()
{
	RingZones = FRingZoneSettings::MakeDefaults();
}

FPrimaryAssetId UWorldGenSettings::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("WorldGenSettings"), GetFName());
}

void UWorldGenSettings::ApplyTo(FTerrainGenerationSettings& Settings) const
{
	Settings.NormalStrength = NormalStrength;
	Settings.SkirtDepth = SkirtDepth;
	Settings.MacroFrequency = MacroFrequency;
	Settings.OceanThreshold = OceanThreshold;
	Settings.MountainThreshold = MountainThreshold;
	Settings.StartIslandRadius = StartIslandRadius;
	Settings.StartIslandStrength = StartIslandStrength;
	Settings.StraitFreeRadius = StraitFreeRadius;
	Settings.BiomeCellSize = BiomeCellSize;
	Settings.BiomeWarpAmplitude = BiomeWarpAmplitude;
	Settings.RingZones = RingZones;
	Settings.DesertLatitude = DesertLatitude;
	Settings.TundraLatitude = TundraLatitude;
	Settings.JungleLongitude = JungleLongitude;
	Settings.FoothillsMaxHeight = FoothillsMaxHeight;
	Settings.OasisGridSize = OasisGridSize;
	Settings.RiverFrequency = RiverFrequency;
	Settings.RiverWidth = RiverWidth;
	Settings.RiverShoreWidth = RiverShoreWidth;
	Settings.RiverDepth = RiverDepth;
}
