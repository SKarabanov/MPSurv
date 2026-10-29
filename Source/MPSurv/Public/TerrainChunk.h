// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <atomic>

#include "CoreMinimal.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "TerrainChunk.generated.h"

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
	 * at the centre, and swamps, plateaus and mountains give way to plains and forests.
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

/** CPU-side geometry of one grid build. Plain value type, so it can be built on a worker and moved to the game thread. */
struct FTerrainMeshBuffers
{
	int32 QuadCount = 0;
	float Step = 0.0f;

	/** Heights with a one-cell border on every side, HeightsPerSide = QuadCount + 3. Empty for water. */
	TArray<float> Heights;
	int32 HeightsPerSide = 0;

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FProcMeshTangent> Tangents;

	/**
	 * Terrain: R is the lower of the two biome IDs, G the higher, B the weight of G (0 = all R, 255 = all G).
	 * Corners of one triangle share R and G. A corner that would disagree is a copy, so IDs are not interpolated.
	 * The heavier biome is G when B >= 128 and R otherwise. Sea, flooded shore and oasis stay the heavier biome.
	 * Water: RGB hold the wave attenuation mask, 0 at the shore and 255 over deep water.
	 */
	TArray<FColor> Colors;
};

/** What one chunk build should produce, decided on the game thread and copied to the worker. */
struct FTerrainBuildRequest
{
	FTerrainGenerationSettings Settings;

	/** LOD of the terrain grid. Always 0 on a dedicated server. */
	int32 LOD = 0;

	/** Dedicated server: a single collision-only terrain section, no skirts, no water. */
	bool bDedicatedServer = false;

	/** The terrain section carries collision itself (its grid is LOD 0). Dedicated server only. */
	bool bTerrainCollision = false;

	/** Build a hidden LOD 0 section for collision. Visual LOD sections never carry collision. */
	bool bCollisionGrid = false;

	/**
	 * Rendering machines: build LOD 0, 1 and 2, each with its own water section, in this one build.
	 * Later LOD changes only toggle section visibility.
	 */
	bool bBuildAllVisualLODs = false;

	/** Visual sections are already uploaded. This build only creates or clears the hidden collision section. */
	bool bUpdateCollisionOnly = false;

	/**
	 * Grass placement rules for the worker. Empty on a dedicated server or when the actor has no grass entries.
	 * No mesh pointers: the game thread maps BiomeId to HISM after the build returns.
	 */
	TArray<FBiomeGrassSpawnRule> GrassRules;

	/** When true, the worker runs the independent grass grid pass (not tied to terrain LOD vertices). */
	bool bBuildGrass = false;
};

/** Geometry produced by one chunk build. */
struct FTerrainBuildResult
{
	FTerrainMeshBuffers Terrain;

	/** One entry per visual LOD when the request built every LOD. Empty for a dedicated server and for partial updates. */
	TArray<FTerrainMeshBuffers> TerrainLODs;

	/** Empty unless the request asked for a collision grid. */
	FTerrainMeshBuffers Collision;

	/** Empty on a dedicated server or where no water reaches the chunk. */
	FTerrainMeshBuffers Water;

	/** One water mesh per visual LOD, parallel to TerrainLODs. An entry with no triangles is a dry LOD. */
	TArray<FTerrainMeshBuffers> WaterLODs;

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
 * Rendering machines pick the visual LOD from the local camera. Collision is controlled by the server
 * (SetCollisionEnabled) on the authority. A client enables it only while the local pawn is within
 * CollisionRadiusInChunks of the chunk square, the same test the server uses.
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

	/** Hidden LOD 0 collision. PCG prefers this section while it has vertices. */
	static constexpr int32 CollisionSectionIndex = 2;

	/** Visual terrain. LOD 0 stays in section 0; LOD 1 and 2 sit past the collision section. */
	static int32 TerrainSectionIndexForLOD(int32 LOD)
	{
		const int32 ClampedLOD = FMath::Clamp(LOD, 0, NumLODs - 1);
		return ClampedLOD == 0 ? 0 : ClampedLOD + 2;
	}

	/** Water for the same LOD. LOD 0 stays in section 1. */
	static int32 WaterSectionIndexForLOD(int32 LOD)
	{
		const int32 ClampedLOD = FMath::Clamp(LOD, 0, NumLODs - 1);
		return ClampedLOD == 0 ? 1 : ClampedLOD + 4;
	}

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server-only. Assigns init data and starts the first mesh build (synchronous if InData.bForceSynchronous). */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Terrain")
	void InitializeChunk(const FChunkInitData& InData);

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
	 * Rebuilds the mesh for the current collision state. The first client build uploads every visual LOD and
	 * its water; later LOD changes only toggle section visibility. A dedicated server only builds collision
	 * geometry, and only while collision is enabled. The heavy work runs on a worker unless this is the first
	 * build of a chunk with bForceSynchronous. A request made while a build is running is replayed once that
	 * build is applied. Skipped while InitData.Seed is 0.
	 */
	UFUNCTION(BlueprintCallable, Category = "Terrain")
	void GenerateTerrainMesh();

	/** Server-only. Enables or disables physical collision of this chunk; rebuilds the mesh on change. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Terrain")
	void SetCollisionEnabled(bool bEnabled);

	/** HISM components created from the loaded biome assets, keyed by biome ID. Valid after EnsureGrassComponents. */
	const TMap<uint8, TObjectPtr<UHierarchicalInstancedStaticMeshComponent>>& GetBiomeGrassHISMs() const { return BiomeGrassHISMs; }

	/**
	 * Stub for future terraforming: remove grass instances whose bounds overlap a world-space sphere.
	 *
	 * Intended implementation (game thread only):
	 * 1. For each HISM in BiomeGrassHISMs, call GetInstancesOverlappingSphere(Location, Radius, bSphereInWorldSpace=true)
	 *    to collect instance indices that intersect the brush.
	 * 2. Pass those indices to RemoveInstances on the same HISM (or ClearInstances in the overlapping region via
	 *    a rebuild of the chunk's grass for that area once streaming edits exist).
	 * 3. Do not touch ProceduralMesh here; terrain height edits are a separate path that should then regenerate
	 *    or patch grass for the affected cells.
	 *
	 * Currently a no-op so call sites can be wired before the edit system exists.
	 */
	UFUNCTION(BlueprintCallable, Category = "Terrain|Grass")
	void RemoveGrassInRadius(FVector Location, float Radius);

	UPROPERTY(ReplicatedUsing = OnRep_InitData, BlueprintReadOnly, Category = "Terrain")
	FChunkInitData InitData;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void OnRep_InitData();

	/** LOD timer callback on rendering machines: shows the section for the camera distance. */
	void UpdateVisualLOD();

	/** Shows one terrain LOD and its water section. Does not recreate the scene proxy. */
	void ApplyVisualLODVisibility(int32 LOD);

	/** Stationary after the mesh upload, so a resting chunk leaves the dynamic visibility path. */
	void SettleChunkMobility();

	/** Creates or clears the hidden collision section. Create and clear recreate the scene proxy. */
	void ApplyCollisionSection(const FTerrainMeshBuffers& Collision, bool bEnable);

	/** Fills grass HISMs from a finished build. Does not touch mesh sections. */
	void ApplyGrassInstances(const FTerrainBuildResult& Result);

	/** Copies grass rules when the shown LOD is 0 and this InitData has not built grass yet. */
	void PopulateGrassRequest(FTerrainBuildRequest& Request) const;

	/** One next-tick retry while the manager's visual LOD budget is full. Further requests on the same frame are dropped. */
	void DeferVisualLODRetry();

	/** Returns the visual LOD budget slot held by this chunk, if any. */
	void ReleaseVisualLODSlot();

	/**
	 * LOD for the current local camera distance, or CurrentLOD if there is no camera yet.
	 * With hysteresis, a coarser LOD is only chosen once the camera is LODHysteresis past its distance.
	 */
	int32 ComputeTargetLOD(bool bApplyHysteresis) const;

	/** Authority: the server-controlled flag. Clients: pawn within CollisionRadiusInChunks of this chunk. */
	bool ShouldHaveCollision() const;

	/**
	 * Game thread. Numeric snapshot from ATerrainManager. Safe to copy onto a worker; the assets stay on the game thread.
	 */
	FTerrainGenerationSettings ResolveGenerationSettings() const;

	/** Game thread. Uploads a finished build into the mesh sections, then starts PCG once per InitData. */
	void ApplyMesh(const FTerrainBuildRequest& Request, const FTerrainBuildResult& Result);

	/** Game thread. Creates one NoCollision HISM per resolved grass entry that has a mesh. Idempotent. */
	void EnsureGrassComponents();

	/** Game thread. Clears every grass HISM; used when InitData changes. */
	void ClearAllGrassInstances();

	/** Moves the actor onto InitData.Coords and makes it visible again after a pool reset. */
	void PlaceAtChunkCoords();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Terrain")
	TObjectPtr<UProceduralMeshComponent> ProceduralMesh;

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

	/** Horizontal camera distance to the nearest point of the chunk square beyond which LOD 1 is used (3 chunks). */
	UPROPERTY(EditDefaultsOnly, Category = "Terrain|LOD", meta = (ClampMin = "0.0", Units = "cm"))
	float LOD1Distance = 19200.0f;

	/** Horizontal camera distance to the nearest point of the chunk square beyond which LOD 2 is used (6 chunks). */
	UPROPERTY(EditDefaultsOnly, Category = "Terrain|LOD", meta = (ClampMin = "0.0", Units = "cm"))
	float LOD2Distance = 38400.0f;

	/** Extra distance past a LOD threshold before switching to the coarser LOD, so the mesh does not rebuild back and forth. */
	UPROPERTY(EditDefaultsOnly, Category = "Terrain|LOD", meta = (ClampMin = "0.0", Units = "cm"))
	float LODHysteresis = 1000.0f;

	/** Seconds between camera distance checks on rendering machines. */
	UPROPERTY(EditDefaultsOnly, Category = "Terrain|LOD", meta = (ClampMin = "0.05", Units = "s"))
	float LODUpdateInterval = 0.5f;

private:
	/** Game thread. Applies a finished build unless InitData changed meanwhile, then replays a pending request. */
	void OnBuildComplete(const FTerrainBuildRequest& Request, const FTerrainBuildResult& Result, uint32 Serial);

	/** Runs PCG once per InitData, after the chunk first has mesh sections. */
	void GeneratePCG();

	/** True from the start of a build until its result has been handled on the game thread. */
	std::atomic<bool> bIsGenerating{false};

	/** A rebuild was requested while a build was running. */
	bool bRegenerationPending = false;

	/** Incremented whenever InitData changes, so a build started for the old data is discarded. */
	uint32 BuildSerial = 0;

	/** Prevents BeginPlay from rebuilding a mesh already produced by OnRep_InitData. */
	bool bHasGeneratedMesh = false;

	/** Client mesh already holds every visual LOD. Further LOD changes only toggle visibility. */
	bool bVisualLODsReady = false;

	/** PCG runs once per InitData, not on every LOD rebuild. */
	bool bPCGGenerated = false;

	/**
	 * Grass HISMs were filled once for the current InitData. Cleared only on InitData change.
	 * Survives LOD 0→1→0 so the worker does not rebuild grass when the player returns.
	 */
	bool bGrassBuilt = false;

	/** Visual LOD of the current mesh. Always 0 on a dedicated server. */
	int32 CurrentLOD = 0;

	/** This chunk has taken a visual LOD budget slot that ReleaseVisualLODSlot has not yet returned. */
	bool bHoldsVisualLODSlot = false;

	/** GFrameCounter of the last deferred visual LOD retry. Stops the same frame from queueing more than one. */
	uint64 VisualLODRetryFrame = 0;

	bool bVisualLODRetryScheduled = false;

	FTimerHandle VisualLODRetryHandle;

	/** Collision flag last uploaded by ApplyMesh. Clients compare it when the pawn crosses CollisionRadiusInChunks. */
	bool bCollisionApplied = false;

	/**
	 * Game thread only. Grass entries copied from the loaded biome assets in GenerateTerrainMesh.
	 * Mesh pointers live here; the worker receives only FBiomeGrassSpawnRule.
	 */
	mutable TArray<FBiomeGrassEntry> ResolvedGrass;

	/** Server-controlled collision state. On by default so the static grid and pre-spawned chunks collide. */
	bool bServerCollisionEnabled = true;

	FTimerHandle LODTimerHandle;
};
