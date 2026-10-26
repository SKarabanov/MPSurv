// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BiomeDataAsset.h"
#include "GameFramework/Actor.h"
#include "UObject/StrongObjectPtr.h"
#include "WorldGenSettings.h"
#include "TerrainManager.generated.h"

class ATerrainChunk;

/** How the terrain manager populates the chunk grid. */
UENUM(BlueprintType)
enum class ETerrainSpawnMode : uint8
{
	/** Spawns a fixed square grid of GridRadius around the origin synchronously at startup, without collision. For inspecting terrain in Simulate. */
	StaticDebugGrid,
	/** Streams chunks in and out around active players on the server. */
	DynamicStreaming
};

/**
 * Server-side owner of the terrain chunk grid. Spawns replicated ATerrainChunk actors and keeps
 * track of them by chunk coordinates. Clients receive the chunks through regular actor replication.
 */
UCLASS()
class MPSURV_API ATerrainManager : public AActor
{
	GENERATED_BODY()

public:
	ATerrainManager();

	/** Returns the chunk at the given grid coordinates, or nullptr if none is active there. Server only. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Terrain Grid")
	ATerrainChunk* GetChunkAt(FIntPoint Coords) const;

	/**
	 * Game thread, every machine. Loads WorldGenSettings and BiomeSettingsMap once and returns the numeric snapshot.
	 * Unassigned assets keep the defaults in FTerrainGenerationSettings. The assets themselves never leave this thread.
	 */
	FTerrainGenerationSettings GetGenerationSettings() const;

	/** Game thread. Grass entries from the loaded biome assets. BiomeId is taken from the asset, not the entry. */
	const TArray<FBiomeGrassEntry>& GetGrassEntries() const;

	/** Radius, in chunks, inside which a pawn keeps physical collision. Shared by the server and by clients. */
	int32 GetCollisionRadiusInChunks() const { return CollisionRadiusInChunks; }

	/** Spawns up to ChunksPerFrame chunks from GenerationQueue; switches itself off once the queue is empty. */
	virtual void Tick(float DeltaTime) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Queues every chunk in the square [-GridRadius, GridRadius] on both axes, nearest to the origin first. Server only. */
	void SpawnInitialGrid();

	/** Pre-spawns the ground around player starts and starts the streaming timer. Server only. */
	void StartDynamicStreaming();

	/** Streaming timer callback: unloads distant chunks and spawns the nearest missing ones within budget. */
	void UpdateStreaming();

	/**
	 * Synchronously spawns every missing chunk within RadiusInChunks of a point in chunk space.
	 * Chunks within CollisionRadiusInChunks of the point get collision, including ones that already existed.
	 */
	void SpawnChunksInRadius(const FVector2D& CenterInChunks, float RadiusInChunks);

	/** World locations of every player-controlled pawn. */
	void GatherPlayerLocations(TArray<FVector>& OutLocations) const;

	/** Logs and returns false if chunks cannot be spawned with the current setup. */
	bool ValidateSpawnSettings() const;

	/** Game thread. Fills the numeric snapshot and keeps the loaded assets alive. Safe to call more than once. */
	void EnsureGenerationAssetsLoaded() const;

	/**
	 * Spawns a single chunk at grid coordinates and registers it in ActiveChunks. Reuses a pooled actor when one
	 * is free; otherwise spawns deferred. The collision state is applied before the first build, so a dedicated
	 * server never builds geometry for a chunk that starts without collision.
	 * Returns the existing chunk unchanged if one is already there.
	 * bForceSynchronous builds the first mesh on the spot instead of on a worker thread, for start chunks.
	 */
	ATerrainChunk* SpawnChunk(const FIntPoint& Coords, bool bEnableCollision = true, bool bForceSynchronous = false);

	/** Takes a valid chunk out of FreeChunksPool, or nullptr when the pool has none. */
	ATerrainChunk* AcquireChunk(const FIntPoint& Coords);

	/** Unregisters the chunk and returns it to FreeChunksPool instead of destroying it. */
	void DespawnChunk(const FIntPoint& Coords);

	/** Hides and resets a chunk, then stores it for the next AcquireChunk. */
	void ReleaseChunk(ATerrainChunk* Chunk);

	UPROPERTY(EditAnywhere, Category = "Terrain Grid")
	TSubclassOf<ATerrainChunk> ChunkClass;

	/**
	 * Shared world shape. Loaded on the game thread into FTerrainGenerationSettings.
	 * Empty keeps the built-in defaults.
	 */
	UPROPERTY(EditAnywhere, Category = "Terrain|World")
	TSoftObjectPtr<UWorldGenSettings> WorldGenSettings;

	/**
	 * One data asset per biome. The asset's class carries that biome's parameters; GrassRules on the asset supply the meshes.
	 * A missing entry keeps the built-in defaults for that biome.
	 */
	UPROPERTY(EditAnywhere, Category = "Terrain|Biomes")
	TMap<EBiomeType, TSoftObjectPtr<UBiomeDataAsset>> BiomeSettingsMap;

	/** Must be non-zero: ATerrainChunk treats seed 0 as uninitialized and skips mesh generation. */
	UPROPERTY(EditAnywhere, Category = "Terrain Grid")
	int32 WorldSeed = 1337;

	/** Chunks spawned from GenerationQueue per frame. Each one builds its mesh synchronously on spawn. */
	UPROPERTY(EditAnywhere, Category = "Terrain", meta = (ClampMin = "1"))
	int32 ChunksPerFrame = 1;

	UPROPERTY(EditAnywhere, Category = "Mode")
	ETerrainSpawnMode SpawnMode = ETerrainSpawnMode::StaticDebugGrid;

	/** Grid half-extent in chunks for StaticDebugGrid. 1 gives 3x3 chunks, 2 gives 5x5. */
	UPROPERTY(EditAnywhere, Category = "Debug", meta = (ClampMin = "0",
		EditCondition = "SpawnMode == ETerrainSpawnMode::StaticDebugGrid", EditConditionHides))
	int32 GridRadius = 15;

	/** Logs spawn and despawn counts on every streaming update that changes the grid. */
	UPROPERTY(EditAnywhere, Category = "Debug", meta = (EditCondition = "SpawnMode == ETerrainSpawnMode::DynamicStreaming", EditConditionHides))
	bool bLogStreaming = false;

	/** Draws the load (green) and unload (red) circles around each player on the server. */
	UPROPERTY(EditAnywhere, Category = "Debug", meta = (EditCondition = "SpawnMode == ETerrainSpawnMode::DynamicStreaming", EditConditionHides))
	bool bDrawStreamingDebug = false;

	/** Chunks intersecting this circle around a player are loaded, in chunks. */
	UPROPERTY(EditAnywhere, Category = "Streaming", meta = (ClampMin = "1",
		EditCondition = "SpawnMode == ETerrainSpawnMode::DynamicStreaming", EditConditionHides))
	int32 StreamingRadius = 8;

	/** Extra distance past StreamingRadius before a chunk is unloaded, so chunks on the border do not flicker. */
	UPROPERTY(EditAnywhere, Category = "Streaming", meta = (ClampMin = "1",
		EditCondition = "SpawnMode == ETerrainSpawnMode::DynamicStreaming", EditConditionHides))
	int32 DespawnMargin = 2;

	/** Seconds between player position polls. */
	UPROPERTY(EditAnywhere, Category = "Streaming", meta = (ClampMin = "0.05", Units = "s",
		EditCondition = "SpawnMode == ETerrainSpawnMode::DynamicStreaming", EditConditionHides))
	float StreamingUpdateInterval = 0.25f;

	/** Spawn budget per streaming update; the nearest chunks to any player go first. */
	UPROPERTY(EditAnywhere, Category = "Streaming", meta = (ClampMin = "1",
		EditCondition = "SpawnMode == ETerrainSpawnMode::DynamicStreaming", EditConditionHides))
	int32 MaxChunksPerUpdate = 4;

	/**
	 * Chunks within this distance of any player have physical collision on the server; farther ones have none.
	 * Visual LOD is not driven by the server: every rendering machine picks it from its own camera.
	 */
	UPROPERTY(EditAnywhere, Category = "Streaming", meta = (ClampMin = "0",
		EditCondition = "SpawnMode == ETerrainSpawnMode::DynamicStreaming", EditConditionHides))
	int32 CollisionRadiusInChunks = 2;

	/**
	 * Radius in chunks spawned instantly around every player start at startup, so the first player lands on ground.
	 * Chunks this close to a player are also spawned outside the per-update budget.
	 */
	UPROPERTY(EditAnywhere, Category = "Streaming", meta = (ClampMin = "0",
		EditCondition = "SpawnMode == ETerrainSpawnMode::DynamicStreaming", EditConditionHides))
	int32 PreSpawnRadius = 2;

private:
	UPROPERTY(Transient)
	TMap<FIntPoint, TObjectPtr<ATerrainChunk>> ActiveChunks;

	/** Hidden chunks waiting to be moved onto new coordinates. Server only. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<ATerrainChunk>> FreeChunksPool;

	/** Chunk coordinates waiting to be spawned by Tick. The next chunk is taken from the end. */
	TArray<FIntPoint> GenerationQueue;

	FTimerHandle StreamingTimerHandle;

	/** Hard refs so the loaded assets and their grass meshes are not collected after the synchronous load. */
	mutable TStrongObjectPtr<UWorldGenSettings> LoadedWorldGenSettings;
	mutable TArray<TStrongObjectPtr<UBiomeDataAsset>> LoadedBiomeAssets;
	mutable FTerrainGenerationSettings ResolvedSettings;
	mutable TArray<FBiomeGrassEntry> ResolvedGrass;
	mutable bool bGenerationAssetsLoaded = false;
};
