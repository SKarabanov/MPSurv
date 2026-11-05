// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <atomic>

#include "CoreMinimal.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "RealtimeMeshComponent.h"
#include "RealtimeMeshSimple.h"
#include "TerrainChunk.generated.h"

class ATerrainManager;
class UMaterialInterface;
class UPCGComponent;
class UStaticMesh;

/**
 * Strict biome ID. Ocean comes from the continentalness mask; the land biomes come from the Voronoi grid.
 * Vertex colour no longer stores a single ID: R and G are the two heaviest biomes sorted by ID, and B is the weight of G.
 */
UENUM(BlueprintType)
enum class EBiomeType : uint8
{
	Ocean = 0,
	Plains = 1,
	Forest = 2,
	Swamp = 3,
	Plateau = 4,
	Mountains = 5,
	Desert = 6,

	/** Sub-biome of the desert: never drawn for a Voronoi cell, only written for ground within an oasis radius. */
	Oasis = 7,

	Tundra = 8,
	Jungle = 9,

	/** Sub-biome of the mountains: never drawn for a Voronoi cell, only where a mountain stands below FoothillsMaxHeight. */
	Foothills = 10
};

/** Number of EBiomeType values; indexes PossiblePOIs on FTerrainGenerationSettings. */
inline constexpr int32 BiomeTypeCount = static_cast<int32>(EBiomeType::Foothills) + 1;

/** Kind of point of interest that can flatten terrain and spawn an actor. */
UENUM(BlueprintType)
enum class EPOIType : uint8
{
	None,
	Altar,
	Ruins,
	Dungeon
};

/**
 * One weighted POI variant for a biome. Soft class resolves on the game thread; the worker reads Type, Weight and radii only.
 */
USTRUCT(BlueprintType)
struct FPOIData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "POI")
	EPOIType Type = EPOIType::None;

	/** Relative chance among PossiblePOIs of the same biome. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "POI", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;

	/** Radius of the perfectly flat pad under the foundation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "POI", meta = (ClampMin = "0.0", Units = "cm"))
	float FlattenRadius = 2000.0f;

	/** Width of the blend from the flat pad back into procedural height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "POI", meta = (ClampMin = "0.0", Units = "cm"))
	float BlendRadius = 1500.0f;

	/** Actor spawned on the flattened site. Unresolved soft path; never LoadSynchronous on a worker. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "POI")
	TSoftClassPtr<AActor> POIActorClass;
};

/** Voronoi biomes at a point. */
struct FBiomeContext
{
	/** Biome of the nearest Voronoi site. */
	EBiomeType PrimaryBiome = EBiomeType::Plains;

	/** Biome of the second nearest Voronoi site. */
	EBiomeType SecondaryBiome = EBiomeType::Plains;

	/** Dist2 - Dist1 to the two nearest sites: 0 on the cell border, growing towards the cell centre. */
	float DistanceToEdge = 0.0f;
};

/** A Voronoi biome site near a query point. */
struct FBiomeSite
{
	EBiomeType Biome = EBiomeType::Plains;
	float Distance = 0.0f;
};

/** One biome and its share of a ring. A higher weight claims a larger slice of that ring's noise. */
USTRUCT(BlueprintType)
struct FBiomeWeight
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome")
	EBiomeType Biome = EBiomeType::Plains;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;
};

/**
 * Land from the previous ring's MaxRadius out to this one. Biomes lists who may appear here and how often.
 * Rings are tested from the smallest MaxRadius outward, so the array order in the asset does not matter.
 */
USTRUCT(BlueprintType)
struct FRingZoneSettings
{
	GENERATED_BODY()

	/** Outer edge of this ring, in centimetres from the world origin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome", meta = (ClampMin = "0.0", Units = "cm"))
	float MaxRadius = 200000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome")
	TArray<FBiomeWeight> Biomes;

	/** Plains, then forest with clearings, then mountains and forest, then swamp and plateau out to 1000 km. */
	static TArray<FRingZoneSettings> MakeDefaults();
};

/** Everything a client needs to rebuild a chunk deterministically. Replicated as one unit. */
USTRUCT(BlueprintType)
struct FChunkInitData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	int32 Seed = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	FIntPoint Coords = FIntPoint(0, 0);

	/**
	 * The first mesh build runs on the game thread instead of a worker, so the chunk has geometry and collision
	 * as soon as it is initialized. For start chunks that players spawn on; later rebuilds are asynchronous.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	bool bForceSynchronous = false;
};


/**
 * Terrain generation parameters. Copied by value into every background build, so workers never read an asset.
 * The game thread fills this from UWorldGenSettings and the biome data assets. The values below are the fallback
 * when an asset is unassigned.
 */
USTRUCT(BlueprintType)
struct FTerrainGenerationSettings
{
	GENERATED_BODY()

	FTerrainGenerationSettings();

	// --- Mesh / shading ---

	/** Scales slope in the shading normals. Above 1 exaggerates relief, below 1 flattens it. */
	UPROPERTY(EditDefaultsOnly, Category = "Terrain|Mesh", meta = (ClampMin = "0.01"))
	float NormalStrength = 1.0f;

	/** How far the perimeter skirt hangs below the edge vertices, hiding cracks between neighbouring LODs. */
	UPROPERTY(EditDefaultsOnly, Category = "Terrain|Mesh", meta = (ClampMin = "0.0", Units = "cm"))
	float SkirtDepth = 1000.0f;

	// --- Continentalness (shared by all biomes) ---

	/** Frequency of the continentalness mask that shapes islands and oceans. */
	UPROPERTY(EditDefaultsOnly, Category = "Terrain|Continentalness", meta = (ClampMin = "0.0"))
	float MacroFrequency = 0.000012f;

	/** Normalized continentalness below which terrain is ocean floor. */
	UPROPERTY(EditDefaultsOnly, Category = "Terrain|Continentalness", meta = (ClampMin = "0.01", ClampMax = "0.99"))
	float OceanThreshold = 0.50f;

	/**
	 * Normalized continentalness where land biomes reach their full height. Between OceanThreshold and this value
	 * every biome ramps up from the waterline.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Terrain|Continentalness", meta = (ClampMin = "0.01", ClampMax = "0.99"))
	float MountainThreshold = 0.66f;

	// --- Start island ---

	/**
	 * Reach of the start island around the world origin. Within it the continentalness is lifted, most strongly
	 * at the centre. The lift does not change which biome the rings and latitude bands pick.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Terrain|Start Island", meta = (ClampMin = "1.0", Units = "cm"))
	float StartIslandRadius = 250000.0f;

	/**
	 * Continentalness added at the world origin, fading out towards StartIslandRadius. Raised automatically when a
	 * seed needs more to put the origin on land at full height.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Terrain|Start Island", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StartIslandStrength = 0.3f;

	/** No river straits within this distance of the world origin; they fade out over the next 200 m. */
	UPROPERTY(EditDefaultsOnly, Category = "Terrain|Start Island", meta = (ClampMin = "0.0", Units = "cm"))
	float StraitFreeRadius = 30000.0f;

	// --- Voronoi biome layout (shared) ---

	/** Spacing of the jittered Voronoi biome sites laid over the land. */
	UPROPERTY(EditDefaultsOnly, Category = "Terrain|Biomes", meta = (ClampMin = "1000.0", Units = "cm"))
	float BiomeCellSize = 60000.0f;

	/** How far a low-frequency warp shifts a site before distance and latitude are measured, so ring borders stay ragged. */
	UPROPERTY(EditDefaultsOnly, Category = "Terrain|Biomes", meta = (ClampMin = "0.0", Units = "cm"))
	float BiomeWarpAmplitude = 120000.0f;

	/**
	 * Concentric rings, each with its own biome weights. The fallback when no world-settings asset is loaded.
	 * Filled in the constructor so a missing asset still has a playable layout.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Terrain|Biomes")
	TArray<FRingZoneSettings> RingZones;

	/** Warped Y below this is desert to the west and jungle to the east. */
	UPROPERTY(EditDefaultsOnly, Category = "Terrain|Biomes", meta = (Units = "cm"))
	float DesertLatitude = -400000.0f;

	/** Warped Y above this is tundra. */
	UPROPERTY(EditDefaultsOnly, Category = "Terrain|Biomes", meta = (Units = "cm"))
	float TundraLatitude = 500000.0f;

	/** Warped X above this, on the southern band, is jungle. Desert lies to the west. */
	UPROPERTY(EditDefaultsOnly, Category = "Terrain|Biomes", meta = (Units = "cm"))
	float JungleLongitude = 0.0f;

	/** Mountain ground below this height is written as foothills. */
	UPROPERTY(EditDefaultsOnly, Category = "Terrain|Biomes", meta = (ClampMin = "0.0", Units = "cm"))
	float FoothillsMaxHeight = 8000.0f;

	/**
	 * Weighted POI variants for each biome. Index is EBiomeType.
	 * Copied from UBiomeDataAsset::PossiblePOIs on the game thread; FTerrainGenerator reads flatten radii only.
	 */
	TArray<FPOIData> PossiblePOIs[BiomeTypeCount];

	// Per-biome shape. Filled on the game thread from the matching UBiomeDataAsset.
	// Oasis spacing is world layout and comes from UWorldGenSettings.
	// These values are the fallback when that asset is missing.

	// --- Ocean ---

	/** Depth of the ocean floor at a continentalness of 0, in world units. */
	float OceanMaxDepth = 4500.0f;

	/** Frequency of underwater hills on the ocean bed. */
	float OceanHillsFrequency = 0.00009f;

	/** Height of underwater hills; they fade out towards the shoreline. */
	float OceanHillsAmplitude = 800.0f;

	/** Frequency of fine roughness on the deep ocean floor. */
	float OceanDetailFrequency = 0.00025f;

	/** Amplitude of fine roughness on the deep ocean floor; fades to 0 at the shore. */
	float OceanDetailAmplitude = 20.0f;

	// --- Plains ---

	/** Frequency of rolling plains hills. */
	float PlainsHillsFrequency = 0.00009f;

	/** Height of rolling plains hills deep inside a plains cell. */
	float PlainsHillsAmplitude = 400.0f;

	/** Frequency of fine surface roughness on the plains. */
	float PlainsDetailFrequency = 0.00025f;

	/** Amplitude of fine surface roughness on the plains. */
	float PlainsDetailAmplitude = 200.0f;

	// --- Tundra ---

	/** Frequency of rolling tundra hills. */
	float TundraHillsFrequency = 0.00009f;

	/** Height of rolling tundra hills deep inside a tundra cell. */
	float TundraHillsAmplitude = 400.0f;

	/** Frequency of fine surface roughness on the tundra. */
	float TundraDetailFrequency = 0.00025f;

	/** Amplitude of fine surface roughness on the tundra. */
	float TundraDetailAmplitude = 200.0f;

	// --- Forest ---

	/** Frequency of forest hills. Higher values give smaller, more frequent hills; 0.0002 is a ~50 m wavelength. */
	float ForestHillsFrequency = 0.0002f;

	/** Height difference between forest hollows and hill tops deep inside a forest cell. */
	float ForestHillsHeight = 1400.0f;

	/** Frequency of fine surface roughness in the forest. */
	float ForestDetailFrequency = 0.00025f;

	/** Amplitude of fine surface roughness in the forest. */
	float ForestDetailAmplitude = 200.0f;

	// --- Jungle ---

	/** Frequency of jungle hills. Higher values give smaller, more frequent hills. */
	float JungleHillsFrequency = 0.0002f;

	/** Height difference between jungle hollows and hill tops deep inside a jungle cell. */
	float JungleHillsHeight = 1400.0f;

	/** Frequency of fine surface roughness in the jungle. */
	float JungleDetailFrequency = 0.00025f;

	/** Amplitude of fine surface roughness in the jungle. */
	float JungleDetailAmplitude = 200.0f;

	// --- Swamp ---

	/** Level of the flat swamp lowland above the sea; continentalness does not lift it. */
	float SwampBaseHeight = 20.0f;

	/** Frequency of the hummocks and puddles on the swamp lowland; 0.001 is a ~10 m wavelength. */
	float SwampPuddleNoiseFrequency = 0.001f;

	/**
	 * How far hummocks rise above and puddles sink below SwampBaseHeight. Ground pushed below sea level fills with
	 * sea water and forms puddles.
	 */
	float SwampPuddleDepth = 80.0f;

	/**
	 * Width over which a swamp blends into its neighbours. Narrower than the regular biome blend, so the ground
	 * drops into the swamp as a steep mud bank. Never wider than the regular blend.
	 */
	float SwampBlendWidth = 3000.0f;

	// --- Plateau ---

	/** Height of the mesa above the surrounding plains. */
	float PlateauHeight = 7000.0f;

	/** Frequency of the canyon network carved into plateaus. 0.000032 is a tract about 300 m apart. */
	float CanyonFrequency = 0.000032f;

	/**
	 * Half-width of a canyon including its walls, in canyon noise units.
	 * The floor stays below CanyonFloorWidth and the mesa starts above this value, so the wall is only the band
	 * between them. 0.22 leaves a flat table and a flat floor; the drop itself is a little wider than the old 0.16.
	 */
	float CanyonWidth = 0.22f;

	/** Half-width of a canyon's flat floor, in canyon noise units. Must stay below CanyonWidth. */
	float CanyonFloorWidth = 0.06f;

	/** Frequency of soft hills on the mesa top and canyon floors. */
	float PlateauHillsFrequency = 0.00009f;

	/** Height of soft hills on the mesa; further damped on the table top and less so on canyon floors. */
	float PlateauHillsAmplitude = 400.0f;

	/** Frequency of fine surface roughness on plateaus. */
	float PlateauDetailFrequency = 0.00025f;

	/** Amplitude of fine surface roughness on plateaus. */
	float PlateauDetailAmplitude = 200.0f;

	// --- Mountains ---

	/** Elevation of mountain peaks deep inside a mountain cell at the highest continentalness. */
	float MountainMaxHeight = 40000.0f;

	/** Frequency of the ridged noise that shapes mountain ranges, across the range. Along the range it is 0.45x. */
	float RidgeFrequency = 0.000016f;

	/** Frequency of plains-scale hills at the mountain cell border; they fade out uphill. */
	float MountainsBaseHillsFrequency = 0.00009f;

	/** Height of plains-scale hills at the mountain cell border. */
	float MountainsBaseHillsAmplitude = 400.0f;

	/** Frequency of fine surface roughness on mountains. */
	float MountainsDetailFrequency = 0.00025f;

	/** Amplitude of fine surface roughness on mountains. */
	float MountainsDetailAmplitude = 800.0f;

	/** Frequency of fine surface roughness on foothills. Milder than the peaks. */
	float FoothillsDetailFrequency = 0.00025f;

	/** Amplitude of fine surface roughness on foothills. */
	float FoothillsDetailAmplitude = 200.0f;

	// --- Desert ---

	/** Frequency of desert dunes. Very low for long dune ridges; 0.00005 is a ~200 m wavelength. */
	float DesertDuneFrequency = 0.00005f;

	/** Height of dune crests above the desert base deep inside a desert cell. */
	float DesertDuneAmplitude = 2500.0f;

	/** Level of the desert floor between the dunes above the sea. */
	float DesertBaseHeight = 500.0f;

	/** Frequency of fine surface roughness on the dunes. */
	float DesertDetailFrequency = 0.00025f;

	/** Amplitude of fine surface roughness on the dunes. */
	float DesertDetailAmplitude = 50.0f;

	// --- Oasis (desert sub-biome) ---

	/** Spacing of the grid on which oasis candidates are placed, one per cell. */
	float OasisGridSize = 30000.0f;

	/** Chance that a grid cell holds an oasis candidate. Candidates are then kept only in desert hollows. */
	float OasisSpawnChance = 0.3f;

	/** Full radius of an oasis: water, bank and the flat shore where dunes level out. Capped at OasisGridSize. */
	float OasisRadius = 5000.0f;

	/** Radius of the pond in the middle of an oasis, at the waterline. Capped at 80% of OasisRadius. */
	float OasisWaterRadius = 1500.0f;

	/** An oasis is kept only where the dunes at its centre rise less than this above the desert base. */
	float OasisMaxDuneHeight = 1000.0f;

	// --- River straits (shared) ---

	/**
	 * Frequency of the river straits: sea channels at Z = 0 that cut the land from ocean to ocean and split it into
	 * islands. Lower values space them farther apart; 0.000006 gives roughly one strait per kilometre. 0 disables them.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Terrain|Rivers", meta = (ClampMin = "0.0", UIMax = "0.00003"))
	float RiverFrequency = 0.000006f;

	/** Average water width of a strait at the waterline. Varies between 0.7x and 1.3x along its course. 0 disables them. */
	UPROPERTY(EditDefaultsOnly, Category = "Terrain|Rivers", meta = (ClampMin = "0.0", UIMax = "20000.0", Units = "cm"))
	float RiverWidth = 4000.0f;

	/** Width of the beach on each side of a strait, over which the land rises from the water to full biome height. */
	UPROPERTY(EditDefaultsOnly, Category = "Terrain|Rivers", meta = (ClampMin = "100.0", UIMax = "30000.0", Units = "cm"))
	float RiverShoreWidth = 6000.0f;

	/** Depth of the strait bed below sea level along its middle. Capped by OceanMaxDepth. */
	UPROPERTY(EditDefaultsOnly, Category = "Terrain|Rivers", meta = (ClampMin = "0.0", Units = "cm"))
	float RiverDepth = 1000.0f;
};

/** Basin morphology of a mountain lake. */
enum class EMountainLakeType : uint8
{
	/** Elongated fault basin aligned with the ridges. */
	TectonicRift,
	/** Asymmetric horseshoe basin with a steep back wall. */
	GlacialCirque
};

/** Deterministic mountain lake placed on a hashed 400 m cell grid. */
struct FMountainLakeData
{
	bool bExists = false;
	FVector2D Center = FVector2D::ZeroVector;
	float Radius = 0.0f;
	float WaterZ = 0.0f;
	float BedDepth = 0.0f;

	EMountainLakeType LakeType = EMountainLakeType::TectonicRift;

	/** Major to minor semi-axis ratio of a rift; Radius is the major semi-axis. */
	float AspectRatio = 2.2f;

	/** Direction of a cirque's back wall, in radians. */
	float CirqueAngle = 0.0f;
};

/**
 * Blueprint entry that binds a biome ID to a grass/bush mesh and the placement rules used on the worker thread.
 * The mesh pointer stays on the game thread; only the numeric fields are copied into FTerrainBuildRequest.
 */
USTRUCT(BlueprintType)
struct FBiomeGrassEntry
{
	GENERATED_BODY()

	/** Strict EBiomeType value written to terrain vertex colour R (1 = Plains, 2 = Forest, ...). Ocean (0) is ignored. */
	UPROPERTY(EditDefaultsOnly, Category = "Grass")
	uint8 BiomeId = 1;

	/** Mesh instanced by the HISM created for this biome. */
	UPROPERTY(EditDefaultsOnly, Category = "Grass")
	TObjectPtr<UStaticMesh> Mesh = nullptr;

	/**
	 * Spacing of the independent grass sample grid for this biome, in world units.
	 * 30 = dense forest carpet, 100 = moderate, 300 = sparse desert bushes. Independent of terrain LOD.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Grass", meta = (ClampMin = "10.0", Units = "cm"))
	float SpawnStep = 50.0f;

	/** Chance to place an instance at a sample that already passes height, slope, and the noise patch. */
	UPROPERTY(EditDefaultsOnly, Category = "Grass", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SpawnChance = 0.35f;

	/**
	 * World-space frequency of the grass patch noise, multiplied by position in centimeters.
	 * 0.005 is one patch about every 2 m. 0 disables the patch test.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Grass", meta = (ClampMin = "0.0"))
	float GrassNoiseFrequency = 0.005f;

	/**
	 * Samples whose normalized Perlin value is below this are left bare.
	 * 0 fills the whole cell, 1 leaves almost no grass.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Grass", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GrassDensityThreshold = 0.4f;

	/** Lowest surface height that still receives grass, in world units. */
	UPROPERTY(EditDefaultsOnly, Category = "Grass", meta = (Units = "cm"))
	float MinHeight = 50.0f;

	/** Highest surface height that still receives grass, in world units. */
	UPROPERTY(EditDefaultsOnly, Category = "Grass", meta = (Units = "cm"))
	float MaxHeight = 8000.0f;

	/**
	 * Minimum Dot(Normal, Up). 1 is flat ground, 0.8 is about 37 degrees; steeper slopes get no grass.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Grass", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinUpDot = 0.8f;

	UPROPERTY(EditDefaultsOnly, Category = "Grass", meta = (ClampMin = "0.01"))
	float ScaleMin = 0.85f;

	UPROPERTY(EditDefaultsOnly, Category = "Grass", meta = (ClampMin = "0.01"))
	float ScaleMax = 1.25f;

	/** Distance where HISM instances start fading out (UE instance start cull distance). */
	UPROPERTY(EditDefaultsOnly, Category = "Grass|Culling", meta = (ClampMin = "0"))
	int32 CullDistanceStart = 8000;

	/** Distance where HISM instances are fully culled. Should be >= CullDistanceStart. */
	UPROPERTY(EditDefaultsOnly, Category = "Grass|Culling", meta = (ClampMin = "0"))
	int32 CullDistanceEnd = 12000;

	/** Master switch for grass instance shadows (HISM SetCastShadow). */
	UPROPERTY(EditDefaultsOnly, Category = "Grass|Shadows")
	bool bCastShadow = false;

	/** Dynamic shadows from movable lights / CSM. Costly with dense HISM grass. */
	UPROPERTY(EditDefaultsOnly, Category = "Grass|Shadows", meta = (EditCondition = "bCastShadow"))
	bool bCastDynamicShadow = true;

	/** Static/baked shadows. Usually off for procedural grass. */
	UPROPERTY(EditDefaultsOnly, Category = "Grass|Shadows", meta = (EditCondition = "bCastShadow"))
	bool bCastStaticShadow = false;
};

/** POD placement rules copied into a build request; no UObject pointers, safe on a worker thread. */
struct FBiomeGrassSpawnRule
{
	uint8 BiomeId = 1;
	float SpawnStep = 50.0f;
	float SpawnChance = 0.35f;
	float GrassNoiseFrequency = 0.005f;
	float GrassDensityThreshold = 0.4f;
	float MinHeight = 50.0f;
	float MaxHeight = 8000.0f;
	float MinUpDot = 0.8f;
	float ScaleMin = 0.85f;
	float ScaleMax = 1.25f;
};

/**
 * One node of the full-detail surface. A chunk build samples this once on a 67×67 grid
 * (LOD 0 vertices plus a one-cell border). LOD meshes, collision and grass read the stored node.
 */
struct FTerrainGridSample
{
	float Height = 0.0f;

	/** Central difference over ±CellSize, unit length. */
	FVector Normal = FVector::UpVector;

	/** Unit tangent perpendicular to Normal. */
	FVector Tangent = FVector::ForwardVector;

	/** Packed biome pair: R is the lower id, G the higher id, B the weight of G. */
	FColor VertexColor = FColor::Black;

	/** Strict biome: sea, flooded shore, oasis, foothills, otherwise the nearest cell. */
	uint8 StrictBiome = 0;
};

/** What one chunk build should produce, decided on the game thread and copied to the worker. */
struct FTerrainBuildRequest
{
	FTerrainGenerationSettings Settings;

	/** Dedicated server: height grid only. Collision is built from that grid later. No skirts, no water, no visual LODs. */
	bool bDedicatedServer = false;

	/**
	 * Rendering machines: one stream set per visual LOD for terrain (with skirts) and one for water.
	 * The renderer selects the LOD from ScreenSize. This build does not run again when the camera moves.
	 */
	bool bBuildAllVisualLODs = false;

	/**
	 * Grass placement rules for the worker. Empty on a dedicated server or when the actor has no grass entries.
	 * No mesh pointers: the game thread maps BiomeId to HISM after the build returns.
	 */
	TArray<FBiomeGrassSpawnRule> GrassRules;

	/**
	 * When true, the worker places grass from the 65×65 grid during this build.
	 * Set on the first client build, including a coarse shown LOD, so a later approach does not rebuild.
	 */
	bool bBuildGrass = false;
};

/** Geometry produced by one chunk build. Stream sets are filled on the worker and moved to the game thread. */
struct FTerrainBuildResult
{
	/** Terrain with skirts, one entry per visual LOD. Empty on a dedicated server. */
	TArray<RealtimeMesh::FRealtimeMeshStreamSet> TerrainLODs;

	/** Water for the same LOD. An entry with no position stream is a dry LOD. */
	TArray<RealtimeMesh::FRealtimeMeshStreamSet> WaterLODs;

	/**
	 * Grass instance transforms in actor-local space, keyed by biome ID.
	 * Filled when the request asked for grass; otherwise left empty so existing HISM instances stay.
	 */
	TMap<uint8, TArray<FTransform>> GrassTransforms;

	/**
	 * Per-instance custom data float 0 (wind phase in [0, 1]), parallel to GrassTransforms for the same biome ID.
	 * Float 1 (flatten/interaction) is written as 0 on the game thread until terraforming exists.
	 */
	TMap<uint8, TArray<float>> GrassWindPhases;

	/**
	 * Inner 65×65 LOD 0 nodes, row-major, without the one-cell border.
	 * Moved onto the chunk on the game thread in ApplyMesh.
	 */
	TArray<FTerrainGridSample> CachedLOD0Grid;
};

/**
 * Replicated terrain chunk. The mesh is rebuilt locally from InitData so every machine
 * produces the same surface without replicating vertex data.
 * Geometry is built in actor-local space; the spawner must place the actor at
 * InitData.Coords * ChunkWorldSize.
 *
 * Heights and geometry are computed on a worker thread by CalculateTerrainData and applied on the game thread;
 * start chunks can force their first build to run synchronously (FChunkInitData::bForceSynchronous).
 *
 * Visual LOD is selected by the renderer from ScreenSize. Collision is complex mesh data from the 65×65 grid,
 * cooked once and then toggled. Grass follows the same 2-chunk radius: the server via SetCollisionEnabled,
 * a client via the local pawn distance.
 */
UCLASS()
class MPSURV_API ATerrainChunk : public AActor
{
	GENERATED_BODY()

public:
	ATerrainChunk();

	/** Side of a chunk in world units. Fixed, so the chunk grid and streaming do not depend on mesh density. */
	static constexpr float ChunkWorldSize = 6400.0f;

	/** Distance between adjacent vertices at LOD 0, in world units. */
	static constexpr float CellSize = 100.0f;

	/** Quads along one side of the chunk at LOD 0, derived from the chunk size and grid step. Each further LOD halves it. */
	static constexpr int32 QuadsPerSide = static_cast<int32>(ChunkWorldSize / CellSize);

	/** LOD 0 is 64x64 quads (1 m), LOD 1 is 32x32 (2 m) and LOD 2 is 16x16 (4 m); all sample the same full-detail surface. */
	static constexpr int32 NumLODs = 3;

	static_assert(static_cast<float>(QuadsPerSide) * CellSize == ChunkWorldSize, "CellSize must divide ChunkWorldSize evenly.");
	static_assert(QuadsPerSide % (1 << (NumLODs - 1)) == 0, "QuadsPerSide must stay divisible by every LOD reduction.");

	/** Inner 65×65 LOD 0 nodes from the last build. PCG samples this surface. */
	const TArray<FTerrainGridSample>& GetCachedLOD0Grid() const { return CachedLOD0Grid; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server-only. Assigns init data and starts the first mesh build (synchronous if InData.bForceSynchronous). */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Terrain")
	void InitializeChunk(const FChunkInitData& InData);

	/**
	 * Editor viewport only. Builds the mesh on the calling thread from InData.
	 * Collision is not cooked and grass HISMs are not created.
	 * Call this before FinishSpawning. PCG runs afterwards, from GenerateEditorPreviewPCG.
	 */
	void InitializeEditorPreview(const FChunkInitData& InData, bool bRunPCG);

	/** Editor viewport only. Scatters the chunk graph. The actor must already have finished spawning. */
	void GenerateEditorPreviewPCG();

	/** Drops this chunk from the PCG tracking set before the actor is destroyed. */
	void ReleaseEditorPreviewPCG();

	/**
	 * Game thread. Returns the chunk to the manager pool: drops in-flight results via BuildSerial, hides the actor
	 * and clears mesh, grass and PCG. Does not touch bIsGenerating; the running build's callback clears that flag,
	 * and a reuse that arrives while it is still set raises bRegenerationPending from GenerateTerrainMesh.
	 */
	void ResetChunkState();

	/** Movable for a pooled teleport. SetActorLocation on a Stationary chunk does not move it. */
	void PrepareChunkForTeleport();

	/** Biome-blended elevation with rivers, streams and lakes, plus surface detail. Game thread. */
	UFUNCTION(BlueprintCallable, Category = "Terrain")
	float GetHeightAt(float WorldX, float WorldY) const;

	/** Biomes of the two nearest Voronoi sites and the distance to their cell border at a world position. */
	FBiomeContext GetBiomeContext(float WorldX, float WorldY, float SeedLow, float SeedHigh) const;

	/**
	 * Builds terrain and water geometry for the given init data and build request. Static and self-contained:
	 * it reads nothing but its arguments, so it is safe to run on any thread.
	 */
	static FTerrainBuildResult CalculateTerrainData(FChunkInitData InInitData, float InCellSize, int32 InQuadsPerSide, const FTerrainBuildRequest& Request);

	/**
	 * Builds the chunk once. A rendering machine uploads every visual LOD and its water.
	 * A dedicated server builds the height grid only, and only when collision is first enabled.
	 * Later radius changes toggle collision and grass and do not call this again.
	 * The heavy work runs on a worker unless this is the first build of a chunk with bForceSynchronous.
	 * A request made while a build is running is replayed once that build is applied.
	 * Skipped while InitData.Seed is 0.
	 */
	UFUNCTION(BlueprintCallable, Category = "Terrain")
	void GenerateTerrainMesh();

	/** Server-only. Enables or disables physical collision. Does not rebuild the visual mesh or drop a cooked body. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Terrain")
	void SetCollisionEnabled(bool bEnabled);

	/**
	 * Game thread. Turns the cooked body on or off from the collision radius.
	 * Leaving the radius keeps the body. ResetChunkState is what drops it.
	 */
	void RefreshCollisionState();

	/** True when grass or PCG should be created or released. A dedicated server is always false. */
	bool NeedsDetailRefresh() const;

	/** Game thread. Applies or releases grass and PCG using a one-chunk hysteresis past the collision radius. */
	void RefreshDetailState();

	/** Collision, then grass and PCG. The manager calls this for at most one chunk per frame. */
	void UpdateLocalProximity();

	/** The manager that spawned this chunk. Kept across pool reuse. */
	void SetTerrainManager(ATerrainManager* InManager);

	/** HISM components for biomes that actually spawned instances. Empty rules never create one. */
	const TMap<uint8, TObjectPtr<UHierarchicalInstancedStaticMeshComponent>>& GetBiomeGrassHISMs() const { return BiomeGrassHISMs; }

	/**
	 * Stub for future terraforming: remove grass instances whose bounds overlap a world-space sphere.
	 *
	 * Intended implementation (game thread only):
	 * 1. For each HISM in BiomeGrassHISMs, call GetInstancesOverlappingSphere(Location, Radius, bSphereInWorldSpace=true)
	 *    to collect instance indices that intersect the brush.
	 * 2. Pass those indices to RemoveInstances on the same HISM (or ClearInstances in the overlapping region via
	 *    a rebuild of the chunk's grass for that area once streaming edits exist).
	 * 3. Do not touch the realtime mesh here; terrain height edits are a separate path that should then regenerate
	 *    or patch grass for the affected cells.
	 *
	 * Currently a no-op so call sites can be wired before the edit system exists.
	 */
	UFUNCTION(BlueprintCallable, Category = "Terrain|Grass")
	void RemoveGrassInRadius(FVector Location, float Radius);

	UPROPERTY(ReplicatedUsing = OnRep_InitData, BlueprintReadOnly, Category = "Terrain")
	FChunkInitData InitData;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PostRegisterAllComponents() override;

	/** The Blueprint component template stores a null mesh and overwrites the pointer set in the constructor. */
	void AttachRealtimeMesh();

	UFUNCTION()
	void OnRep_InitData();

	/** Stationary after the mesh upload, so a resting chunk leaves the dynamic visibility path. */
	void SettleChunkMobility();

	/** LOD 0 at screen size 1, LOD 1 at LOD1ScreenSize, LOD 2 at LOD2ScreenSize. */
	void ConfigureVisualLODs();

	/**
	 * Pushes the mesh bounds into the scene and recreates the static draw list.
	 * A chunk built before its component is registered has to do this again once it is.
	 */
	/** Pushes RelativeLocation into the world transform and rebuilds the primitive bounds. */
	void RefreshRenderedMesh();

	/** Local box of the 65×65 grid, extended by the skirt, so culling does not use the empty-mesh sphere. */
	void ApplyChunkBounds(float SkirtDepth);

	/** One static section group. Skips a stream set that has no positions. */
	void UploadSectionGroup(int32 LODIndex, FName GroupName, RealtimeMesh::FRealtimeMeshStreamSet&& Streams, int32 MaterialSlot, bool bCastShadow);

	/** Material slots 0 (terrain) and 1 (water). */
	void SetupMeshMaterials();

	/** Cooks a 2 m complex mesh once, then toggles it. A cooked body survives leaving the radius. */
	void ApplyStoredCollision(bool bEnable);

	/** Collision off, body kept. */
	void DisableStoredCollision();

	/** Marks the mesh BodySetup as garbage so a chunk outside the radius does not keep a physics object. */
	void DestroyMeshBodySetup();

	/** Fills grass HISMs from the cached transforms. Skips a rule that produced no instances. */
	void ApplyGrassInstances();

	/** Clears and unregisters grass HISMs. The components and the transform cache stay for the next entry. */
	void ReleaseGrassInstances();

	/** Squared distance in chunks from the local pawn to this chunk. Large when no pawn exists. */
	float DistSqFromLocalPawn() const;

	/** Grass and PCG: on at CollisionRadiusInChunks, off one chunk farther, so a cell border does not rebuild them. */
	bool WantsDetail(bool bCurrentlyActive) const;

	/** Copies grass rules on the first client build. Later builds leave grass on the chunk cache. */
	void PopulateGrassRequest(FTerrainBuildRequest& Request) const;

	/** Authority: the server-controlled flag. Clients: pawn within CollisionRadiusInChunks of this chunk. */
	bool ShouldHaveCollision() const;

	/** Cached manager. A client fills it from the actor owner the first time the cache is empty. */
	ATerrainManager* GetTerrainManager() const;

	/**
	 * Game thread. Numeric snapshot from ATerrainManager. Safe to copy onto a worker; the assets stay on the game thread.
	 */
	FTerrainGenerationSettings ResolveGenerationSettings() const;

	/** Game thread. Uploads a finished build into the mesh sections, then starts PCG once per InitData. */
	void ApplyMesh(const FTerrainBuildRequest& Request, FTerrainBuildResult& Result);

	/** Game thread. Creates one NoCollision HISM for a biome that already has instances. */
	UHierarchicalInstancedStaticMeshComponent* EnsureGrassComponent(uint8 BiomeId);

	/** Game thread. Clears every grass HISM; used when InitData changes. */
	void ClearAllGrassInstances();

	/** Moves the actor onto InitData.Coords and makes it visible again after a pool reset. */
	void PlaceAtChunkCoords();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Terrain")
	TObjectPtr<URealtimeMeshComponent> RealtimeMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Terrain")
	TObjectPtr<URealtimeMeshSimple> RealtimeMesh;

	/** One HISM per biome ID. Built on the game thread; filled from worker transforms in ApplyMesh. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Terrain|Grass")
	TMap<uint8, TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> BiomeGrassHISMs;

	/** Scatters vegetation and props once the chunk has geometry. Generated locally on every machine, once per InitData. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Terrain|PCG")
	TObjectPtr<UPCGComponent> PCGComponent;

	UPROPERTY(EditDefaultsOnly, Category = "Terrain")
	TObjectPtr<UMaterialInterface> TerrainMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "Terrain")
	TObjectPtr<UMaterialInterface> WaterMaterial;

	/** Screen size at which the renderer drops from LOD 0 to LOD 1. LOD 0 stays at 1. */
	UPROPERTY(EditDefaultsOnly, Category = "Terrain|LOD", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LOD1ScreenSize = 0.45f;

	/**
	 * Screen size of the coarsest LOD. Below this the chunk is not drawn.
	 * 0.06 drops chunks that are only a few pixels, which is the far side of the despawn margin.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Terrain|LOD", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LOD2ScreenSize = 0.06f;

private:
	/** Game thread. Applies a finished build unless InitData changed meanwhile, then replays a pending request. */
	void OnBuildComplete(const FTerrainBuildRequest& Request, FTerrainBuildResult& Result, uint32 Serial);

	/** Runs the chunk PCG graph once per visit to the collision radius. The streaming edge does not enqueue it. */
	void GeneratePCG();

	/** Drops a graph started for this cell so a pooled or distant chunk does not keep its instances. */
	void ReleasePCG();

	/** True from the start of a build until its result has been handled on the game thread. */
	std::atomic<bool> bIsGenerating{false};

	/** A rebuild was requested while a build was running. */
	bool bRegenerationPending = false;

	/** Incremented whenever InitData changes, so a build started for the old data is discarded. */
	uint32 BuildSerial = 0;

	/** Prevents BeginPlay from rebuilding a mesh already produced by OnRep_InitData. */
	bool bHasGeneratedMesh = false;

	/** Client mesh already holds every visual LOD for UploadedVisualSeed/Coords. A new InitData builds again. */
	bool bVisualLODsReady = false;

	/** InitData the current visual sections were built for. Collision toggles do not change it. */
	uint32 UploadedVisualSeed = 0;

	FIntPoint UploadedVisualCoords = FIntPoint(MIN_int32, MIN_int32);

	/**
	 * Bumped on every collision cook or discard. A callback with an older serial does not touch the body,
	 * so a leave/enter pair cannot publish over a newer cook or wipe a mesh that is still in range.
	 */
	uint32 CollisionUpdateSerial = 0;

	/** PCG runs once per InitData, not on every LOD rebuild. */
	bool bPCGGenerated = false;

	/**
	 * Grass HISMs are filled while the pawn is inside the collision radius.
	 * Leaving that radius clears them. The transform cache is what gets reapplied.
	 */
	bool bGrassBuilt = false;

	/**
	 * First client build stored grass transforms. Entering the collision radius applies this cache on the game thread.
	 */
	bool bGrassCacheReady = false;

	/** Inner 65×65 LOD 0 nodes from the last build of this InitData. Game thread only. */
	TArray<FTerrainGridSample> CachedLOD0Grid;

	/** Grass instance transforms from the first client build, keyed by biome ID. */
	TMap<uint8, TArray<FTransform>> CachedGrassTransforms;

	/** Wind phase parallel to CachedGrassTransforms. */
	TMap<uint8, TArray<float>> CachedGrassWindPhases;

	/** A complex body has been cooked for this cell. Leaving the radius does not clear it. */
	bool bCollisionCooked = false;

	/** Collision is enabled on the component. Clients compare it when the pawn crosses CollisionRadiusInChunks. */
	bool bCollisionApplied = false;

	/**
	 * Game thread only. Grass entries copied from the loaded biome assets in GenerateTerrainMesh.
	 * Mesh pointers live here; the worker receives only FBiomeGrassSpawnRule.
	 */
	mutable TArray<FBiomeGrassEntry> ResolvedGrass;

	/** Server-controlled collision state. On by default so the static grid and pre-spawned chunks collide. */
	bool bServerCollisionEnabled = true;

	/** Spawned chunks keep the manager here instead of scanning the world on each query. */
	mutable TWeakObjectPtr<ATerrainManager> CachedTerrainManager;
};
