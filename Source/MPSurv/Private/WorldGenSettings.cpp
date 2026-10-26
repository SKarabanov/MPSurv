// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldGenSettings.h"

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
	Settings.CenterPlainsRadius = CenterPlainsRadius;
	Settings.BiomeFalloffRadius = BiomeFalloffRadius;
	Settings.BiomeWarpAmplitude = BiomeWarpAmplitude;
	Settings.PlainsRadius = PlainsRadius;
	Settings.MountainLatitude = MountainLatitude;
	Settings.DesertLatitude = DesertLatitude;
	Settings.ForestMinRadius = ForestMinRadius;
	Settings.ForestMaxRadius = ForestMaxRadius;
	Settings.SwampPlateauRadius = SwampPlateauRadius;
	Settings.MountainMinRadius = MountainMinRadius;
	Settings.MountainMaxRadius = MountainMaxRadius;
	Settings.TundraLatitude = TundraLatitude;
	Settings.JungleLongitude = JungleLongitude;
	Settings.FoothillsMaxHeight = FoothillsMaxHeight;
	Settings.DesertRegionMinSize = DesertRegionMinSize;
	Settings.DesertRegionMaxSize = DesertRegionMaxSize;
	Settings.DesertRegionChance = DesertRegionChance;
	Settings.OasisGridSize = OasisGridSize;
	Settings.RiverFrequency = RiverFrequency;
	Settings.RiverWidth = RiverWidth;
	Settings.RiverShoreWidth = RiverShoreWidth;
	Settings.RiverDepth = RiverDepth;
}
