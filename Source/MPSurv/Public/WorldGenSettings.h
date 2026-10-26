// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TerrainChunk.h"
#include "WorldGenSettings.generated.h"

/**
 * Shared world shape: continentalness, the start island, the Voronoi layout, desert regions, oasis spacing and rivers.
 * One asset for the whole world. Per-biome shape lives on UBiomeDataAsset. The game thread copies these numbers into
 * FTerrainGenerationSettings before a chunk build; the worker never sees this asset.
 */
UCLASS(BlueprintType)
class MPSURV_API UWorldGenSettings : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	/** Writes this asset onto the POD snapshot the worker already receives. Missing asset keeps the struct defaults. */
	void ApplyTo(FTerrainGenerationSettings& Settings) const;

	UPROPERTY(EditAnywhere, Category = "Mesh", meta = (ClampMin = "0.01"))
	float NormalStrength = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Mesh", meta = (ClampMin = "0.0", Units = "cm"))
	float SkirtDepth = 1000.0f;

	/** Frequency of the continentalness mask that shapes islands and oceans. */
	UPROPERTY(EditAnywhere, Category = "Continentalness", meta = (ClampMin = "0.0"))
	float MacroFrequency = 0.000012f;

	/** Normalized continentalness below which terrain is ocean floor. */
	UPROPERTY(EditAnywhere, Category = "Continentalness", meta = (ClampMin = "0.01", ClampMax = "0.99"))
	float OceanThreshold = 0.50f;

	/**
	 * Normalized continentalness where land biomes reach their full height. Between OceanThreshold and this value
	 * every biome ramps up from the waterline.
	 */
	UPROPERTY(EditAnywhere, Category = "Continentalness", meta = (ClampMin = "0.01", ClampMax = "0.99"))
	float MountainThreshold = 0.66f;

	UPROPERTY(EditAnywhere, Category = "Start Island", meta = (ClampMin = "1.0", Units = "cm"))
	float StartIslandRadius = 250000.0f;

	UPROPERTY(EditAnywhere, Category = "Start Island", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StartIslandStrength = 0.3f;

	/** No river straits within this distance of the world origin; they fade out over the next 200 m. */
	UPROPERTY(EditAnywhere, Category = "Start Island", meta = (ClampMin = "0.0", Units = "cm"))
	float StraitFreeRadius = 30000.0f;

	/** Spacing of the jittered Voronoi biome sites laid over the land. */
	UPROPERTY(EditAnywhere, Category = "Biomes", meta = (ClampMin = "1000.0", Units = "cm"))
	float BiomeCellSize = 60000.0f;

	UPROPERTY(EditAnywhere, Category = "Biomes", meta = (ClampMin = "0.0", Units = "cm"))
	float CenterPlainsRadius = 15000.0f;

	UPROPERTY(EditAnywhere, Category = "Biomes", meta = (ClampMin = "1.0", Units = "cm"))
	float BiomeFalloffRadius = 150000.0f;

	/** How far a low-frequency warp shifts a site before distance and latitude are measured. */
	UPROPERTY(EditAnywhere, Category = "Biomes", meta = (ClampMin = "0.0", Units = "cm"))
	float BiomeWarpAmplitude = 120000.0f;

	/** Sites closer than this to the world origin are plains. */
	UPROPERTY(EditAnywhere, Category = "Biomes", meta = (ClampMin = "0.0", Units = "cm"))
	float PlainsRadius = 200000.0f;

	/** Kept so existing assets still load. The northern band is TundraLatitude. */
	UPROPERTY(EditAnywhere, Category = "Biomes", meta = (Units = "cm"))
	float MountainLatitude = 400000.0f;

	/** Warped Y below this is desert. */
	UPROPERTY(EditAnywhere, Category = "Biomes", meta = (Units = "cm"))
	float DesertLatitude = -400000.0f;

	/** Inner radius of the forest ring. */
	UPROPERTY(EditAnywhere, Category = "Biomes", meta = (ClampMin = "0.0", Units = "cm"))
	float ForestMinRadius = 150000.0f;

	/** Outer radius of the forest ring. */
	UPROPERTY(EditAnywhere, Category = "Biomes", meta = (ClampMin = "0.0", Units = "cm"))
	float ForestMaxRadius = 600000.0f;

	/** Past this distance, and outside the forest ring, cells split between swamp and plateau. */
	UPROPERTY(EditAnywhere, Category = "Biomes", meta = (ClampMin = "0.0", Units = "cm"))
	float SwampPlateauRadius = 400000.0f;

	/** Inner radius of the mountain ring. */
	UPROPERTY(EditAnywhere, Category = "Biomes", meta = (ClampMin = "0.0", Units = "cm"))
	float MountainMinRadius = 400000.0f;

	/** Outer radius of the mountain ring. */
	UPROPERTY(EditAnywhere, Category = "Biomes", meta = (ClampMin = "0.0", Units = "cm"))
	float MountainMaxRadius = 550000.0f;

	/** Warped Y above this is tundra. */
	UPROPERTY(EditAnywhere, Category = "Biomes", meta = (Units = "cm"))
	float TundraLatitude = 500000.0f;

	/** Warped X above this, on the southern band, is jungle. Desert lies to the west. */
	UPROPERTY(EditAnywhere, Category = "Biomes", meta = (Units = "cm"))
	float JungleLongitude = 0.0f;

	/** Mountain ground below this height is written as foothills. */
	UPROPERTY(EditAnywhere, Category = "Biomes", meta = (ClampMin = "0.0", Units = "cm"))
	float FoothillsMaxHeight = 8000.0f;

	/**
	 * Smallest desert across. Every biome cell whose site falls in a desert region becomes desert, so a desert is
	 * at least one cell (BiomeCellSize) even when this is smaller.
	 */
	UPROPERTY(EditAnywhere, Category = "Desert Regions", meta = (ClampMin = "0.0", Units = "cm"))
	float DesertRegionMinSize = 60000.0f;

	/** Largest desert across. Desert regions are spaced 1.5x this apart, so larger deserts are also sparser. */
	UPROPERTY(EditAnywhere, Category = "Desert Regions", meta = (ClampMin = "1000.0", Units = "cm"))
	float DesertRegionMaxSize = 200000.0f;

	/** Chance that a desert region exists in the outer world; it fades out towards the start island. */
	UPROPERTY(EditAnywhere, Category = "Desert Regions", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DesertRegionChance = 0.6f;

	/** Spacing of the grid on which oasis candidates are placed, one per cell. */
	UPROPERTY(EditAnywhere, Category = "Oasis", meta = (ClampMin = "1000.0", Units = "cm"))
	float OasisGridSize = 30000.0f;

	UPROPERTY(EditAnywhere, Category = "Rivers", meta = (ClampMin = "0.0", UIMax = "0.00003"))
	float RiverFrequency = 0.000006f;

	UPROPERTY(EditAnywhere, Category = "Rivers", meta = (ClampMin = "0.0", UIMax = "20000.0", Units = "cm"))
	float RiverWidth = 4000.0f;

	UPROPERTY(EditAnywhere, Category = "Rivers", meta = (ClampMin = "100.0", UIMax = "30000.0", Units = "cm"))
	float RiverShoreWidth = 6000.0f;

	UPROPERTY(EditAnywhere, Category = "Rivers", meta = (ClampMin = "0.0", Units = "cm"))
	float RiverDepth = 1000.0f;
};
