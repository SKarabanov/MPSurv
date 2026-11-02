// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Terrain/TerrainChunk.h"
#include "BiomeDataAsset.generated.h"

/**
 * Shape and grass of one biome. Subclasses add only that biome's parameters.
 * The game thread copies the numbers into FTerrainGenerationSettings and the grass meshes stay here; the worker
 * receives neither this asset nor the mesh pointers.
 */
UCLASS(BlueprintType, Abstract)
class MPSURV_API UBiomeDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	/** Writes this biome's fields onto the POD snapshot. The base class has no shape of its own. */
	virtual void ApplyTo(FTerrainGenerationSettings& Settings) const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Biome")
	EBiomeType BiomeType = EBiomeType::Plains;

	/**
	 * Grass and bush meshes for this biome. BiomeId on each entry is overwritten from BiomeType when the manager
	 * builds the snapshot, so a mesh on this asset cannot scatter as another biome.
	 */
	UPROPERTY(EditAnywhere, Category = "Grass")
	TArray<FBiomeGrassEntry> GrassRules;
};

UCLASS(BlueprintType)
class MPSURV_API UOceanBiomeDataAsset : public UBiomeDataAsset
{
	GENERATED_BODY()

public:
	UOceanBiomeDataAsset();

	virtual void ApplyTo(FTerrainGenerationSettings& Settings) const override;

	UPROPERTY(EditAnywhere, Category = "Ocean", meta = (ClampMin = "0.0", Units = "cm"))
	float OceanMaxDepth = 4500.0f;

	UPROPERTY(EditAnywhere, Category = "Ocean", meta = (ClampMin = "0.0"))
	float OceanHillsFrequency = 0.00009f;

	UPROPERTY(EditAnywhere, Category = "Ocean", meta = (ClampMin = "0.0", Units = "cm"))
	float OceanHillsAmplitude = 800.0f;

	UPROPERTY(EditAnywhere, Category = "Ocean", meta = (ClampMin = "0.0"))
	float OceanDetailFrequency = 0.00025f;

	UPROPERTY(EditAnywhere, Category = "Ocean", meta = (ClampMin = "0.0", Units = "cm"))
	float OceanDetailAmplitude = 20.0f;
};

UCLASS(BlueprintType)
class MPSURV_API UPlainsBiomeDataAsset : public UBiomeDataAsset
{
	GENERATED_BODY()

public:
	UPlainsBiomeDataAsset();

	virtual void ApplyTo(FTerrainGenerationSettings& Settings) const override;

	UPROPERTY(EditAnywhere, Category = "Plains", meta = (ClampMin = "0.0"))
	float PlainsHillsFrequency = 0.00009f;

	UPROPERTY(EditAnywhere, Category = "Plains", meta = (ClampMin = "0.0", Units = "cm"))
	float PlainsHillsAmplitude = 400.0f;

	UPROPERTY(EditAnywhere, Category = "Plains", meta = (ClampMin = "0.0"))
	float PlainsDetailFrequency = 0.00025f;

	UPROPERTY(EditAnywhere, Category = "Plains", meta = (ClampMin = "0.0", Units = "cm"))
	float PlainsDetailAmplitude = 200.0f;
};

UCLASS(BlueprintType)
class MPSURV_API UForestBiomeDataAsset : public UBiomeDataAsset
{
	GENERATED_BODY()

public:
	UForestBiomeDataAsset();

	virtual void ApplyTo(FTerrainGenerationSettings& Settings) const override;

	UPROPERTY(EditAnywhere, Category = "Forest", meta = (ClampMin = "0.00001", UIMin = "0.00005", UIMax = "0.001"))
	float ForestHillsFrequency = 0.0002f;

	UPROPERTY(EditAnywhere, Category = "Forest", meta = (ClampMin = "0.0", UIMax = "5000.0", Units = "cm"))
	float ForestHillsHeight = 1400.0f;

	UPROPERTY(EditAnywhere, Category = "Forest", meta = (ClampMin = "0.0"))
	float ForestDetailFrequency = 0.00025f;

	UPROPERTY(EditAnywhere, Category = "Forest", meta = (ClampMin = "0.0", Units = "cm"))
	float ForestDetailAmplitude = 200.0f;
};

UCLASS(BlueprintType)
class MPSURV_API USwampBiomeDataAsset : public UBiomeDataAsset
{
	GENERATED_BODY()

public:
	USwampBiomeDataAsset();

	virtual void ApplyTo(FTerrainGenerationSettings& Settings) const override;

	UPROPERTY(EditAnywhere, Category = "Swamp", meta = (Units = "cm"))
	float SwampBaseHeight = 20.0f;

	UPROPERTY(EditAnywhere, Category = "Swamp", meta = (ClampMin = "0.0", UIMax = "0.005"))
	float SwampPuddleNoiseFrequency = 0.001f;

	UPROPERTY(EditAnywhere, Category = "Swamp", meta = (ClampMin = "0.0", Units = "cm"))
	float SwampPuddleDepth = 80.0f;

	UPROPERTY(EditAnywhere, Category = "Swamp", meta = (ClampMin = "100.0", UIMax = "9000.0", Units = "cm"))
	float SwampBlendWidth = 3000.0f;
};

UCLASS(BlueprintType)
class MPSURV_API UPlateauBiomeDataAsset : public UBiomeDataAsset
{
	GENERATED_BODY()

public:
	UPlateauBiomeDataAsset();

	virtual void ApplyTo(FTerrainGenerationSettings& Settings) const override;

	UPROPERTY(EditAnywhere, Category = "Plateau", meta = (ClampMin = "0.0", Units = "cm"))
	float PlateauHeight = 7000.0f;

	/** 0.000032 is a tract about 300 m apart. */
	UPROPERTY(EditAnywhere, Category = "Plateau", meta = (ClampMin = "0.0"))
	float CanyonFrequency = 0.000032f;

	/**
	 * Half-width of a canyon including its walls, in canyon noise units.
	 * The floor stays below CanyonFloorWidth and the mesa starts above this value.
	 */
	UPROPERTY(EditAnywhere, Category = "Plateau", meta = (ClampMin = "0.0"))
	float CanyonWidth = 0.22f;

	/** Half-width of a canyon's flat floor, in canyon noise units. Must stay below CanyonWidth. */
	UPROPERTY(EditAnywhere, Category = "Plateau", meta = (ClampMin = "0.0"))
	float CanyonFloorWidth = 0.06f;

	UPROPERTY(EditAnywhere, Category = "Plateau", meta = (ClampMin = "0.0"))
	float PlateauHillsFrequency = 0.00009f;

	UPROPERTY(EditAnywhere, Category = "Plateau", meta = (ClampMin = "0.0", Units = "cm"))
	float PlateauHillsAmplitude = 400.0f;

	UPROPERTY(EditAnywhere, Category = "Plateau", meta = (ClampMin = "0.0"))
	float PlateauDetailFrequency = 0.00025f;

	UPROPERTY(EditAnywhere, Category = "Plateau", meta = (ClampMin = "0.0", Units = "cm"))
	float PlateauDetailAmplitude = 200.0f;
};

UCLASS(BlueprintType)
class MPSURV_API UMountainsBiomeDataAsset : public UBiomeDataAsset
{
	GENERATED_BODY()

public:
	UMountainsBiomeDataAsset();

	virtual void ApplyTo(FTerrainGenerationSettings& Settings) const override;

	UPROPERTY(EditAnywhere, Category = "Mountains", meta = (ClampMin = "0.0", Units = "cm"))
	float MountainMaxHeight = 40000.0f;

	UPROPERTY(EditAnywhere, Category = "Mountains", meta = (ClampMin = "0.0"))
	float RidgeFrequency = 0.000016f;

	UPROPERTY(EditAnywhere, Category = "Mountains", meta = (ClampMin = "0.0"))
	float MountainsBaseHillsFrequency = 0.00009f;

	UPROPERTY(EditAnywhere, Category = "Mountains", meta = (ClampMin = "0.0", Units = "cm"))
	float MountainsBaseHillsAmplitude = 400.0f;

	UPROPERTY(EditAnywhere, Category = "Mountains", meta = (ClampMin = "0.0"))
	float MountainsDetailFrequency = 0.00025f;

	UPROPERTY(EditAnywhere, Category = "Mountains", meta = (ClampMin = "0.0", Units = "cm"))
	float MountainsDetailAmplitude = 800.0f;
};

UCLASS(BlueprintType)
class MPSURV_API UDesertBiomeDataAsset : public UBiomeDataAsset
{
	GENERATED_BODY()

public:
	UDesertBiomeDataAsset();

	virtual void ApplyTo(FTerrainGenerationSettings& Settings) const override;

	UPROPERTY(EditAnywhere, Category = "Desert", meta = (ClampMin = "0.0", UIMax = "0.0005"))
	float DesertDuneFrequency = 0.00005f;

	UPROPERTY(EditAnywhere, Category = "Desert", meta = (ClampMin = "0.0", Units = "cm"))
	float DesertDuneAmplitude = 2500.0f;

	UPROPERTY(EditAnywhere, Category = "Desert", meta = (Units = "cm"))
	float DesertBaseHeight = 500.0f;

	UPROPERTY(EditAnywhere, Category = "Desert", meta = (ClampMin = "0.0"))
	float DesertDetailFrequency = 0.00025f;

	UPROPERTY(EditAnywhere, Category = "Desert", meta = (ClampMin = "0.0", Units = "cm"))
	float DesertDetailAmplitude = 50.0f;
};

UCLASS(BlueprintType)
class MPSURV_API UOasisBiomeDataAsset : public UBiomeDataAsset
{
	GENERATED_BODY()

public:
	UOasisBiomeDataAsset();

	virtual void ApplyTo(FTerrainGenerationSettings& Settings) const override;

	UPROPERTY(EditAnywhere, Category = "Oasis", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float OasisSpawnChance = 0.3f;

	UPROPERTY(EditAnywhere, Category = "Oasis", meta = (ClampMin = "0.0", Units = "cm"))
	float OasisRadius = 5000.0f;

	UPROPERTY(EditAnywhere, Category = "Oasis", meta = (ClampMin = "0.0", Units = "cm"))
	float OasisWaterRadius = 1500.0f;

	UPROPERTY(EditAnywhere, Category = "Oasis", meta = (ClampMin = "0.0", Units = "cm"))
	float OasisMaxDuneHeight = 1000.0f;
};

UCLASS(BlueprintType)
class MPSURV_API UTundraBiomeDataAsset : public UBiomeDataAsset
{
	GENERATED_BODY()

public:
	UTundraBiomeDataAsset();

	virtual void ApplyTo(FTerrainGenerationSettings& Settings) const override;

	UPROPERTY(EditAnywhere, Category = "Tundra", meta = (ClampMin = "0.0"))
	float TundraHillsFrequency = 0.00009f;

	UPROPERTY(EditAnywhere, Category = "Tundra", meta = (ClampMin = "0.0", Units = "cm"))
	float TundraHillsAmplitude = 400.0f;

	UPROPERTY(EditAnywhere, Category = "Tundra", meta = (ClampMin = "0.0"))
	float TundraDetailFrequency = 0.00025f;

	UPROPERTY(EditAnywhere, Category = "Tundra", meta = (ClampMin = "0.0", Units = "cm"))
	float TundraDetailAmplitude = 200.0f;
};

UCLASS(BlueprintType)
class MPSURV_API UJungleBiomeDataAsset : public UBiomeDataAsset
{
	GENERATED_BODY()

public:
	UJungleBiomeDataAsset();

	virtual void ApplyTo(FTerrainGenerationSettings& Settings) const override;

	UPROPERTY(EditAnywhere, Category = "Jungle", meta = (ClampMin = "0.00001", UIMin = "0.00005", UIMax = "0.001"))
	float JungleHillsFrequency = 0.0002f;

	UPROPERTY(EditAnywhere, Category = "Jungle", meta = (ClampMin = "0.0", UIMax = "5000.0", Units = "cm"))
	float JungleHillsHeight = 1400.0f;

	UPROPERTY(EditAnywhere, Category = "Jungle", meta = (ClampMin = "0.0"))
	float JungleDetailFrequency = 0.00025f;

	UPROPERTY(EditAnywhere, Category = "Jungle", meta = (ClampMin = "0.0", Units = "cm"))
	float JungleDetailAmplitude = 200.0f;
};

UCLASS(BlueprintType)
class MPSURV_API UFoothillsBiomeDataAsset : public UBiomeDataAsset
{
	GENERATED_BODY()

public:
	UFoothillsBiomeDataAsset();

	virtual void ApplyTo(FTerrainGenerationSettings& Settings) const override;

	UPROPERTY(EditAnywhere, Category = "Foothills", meta = (ClampMin = "0.0"))
	float FoothillsDetailFrequency = 0.00025f;

	UPROPERTY(EditAnywhere, Category = "Foothills", meta = (ClampMin = "0.0", Units = "cm"))
	float FoothillsDetailAmplitude = 200.0f;
};
