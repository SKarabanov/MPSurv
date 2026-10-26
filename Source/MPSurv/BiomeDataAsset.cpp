// Copyright Epic Games, Inc. All Rights Reserved.

#include "BiomeDataAsset.h"

FPrimaryAssetId UBiomeDataAsset::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("BiomeSettings"), GetFName());
}

void UBiomeDataAsset::ApplyTo(FTerrainGenerationSettings& /*Settings*/) const
{
}

UOceanBiomeDataAsset::UOceanBiomeDataAsset()
{
	BiomeType = EBiomeType::Ocean;
}

void UOceanBiomeDataAsset::ApplyTo(FTerrainGenerationSettings& Settings) const
{
	Settings.OceanMaxDepth = OceanMaxDepth;
	Settings.OceanHillsFrequency = OceanHillsFrequency;
	Settings.OceanHillsAmplitude = OceanHillsAmplitude;
	Settings.OceanDetailFrequency = OceanDetailFrequency;
	Settings.OceanDetailAmplitude = OceanDetailAmplitude;
}

UPlainsBiomeDataAsset::UPlainsBiomeDataAsset()
{
	BiomeType = EBiomeType::Plains;
}

void UPlainsBiomeDataAsset::ApplyTo(FTerrainGenerationSettings& Settings) const
{
	Settings.PlainsHillsFrequency = PlainsHillsFrequency;
	Settings.PlainsHillsAmplitude = PlainsHillsAmplitude;
	Settings.PlainsDetailFrequency = PlainsDetailFrequency;
	Settings.PlainsDetailAmplitude = PlainsDetailAmplitude;
}

UForestBiomeDataAsset::UForestBiomeDataAsset()
{
	BiomeType = EBiomeType::Forest;
}

void UForestBiomeDataAsset::ApplyTo(FTerrainGenerationSettings& Settings) const
{
	Settings.ForestHillsFrequency = ForestHillsFrequency;
	Settings.ForestHillsHeight = ForestHillsHeight;
	Settings.ForestDetailFrequency = ForestDetailFrequency;
	Settings.ForestDetailAmplitude = ForestDetailAmplitude;
}

USwampBiomeDataAsset::USwampBiomeDataAsset()
{
	BiomeType = EBiomeType::Swamp;
}

void USwampBiomeDataAsset::ApplyTo(FTerrainGenerationSettings& Settings) const
{
	Settings.SwampBaseHeight = SwampBaseHeight;
	Settings.SwampPuddleNoiseFrequency = SwampPuddleNoiseFrequency;
	Settings.SwampPuddleDepth = SwampPuddleDepth;
	Settings.SwampBlendWidth = SwampBlendWidth;
}

UPlateauBiomeDataAsset::UPlateauBiomeDataAsset()
{
	BiomeType = EBiomeType::Plateau;
}

void UPlateauBiomeDataAsset::ApplyTo(FTerrainGenerationSettings& Settings) const
{
	Settings.PlateauHeight = PlateauHeight;
	Settings.CanyonFrequency = CanyonFrequency;
	Settings.CanyonWidth = CanyonWidth;
	Settings.CanyonFloorWidth = CanyonFloorWidth;
	Settings.PlateauHillsFrequency = PlateauHillsFrequency;
	Settings.PlateauHillsAmplitude = PlateauHillsAmplitude;
	Settings.PlateauDetailFrequency = PlateauDetailFrequency;
	Settings.PlateauDetailAmplitude = PlateauDetailAmplitude;
}

UMountainsBiomeDataAsset::UMountainsBiomeDataAsset()
{
	BiomeType = EBiomeType::Mountains;
}

void UMountainsBiomeDataAsset::ApplyTo(FTerrainGenerationSettings& Settings) const
{
	Settings.MountainMaxHeight = MountainMaxHeight;
	Settings.RidgeFrequency = RidgeFrequency;
	Settings.MountainsBaseHillsFrequency = MountainsBaseHillsFrequency;
	Settings.MountainsBaseHillsAmplitude = MountainsBaseHillsAmplitude;
	Settings.MountainsDetailFrequency = MountainsDetailFrequency;
	Settings.MountainsDetailAmplitude = MountainsDetailAmplitude;
}

UDesertBiomeDataAsset::UDesertBiomeDataAsset()
{
	BiomeType = EBiomeType::Desert;
}

void UDesertBiomeDataAsset::ApplyTo(FTerrainGenerationSettings& Settings) const
{
	Settings.DesertDuneFrequency = DesertDuneFrequency;
	Settings.DesertDuneAmplitude = DesertDuneAmplitude;
	Settings.DesertBaseHeight = DesertBaseHeight;
	Settings.DesertDetailFrequency = DesertDetailFrequency;
	Settings.DesertDetailAmplitude = DesertDetailAmplitude;
}

UOasisBiomeDataAsset::UOasisBiomeDataAsset()
{
	BiomeType = EBiomeType::Oasis;
}

void UOasisBiomeDataAsset::ApplyTo(FTerrainGenerationSettings& Settings) const
{
	Settings.OasisSpawnChance = OasisSpawnChance;
	Settings.OasisRadius = OasisRadius;
	Settings.OasisWaterRadius = OasisWaterRadius;
	Settings.OasisMaxDuneHeight = OasisMaxDuneHeight;
}
