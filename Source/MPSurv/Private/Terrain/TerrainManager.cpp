// Copyright Epic Games, Inc. All Rights Reserved.

#include "Terrain/TerrainManager.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "CoreGlobals.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "PhysicsEngine/BodySetup.h"
#include "RHIGlobals.h"
#include "RealtimeMesh.h"
#include "RealtimeMeshSimple.h"
#include "Terrain/TerrainChunk.h"
#include "TimerManager.h"
#include "UObject/UObjectArray.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogTerrainManager, Log, All);

namespace TerrainManagerPrivate
{
	/** World-space step of the chunk grid. */
	constexpr float ChunkWorldSize = ATerrainChunk::ChunkWorldSize;

	/**
	 * Resident cap for the debug square. Radius 60 is 121×121 = 14641 full meshes and exhausts memory
	 * around 11000. The queue is nearest-first, so the cap keeps the centre and drops the rest.
	 */
	constexpr int32 MaxDebugGridChunks = 1024;

	FVector2D WorldToChunkSpace(const FVector& WorldLocation)
	{
		return FVector2D(WorldLocation.X / ChunkWorldSize, WorldLocation.Y / ChunkWorldSize);
	}

	/**
	 * Squared distance in chunks from a point to the nearest point of a chunk's square, so the chunk under
	 * the point is always at distance 0 and a radius selects every chunk the circle touches.
	 */
	float DistSqToChunk(const FVector2D& PointInChunks, const FIntPoint& Coords)
	{
		const float PointX = static_cast<float>(PointInChunks.X);
		const float PointY = static_cast<float>(PointInChunks.Y);
		const float DX = FMath::Max3(static_cast<float>(Coords.X) - PointX, 0.0f, PointX - static_cast<float>(Coords.X + 1));
		const float DY = FMath::Max3(static_cast<float>(Coords.Y) - PointY, 0.0f, PointY - static_cast<float>(Coords.Y + 1));
		return DX * DX + DY * DY;
	}

	struct FPendingChunk
	{
		FIntPoint Coords;
		float DistSqToNearestPlayer;
	};
}

ATerrainManager::ATerrainManager()
{
	// Ticks to watch the pawn's chunk cell and to drain the debug-grid queue.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	bReplicates = true;
	SetReplicateMovement(false);

	ChunkClass = ATerrainChunk::StaticClass();
}

namespace
{
	bool TickTerrainResidentDiagnostics(float /*DeltaTime*/)
	{
		int32 ChunksLive = 0;
		int32 ChunksShown = 0;
		int32 ChunksHidden = 0;
		int32 ChunksGarbage = 0;
		for (TObjectIterator<ATerrainChunk> It; It; ++It)
		{
			if (It->IsTemplate())
			{
				continue;
			}
			if (!IsValid(*It))
			{
				++ChunksGarbage;
				continue;
			}
			++ChunksLive;
			if (It->IsHidden())
			{
				++ChunksHidden;
			}
			else
			{
				++ChunksShown;
			}
		}

		int32 Meshes = 0;
		int32 MeshesGarbage = 0;
		for (TObjectIterator<URealtimeMesh> It; It; ++It)
		{
			if (It->IsTemplate())
			{
				continue;
			}
			if (!IsValid(*It))
			{
				++MeshesGarbage;
				continue;
			}
			++Meshes;
		}

		int32 MeshComponents = 0;
		for (TObjectIterator<URealtimeMeshComponent> It; It; ++It)
		{
			if (!It->IsTemplate() && IsValid(*It))
			{
				++MeshComponents;
			}
		}

		int32 Bodies = 0;
		for (TObjectIterator<UBodySetup> It; It; ++It)
		{
			if (!It->IsTemplate() && IsValid(*It) && It->GetOuter() && It->GetOuter()->IsA<URealtimeMesh>())
			{
				++Bodies;
			}
		}

		int32 GrassComponents = 0;
		int32 GrassInstances = 0;
		for (TObjectIterator<UHierarchicalInstancedStaticMeshComponent> It; It; ++It)
		{
			if (It->IsTemplate() || !IsValid(*It))
			{
				continue;
			}
			const AActor* Owner = It->GetOwner();
			if (!Owner || !Owner->IsA<ATerrainChunk>())
			{
				continue;
			}
			++GrassComponents;
			GrassInstances += It->GetInstanceCount();
		}

		int32 Worlds = 0;
		int32 PieWorlds = 0;
		if (GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				const UWorld* World = Context.World();
				if (!World)
				{
					continue;
				}
				++Worlds;
				if (World->WorldType == EWorldType::PIE)
				{
					++PieWorlds;
				}
			}
		}

		const int32 Objects = GUObjectArray.GetObjectArrayNum();
		const uint64 MemMB = FPlatformMemory::GetStats().UsedPhysical / (1024ull * 1024ull);
		const uint64 BufMB = GRHIGlobals.BufferMemorySize / (1024ull * 1024ull);
		const uint64 TexMB = (static_cast<uint64>(GRHIGlobals.StreamingTextureMemorySizeInKB) + static_cast<uint64>(GRHIGlobals.NonStreamingTextureMemorySizeInKB)) / 1024ull;

		static int32 PrevObjects = Objects;
		static int32 PrevChunks = ChunksLive;
		static int32 PrevMeshes = Meshes;
		static int32 PrevGrass = GrassInstances;
		static uint64 PrevBuf = BufMB;

		UE_LOG(LogTerrainManager, Warning,
			TEXT("TerrainDiag objects=%d (d=%d) chunks=%d shown=%d hidden=%d garbage=%d (d=%d) meshes=%d garbage=%d (d=%d) comps=%d bodies=%d grass=%d instances=%d (d=%d) worlds=%d pie=%d memMB=%llu bufMB=%llu (d=%lld) texMB=%llu"),
			Objects, Objects - PrevObjects,
			ChunksLive, ChunksShown, ChunksHidden, ChunksGarbage, ChunksLive - PrevChunks,
			Meshes, MeshesGarbage, Meshes - PrevMeshes,
			MeshComponents, Bodies,
			GrassComponents, GrassInstances, GrassInstances - PrevGrass,
			Worlds, PieWorlds,
			MemMB, BufMB, static_cast<int64>(BufMB) - static_cast<int64>(PrevBuf), TexMB);

		PrevObjects = Objects;
		PrevChunks = ChunksLive;
		PrevMeshes = Meshes;
		PrevGrass = GrassInstances;
		PrevBuf = BufMB;
		return true;
	}
}

static FTSTicker::FDelegateHandle GTerrainDiagHandle;

void StartTerrainResidentDiagnostics()
{
	if (GTerrainDiagHandle.IsValid())
	{
		return;
	}

	GTerrainDiagHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateStatic(&TickTerrainResidentDiagnostics), 2.0f);
}

void StopTerrainResidentDiagnostics()
{
	if (GTerrainDiagHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(GTerrainDiagHandle);
		GTerrainDiagHandle.Reset();
	}
}

void ATerrainManager::BeginPlay()
{
	// Preview actors live in the editor world. Drop them before the session spawns the real grid.
	ClearEditorPreview();

	Super::BeginPlay();

	// Clients rebuild meshes from replicated InitData and need the same numbers. The chunk also loads on first use,
	// so an OnRep that arrives before this still sees the assets.
	EnsureGenerationAssetsLoaded();
	SetActorTickEnabled(true);

	if (!HasAuthority())
	{
		return;
	}

	switch (SpawnMode)
	{
	case ETerrainSpawnMode::StaticDebugGrid:
		SpawnInitialGrid();
		break;

	case ETerrainSpawnMode::DynamicStreaming:
		StartDynamicStreaming();
		break;
	}
}

FTerrainGenerationSettings ATerrainManager::GetGenerationSettings() const
{
	EnsureGenerationAssetsLoaded();
	return ResolvedSettings;
}

const TArray<FBiomeGrassEntry>& ATerrainManager::GetGrassEntries() const
{
	EnsureGenerationAssetsLoaded();
	return ResolvedGrass;
}

void ATerrainManager::EnsureGenerationAssetsLoaded() const
{
	if (bGenerationAssetsLoaded)
	{
		return;
	}

	ResolvedSettings = FTerrainGenerationSettings();
	ResolvedGrass.Reset();
	LoadedWorldGenSettings.Reset();
	LoadedBiomeAssets.Reset();

	if (UWorldGenSettings* WorldSettings = WorldGenSettings.LoadSynchronous())
	{
		LoadedWorldGenSettings.Reset(WorldSettings);
		WorldSettings->ApplyTo(ResolvedSettings);
	}
	else if (!WorldGenSettings.IsNull())
	{
		UE_LOG(LogTerrainManager, Warning, TEXT("World generation settings '%s' failed to load. Using built-in defaults."),
			*WorldGenSettings.ToString());
	}

	for (const TPair<EBiomeType, TSoftObjectPtr<UBiomeDataAsset>>& Pair : BiomeSettingsMap)
	{
		UBiomeDataAsset* BiomeAsset = Pair.Value.LoadSynchronous();
		if (!BiomeAsset)
		{
			if (!Pair.Value.IsNull())
			{
				UE_LOG(LogTerrainManager, Warning, TEXT("Biome settings for %s ('%s') failed to load. That biome keeps its built-in defaults."),
					*UEnum::GetValueAsString(Pair.Key), *Pair.Value.ToString());
			}
			continue;
		}

		if (BiomeAsset->BiomeType != Pair.Key)
		{
			UE_LOG(LogTerrainManager, Warning, TEXT("Biome asset %s is keyed as %s but its BiomeType is %s. Grass and parameters follow the asset."),
				*BiomeAsset->GetName(), *UEnum::GetValueAsString(Pair.Key), *UEnum::GetValueAsString(BiomeAsset->BiomeType));
		}

		LoadedBiomeAssets.Emplace(BiomeAsset);
		BiomeAsset->ApplyTo(ResolvedSettings);

		for (const FBiomeGrassEntry& Entry : BiomeAsset->GrassRules)
		{
			if (!Entry.Mesh || BiomeAsset->BiomeType == EBiomeType::Ocean)
			{
				continue;
			}

			FBiomeGrassEntry Copy = Entry;
			Copy.BiomeId = static_cast<uint8>(BiomeAsset->BiomeType);
			ResolvedGrass.Add(Copy);
		}
	}

	bGenerationAssetsLoaded = true;
}

void ATerrainManager::RebuildEditorPreview()
{
	UWorld* World = GetWorld();
	if (!World || World->IsGameWorld())
	{
		return;
	}

	ClearEditorPreview();
	EnsureGenerationAssetsLoaded();

	UClass* ClassToSpawn = ChunkClass ? ChunkClass.Get() : ATerrainChunk::StaticClass();
	const int32 Radius = FMath::Clamp(EditorPreviewRadius, 0, 8);

	for (int32 Y = -Radius; Y <= Radius; ++Y)
	{
		for (int32 X = -Radius; X <= Radius; ++X)
		{
			const FIntPoint Coords(X, Y);
			const FVector SpawnLocation(Coords.X * ATerrainChunk::ChunkWorldSize, Coords.Y * ATerrainChunk::ChunkWorldSize, 0.0f);
			const FTransform SpawnTransform(SpawnLocation);

			// SpawnActorDeferred's template builds its own parameters, so the transient flag would be dropped.
			// bDeferConstruction is that same deferred path, and ObjectFlags keeps the chunk out of the level file.
			FActorSpawnParameters SpawnParameters;
			SpawnParameters.Owner = this;
			SpawnParameters.OverrideLevel = GetLevel();
			SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			SpawnParameters.ObjectFlags |= RF_Transient;
			SpawnParameters.bDeferConstruction = true;

			ATerrainChunk* Chunk = World->SpawnActor<ATerrainChunk>(ClassToSpawn, SpawnTransform, SpawnParameters);
			if (!Chunk)
			{
				UE_LOG(LogTerrainManager, Error, TEXT("%s: editor preview failed to spawn chunk at (%d, %d)."), *GetName(), Coords.X, Coords.Y);
				continue;
			}

			FChunkInitData InitData;
			InitData.Seed = WorldSeed;
			InitData.Coords = Coords;
			Chunk->SetTerrainManager(this);
			Chunk->InitializeEditorPreview(InitData, bEditorPreviewRunPCG);
			Chunk->FinishSpawning(SpawnTransform);
			if (bEditorPreviewRunPCG)
			{
				Chunk->GenerateEditorPreviewPCG();
			}
			EditorPreviewChunks.Add(Chunk);
		}
	}
}

void ATerrainManager::ClearEditorPreview()
{
	for (const TObjectPtr<ATerrainChunk>& Chunk : EditorPreviewChunks)
	{
		if (!IsValid(Chunk))
		{
			continue;
		}

		Chunk->ReleaseEditorPreviewPCG();
		Chunk->ResetChunkState();
		Chunk->Destroy();
	}

	EditorPreviewChunks.Empty();
}

void ATerrainManager::Destroyed()
{
	ClearEditorPreview();
	Super::Destroyed();
}

void ATerrainManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StreamingTimerHandle);
	}

	if (HasAuthority() && EndPlayReason == EEndPlayReason::Destroyed)
	{
		for (const TPair<FIntPoint, TObjectPtr<ATerrainChunk>>& Pair : ActiveChunks)
		{
			if (IsValid(Pair.Value))
			{
				Pair.Value->Destroy();
			}
		}
		for (const TObjectPtr<ATerrainChunk>& Pooled : FreeChunksPool)
		{
			if (IsValid(Pooled))
			{
				Pooled->Destroy();
			}
		}
	}

	UE_LOG(LogTerrainManager, Warning, TEXT("%s EndPlay active=%d pooled=%d queue=%d reason=%d"),
		*GetName(), ActiveChunks.Num(), FreeChunksPool.Num(), GenerationQueue.Num(), static_cast<int32>(EndPlayReason));

	ActiveChunks.Reset();
	FreeChunksPool.Reset();
	GenerationQueue.Reset();

	Super::EndPlay(EndPlayReason);
}

void ATerrainManager::EnqueueDetailRefresh(ATerrainChunk* Chunk)
{
	if (!IsValid(Chunk))
	{
		return;
	}

	for (const TWeakObjectPtr<ATerrainChunk>& Pending : DetailQueue)
	{
		if (Pending.Get() == Chunk)
		{
			return;
		}
	}

	DetailQueue.Add(Chunk);
}

void ATerrainManager::PollPlayerChunkCells()
{
	TArray<FVector> PlayerLocations;
	GatherPlayerLocations(PlayerLocations);

	TArray<FIntPoint> Cells;
	Cells.Reserve(PlayerLocations.Num());
	for (const FVector& Location : PlayerLocations)
	{
		Cells.Add(FIntPoint(
			FMath::FloorToInt(Location.X / ATerrainChunk::ChunkWorldSize),
			FMath::FloorToInt(Location.Y / ATerrainChunk::ChunkWorldSize)));
	}
	Cells.Sort([](const FIntPoint& A, const FIntPoint& B)
	{
		return A.X < B.X || (A.X == B.X && A.Y < B.Y);
	});

	if (Cells == LastPlayerChunkCells)
	{
		return;
	}
	LastPlayerChunkCells = Cells;

	const float CollisionRadiusSq = FMath::Square(static_cast<float>(FMath::Max(CollisionRadiusInChunks, 0)));
	TArray<FVector2D, TInlineAllocator<8>> PlayerPoints;
	PlayerPoints.Reserve(PlayerLocations.Num());
	for (const FVector& Location : PlayerLocations)
	{
		PlayerPoints.Add(FVector2D(Location.X / ATerrainChunk::ChunkWorldSize, Location.Y / ATerrainChunk::ChunkWorldSize));
	}

	auto Visit = [this, &PlayerPoints, CollisionRadiusSq](ATerrainChunk* Chunk)
	{
		if (!IsValid(Chunk) || Chunk->IsHidden() || Chunk->InitData.Seed == 0)
		{
			return;
		}

		if (HasAuthority())
		{
			float Best = TNumericLimits<float>::Max();
			for (const FVector2D& Point : PlayerPoints)
			{
				const float ChunkX = static_cast<float>(Chunk->InitData.Coords.X);
				const float ChunkY = static_cast<float>(Chunk->InitData.Coords.Y);
				const float DX = FMath::Max3(ChunkX - static_cast<float>(Point.X), 0.0f, static_cast<float>(Point.X) - (ChunkX + 1.0f));
				const float DY = FMath::Max3(ChunkY - static_cast<float>(Point.Y), 0.0f, static_cast<float>(Point.Y) - (ChunkY + 1.0f));
				Best = FMath::Min(Best, DX * DX + DY * DY);
			}
			Chunk->SetCollisionEnabled(Best <= CollisionRadiusSq);
		}
		else
		{
			Chunk->RefreshCollisionState();
		}

		if (Chunk->NeedsDetailRefresh())
		{
			EnqueueDetailRefresh(Chunk);
		}
	};

	if (HasAuthority())
	{
		for (const TPair<FIntPoint, TObjectPtr<ATerrainChunk>>& Pair : ActiveChunks)
		{
			Visit(Pair.Value);
		}
	}
	else if (UWorld* World = GetWorld())
	{
		for (TActorIterator<ATerrainChunk> It(World); It; ++It)
		{
			Visit(*It);
		}
	}
}

void ATerrainManager::PumpDetailQueue()
{
	while (DetailQueue.Num() > 0)
	{
		ATerrainChunk* Chunk = DetailQueue[0].Get();
		DetailQueue.RemoveAt(0, 1, EAllowShrinking::No);
		if (!IsValid(Chunk))
		{
			continue;
		}

		Chunk->UpdateLocalProximity();
		return;
	}
}

void ATerrainManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	PollPlayerChunkCells();
	PumpDetailQueue();

	if (GenerationQueue.IsEmpty())
	{
		return;
	}

	// Chunk meshes build on worker threads, so spreading the spawns over frames bounds the actor spawn cost per
	// frame and the number of builds, with their transient buffers, in flight at once.
	int32 SpawnedThisFrame = 0;
	while (!GenerationQueue.IsEmpty() && SpawnedThisFrame < ChunksPerFrame)
	{
		// The queue pops nearest-first, so the first chunk of the session is the centre one; it is built on the
		// spot so there is ground under the camera immediately.
		const bool bIsFirstChunk = ActiveChunks.IsEmpty();
		const FIntPoint Coords = GenerationQueue.Pop(EAllowShrinking::No);

		// The debug grid is only for inspecting terrain in Simulate, where nothing walks on it, so collision cooking is skipped.
		SpawnChunk(Coords, false, bIsFirstChunk);
		SpawnedThisFrame++;
	}

	if (GenerationQueue.IsEmpty())
	{
		GenerationQueue.Empty();
	}
}

ATerrainChunk* ATerrainManager::GetChunkAt(FIntPoint Coords) const
{
	const TObjectPtr<ATerrainChunk>* Found = ActiveChunks.Find(Coords);
	return (Found && IsValid(*Found)) ? Found->Get() : nullptr;
}

bool ATerrainManager::ValidateSpawnSettings() const
{
	if (!GetWorld())
	{
		UE_LOG(LogTerrainManager, Error, TEXT("%s: no valid world, terrain not spawned."), *GetName());
		return false;
	}

	if (!ChunkClass)
	{
		UE_LOG(LogTerrainManager, Error, TEXT("%s: ChunkClass is not set, terrain not spawned."), *GetName());
		return false;
	}

	if (WorldSeed == 0)
	{
		UE_LOG(LogTerrainManager, Warning, TEXT("%s: WorldSeed is 0, chunks will spawn without geometry."), *GetName());
	}

	return true;
}

void ATerrainManager::SpawnInitialGrid()
{
	if (!HasAuthority() || !ValidateSpawnSettings())
	{
		return;
	}

	const int32 Radius = FMath::Max(GridRadius, 0);
	const int32 Side = (Radius * 2) + 1;
	ActiveChunks.Reserve(Side * Side);
	GenerationQueue.Reserve(GenerationQueue.Num() + Side * Side);

	for (int32 Y = -Radius; Y <= Radius; ++Y)
	{
		for (int32 X = -Radius; X <= Radius; ++X)
		{
			GenerationQueue.Add(FIntPoint(X, Y));
		}
	}

	// Tick pops from the end, so the farthest chunks go first in the array and the grid grows outwards from the origin.
	GenerationQueue.Sort([](const FIntPoint& A, const FIntPoint& B)
	{
		return A.SizeSquared() > B.SizeSquared();
	});

	if (GenerationQueue.Num() > TerrainManagerPrivate::MaxDebugGridChunks)
	{
		const int32 Requested = GenerationQueue.Num();
		GenerationQueue.RemoveAt(0, Requested - TerrainManagerPrivate::MaxDebugGridChunks, EAllowShrinking::Yes);
		UE_LOG(LogTerrainManager, Warning,
			TEXT("%s: GridRadius %d queues %d chunks. Keeping the %d nearest so the static grid does not exhaust memory."),
			*GetName(), Radius, Requested, GenerationQueue.Num());
	}

	SetActorTickEnabled(!GenerationQueue.IsEmpty());
}

void ATerrainManager::StartDynamicStreaming()
{
	using namespace TerrainManagerPrivate;

	if (!HasAuthority() || !ValidateSpawnSettings())
	{
		return;
	}

	UWorld* World = GetWorld();

	// The game mode picks the start spot later, so the ground is prepared around every candidate.
	TArray<FVector> Origins;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		Origins.Add(It->GetActorLocation());
	}
	GatherPlayerLocations(Origins);

	if (Origins.IsEmpty())
	{
		Origins.Add(FVector::ZeroVector);
	}

	const float Radius = static_cast<float>(FMath::Max(PreSpawnRadius, 0));
	for (const FVector& Origin : Origins)
	{
		SpawnChunksInRadius(WorldToChunkSpace(Origin), Radius);
	}

	UE_LOG(LogTerrainManager, Log, TEXT("%s: pre-spawned %d chunks around %d origin(s)."), *GetName(), ActiveChunks.Num(), Origins.Num());

	World->GetTimerManager().SetTimer(
		StreamingTimerHandle,
		this,
		&ATerrainManager::UpdateStreaming,
		FMath::Max(StreamingUpdateInterval, 0.05f),
		true);
}

void ATerrainManager::UpdateStreaming()
{
	using namespace TerrainManagerPrivate;

	if (!HasAuthority())
	{
		return;
	}

	TArray<FVector> PlayerLocations;
	GatherPlayerLocations(PlayerLocations);

	// With no pawns (everyone dead or still joining) the current chunks are kept for the respawn.
	if (PlayerLocations.IsEmpty())
	{
		return;
	}

	TArray<FVector2D, TInlineAllocator<8>> PlayerPoints;
	for (const FVector& Location : PlayerLocations)
	{
		PlayerPoints.Add(WorldToChunkSpace(Location));
	}

	TArray<FIntPoint> Cells;
	Cells.Reserve(PlayerPoints.Num());
	for (const FVector2D& Point : PlayerPoints)
	{
		Cells.Add(FIntPoint(FMath::FloorToInt(Point.X), FMath::FloorToInt(Point.Y)));
	}
	Cells.Sort([](const FIntPoint& A, const FIntPoint& B)
	{
		return A.X < B.X || (A.X == B.X && A.Y < B.Y);
	});

	// The loaded ring only changes when a pawn enters another chunk. A settled ring is not scanned again.
	if (bStreamingRingSettled && Cells == LastStreamingCells)
	{
		return;
	}
	LastStreamingCells = Cells;
	bStreamingRingSettled = false;

	const float LoadRadius = static_cast<float>(FMath::Max(StreamingRadius, 1));
	const float LoadRadiusSq = FMath::Square(LoadRadius);
	const float UnloadRadiusSq = FMath::Square(LoadRadius + static_cast<float>(FMath::Max(DespawnMargin, 1)));
	const float UrgentRadiusSq = FMath::Square(FMath::Min(static_cast<float>(FMath::Max(PreSpawnRadius, 0)), LoadRadius));
	const float CollisionRadiusSq = FMath::Square(static_cast<float>(FMath::Max(CollisionRadiusInChunks, 0)));

	auto MinDistSqToPlayers = [&PlayerPoints](const FIntPoint& Coords)
	{
		float Best = TNumericLimits<float>::Max();
		for (const FVector2D& Point : PlayerPoints)
		{
			Best = FMath::Min(Best, DistSqToChunk(Point, Coords));
		}
		return Best;
	};

	// Unload chunks beyond the hysteresis ring of every player, and drop entries destroyed externally.
	TArray<FIntPoint> ToDespawn;
	for (const TPair<FIntPoint, TObjectPtr<ATerrainChunk>>& Pair : ActiveChunks)
	{
		if (!IsValid(Pair.Value) || MinDistSqToPlayers(Pair.Key) > UnloadRadiusSq)
		{
			ToDespawn.Add(Pair.Key);
		}
	}

	for (const FIntPoint& Coords : ToDespawn)
	{
		DespawnChunk(Coords);
	}

	// Missing chunks inside any player's load circle, keyed by distance to the nearest player.
	TMap<FIntPoint, float> Candidates;
	for (const FVector2D& Point : PlayerPoints)
	{
		const int32 MinX = FMath::FloorToInt(Point.X - LoadRadius);
		const int32 MaxX = FMath::FloorToInt(Point.X + LoadRadius);
		const int32 MinY = FMath::FloorToInt(Point.Y - LoadRadius);
		const int32 MaxY = FMath::FloorToInt(Point.Y + LoadRadius);

		for (int32 Y = MinY; Y <= MaxY; ++Y)
		{
			for (int32 X = MinX; X <= MaxX; ++X)
			{
				const FIntPoint Coords(X, Y);
				if (ActiveChunks.Contains(Coords))
				{
					continue;
				}

				const float DistSq = DistSqToChunk(Point, Coords);
				if (DistSq > LoadRadiusSq)
				{
					continue;
				}

				float* Existing = Candidates.Find(Coords);
				if (!Existing)
				{
					Candidates.Add(Coords, DistSq);
				}
				else if (DistSq < *Existing)
				{
					*Existing = DistSq;
				}
			}
		}
	}

	TArray<FPendingChunk> SpawnQueue;
	SpawnQueue.Reserve(Candidates.Num());
	for (const TPair<FIntPoint, float>& Candidate : Candidates)
	{
		SpawnQueue.Add({ Candidate.Key, Candidate.Value });
	}

	SpawnQueue.Sort([](const FPendingChunk& A, const FPendingChunk& B)
	{
		return A.DistSqToNearestPlayer < B.DistSqToNearestPlayer;
	});

	// The queue is sorted, so once the budget is spent and the urgent ring is passed nothing else qualifies.
	const int32 Budget = FMath::Max(MaxChunksPerUpdate, 1);
	int32 SpawnedCount = 0;
	for (const FPendingChunk& Pending : SpawnQueue)
	{
		const bool bUrgent = Pending.DistSqToNearestPlayer <= UrgentRadiusSq;
		if (!bUrgent && SpawnedCount >= Budget)
		{
			break;
		}

		if (SpawnChunk(Pending.Coords, Pending.DistSqToNearestPlayer <= CollisionRadiusSq))
		{
			++SpawnedCount;
		}
	}

	bStreamingRingSettled = ToDespawn.Num() == 0 && SpawnQueue.Num() == 0;

	if (bLogStreaming && (SpawnedCount > 0 || ToDespawn.Num() > 0))
	{
		UE_LOG(LogTerrainManager, Log, TEXT("%s: streaming +%d -%d, pending %d, active %d."),
			*GetName(), SpawnedCount, ToDespawn.Num(), SpawnQueue.Num() - SpawnedCount, ActiveChunks.Num());
	}

#if ENABLE_DRAW_DEBUG
	if (bDrawStreamingDebug)
	{
		const float Lifetime = FMath::Max(StreamingUpdateInterval, 0.05f);
		const float UnloadRadius = FMath::Sqrt(UnloadRadiusSq);
		for (const FVector& Location : PlayerLocations)
		{
			DrawDebugCircle(GetWorld(), Location, LoadRadius * ChunkWorldSize, 64, FColor::Green, false, Lifetime, 0, 50.0f,
				FVector::YAxisVector, FVector::XAxisVector, false);
			DrawDebugCircle(GetWorld(), Location, UnloadRadius * ChunkWorldSize, 64, FColor::Red, false, Lifetime, 0, 50.0f,
				FVector::YAxisVector, FVector::XAxisVector, false);
		}
	}
#endif
}

void ATerrainManager::SpawnChunksInRadius(const FVector2D& CenterInChunks, float RadiusInChunks)
{
	using namespace TerrainManagerPrivate;

	const float RadiusSq = FMath::Square(RadiusInChunks);
	const float CollisionRadiusSq = FMath::Square(static_cast<float>(FMath::Max(CollisionRadiusInChunks, 0)));
	const int32 MinX = FMath::FloorToInt(CenterInChunks.X - RadiusInChunks);
	const int32 MaxX = FMath::FloorToInt(CenterInChunks.X + RadiusInChunks);
	const int32 MinY = FMath::FloorToInt(CenterInChunks.Y - RadiusInChunks);
	const int32 MaxY = FMath::FloorToInt(CenterInChunks.Y + RadiusInChunks);

	for (int32 Y = MinY; Y <= MaxY; ++Y)
	{
		for (int32 X = MinX; X <= MaxX; ++X)
		{
			const FIntPoint Coords(X, Y);
			const float DistSq = DistSqToChunk(CenterInChunks, Coords);
			if (DistSq > RadiusSq)
			{
				continue;
			}

			// A chunk already in the grid for another origin still needs collision if this origin is close.
			// New and pooled chunks receive the flag inside SpawnChunk, before InitializeChunk builds the mesh.
			const bool bEnableCollision = DistSq <= CollisionRadiusSq;
			const bool bAlreadyActive = GetChunkAt(Coords) != nullptr;
			ATerrainChunk* Chunk = SpawnChunk(Coords, bEnableCollision, /*bForceSynchronous=*/true);
			if (Chunk && bAlreadyActive && bEnableCollision)
			{
				Chunk->SetCollisionEnabled(true);
			}
		}
	}
}

void ATerrainManager::GatherPlayerLocations(TArray<FVector>& OutLocations) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PlayerController = It->Get();
		const APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
		if (Pawn)
		{
			OutLocations.Add(Pawn->GetActorLocation());
		}
	}
}

ATerrainChunk* ATerrainManager::AcquireChunk(const FIntPoint& Coords)
{
	using namespace TerrainManagerPrivate;

	while (FreeChunksPool.Num() > 0)
	{
		ATerrainChunk* Pooled = FreeChunksPool.Pop(EAllowShrinking::No);
		if (!IsValid(Pooled))
		{
			continue;
		}

		const FVector ChunkLocation(Coords.X * ChunkWorldSize, Coords.Y * ChunkWorldSize, 0.0f);
		Pooled->PrepareChunkForTeleport();
		Pooled->SetActorLocation(ChunkLocation);
		Pooled->SetActorHiddenInGame(false);
		return Pooled;
	}

	return nullptr;
}

ATerrainChunk* ATerrainManager::SpawnChunk(const FIntPoint& Coords, bool bEnableCollision, bool bForceSynchronous)
{
	using namespace TerrainManagerPrivate;

	UWorld* World = GetWorld();
	if (!World || !ChunkClass)
	{
		return nullptr;
	}

	if (ATerrainChunk* Existing = GetChunkAt(Coords))
	{
		return Existing;
	}

	const FVector SpawnLocation(Coords.X * ChunkWorldSize, Coords.Y * ChunkWorldSize, 0.0f);
	const FTransform SpawnTransform(SpawnLocation);

	ATerrainChunk* Chunk = AcquireChunk(Coords);
	const bool bReused = Chunk != nullptr;
	if (!Chunk)
	{
		Chunk = World->SpawnActorDeferred<ATerrainChunk>(
			ChunkClass,
			SpawnTransform,
			this,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	}

	if (!Chunk)
	{
		UE_LOG(LogTerrainManager, Error, TEXT("%s: failed to spawn chunk at (%d, %d)."), *GetName(), Coords.X, Coords.Y);
		return nullptr;
	}

	FChunkInitData InitData;
	InitData.Seed = WorldSeed;
	InitData.Coords = Coords;
	InitData.bForceSynchronous = bForceSynchronous;

	// The flag is only stored here. InitializeChunk uploads visual LODs once; crossing the collision
	// radius later toggles the body and does not publish sections again.
	Chunk->SetTerrainManager(this);
	Chunk->SetCollisionEnabled(bEnableCollision);
	Chunk->InitializeChunk(InitData);

	if (bReused)
	{
		Chunk->ForceNetUpdate();
	}
	else
	{
		Chunk->FinishSpawning(SpawnTransform);
	}

	ActiveChunks.Add(Coords, Chunk);
	return Chunk;
}

void ATerrainManager::ReleaseChunk(ATerrainChunk* Chunk)
{
	if (!IsValid(Chunk))
	{
		return;
	}

	Chunk->ResetChunkState();
	Chunk->ForceNetUpdate();
	FreeChunksPool.Add(Chunk);
}

void ATerrainManager::DespawnChunk(const FIntPoint& Coords)
{
	TObjectPtr<ATerrainChunk> Chunk;
	if (!ActiveChunks.RemoveAndCopyValue(Coords, Chunk))
	{
		return;
	}

	ReleaseChunk(Chunk);
}
