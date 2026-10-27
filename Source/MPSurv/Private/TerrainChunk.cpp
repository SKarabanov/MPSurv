// Copyright Epic Games, Inc. All Rights Reserved.

#include "TerrainChunk.h"

#include "Async/Async.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "PCGComponent.h"
#include "TerrainManager.h"
#include "TimerManager.h"

namespace TerrainChunkPrivate
{
	constexpr int32 TerrainSectionIndex = 0;
	constexpr int32 WaterSectionIndex = 1;

	/** Hidden LOD 0 collision used by a rendering authority while its visual LOD is coarser. */
	constexpr int32 CollisionSectionIndex = 2;

	// Biome sites are jittered within [0.15, 0.85] of their cell, except the site of cell (0, 0), which is pinned to
	// the world origin. The nearest site is then never farther than 1.42 cells, the second nearest than 1.6 cells,
	// and any site outside the 5x5 block around the query is at least 2 cells away. Blending reaches sites up to
	// 0.15 cells beyond the nearest, so every site that matters lies in the block.
	constexpr int32 BiomeScanRadius = 2;
	constexpr int32 BiomeScanSide = BiomeScanRadius * 2 + 1;
	constexpr float BiomeSiteJitterMin = 0.15f;
	constexpr float BiomeSiteJitterMax = 0.85f;

	// Height blend across a cell border, in DistanceToEdge units as a fraction of the cell size.
	constexpr float BiomeBlendWidthScale = 0.15f;

	// Interior depth reaches 1 this far from the cell border, in DistanceToEdge units as a fraction of the cell size.
	constexpr float BiomeInteriorWidthScale = 0.5f;

	// Arrays indexed by EBiomeType, Ocean included.
	constexpr int32 BiomeCount = 11;

	// Normalized Perlin rarely exceeds ~0.85, so mountain mass is stretched to an effective maximum of 0.88
	// for peaks to actually reach MountainMaxHeight.
	constexpr float EffectiveMaxMacro = 0.88f;

	// Ground under water within this share of the beach ramp counts as sea for the strict biome ID.
	constexpr float FloodedShorePlainsAlpha = 0.18f;

	// Ocean bed profile: depth follows OceanAlpha to this power, a steep drop into the basin and a gentle shelf.
	constexpr float OceanShelfExponent = 0.7f;

	// Every land biome starts near this height on a cell border, so blends between neighbours stay gentle.
	constexpr float LandEdgeZ = 200.0f;

	// Per-biome surface shapes.
	constexpr float PlainsRise = 300.0f;
	constexpr float ForestRise = 500.0f;

	// Mesa rise measured from the cell border, in cm. The shared edge stays low, then the rim climbs about 70 m
	// across the next 70 m, steep enough to read as a plateau and shallow enough for the 1 m grid.
	constexpr float PlateauEscarpmentStartCm = 2000.0f;
	constexpr float PlateauEscarpmentEndCm = 9000.0f;

	// Desert regions lie on a grid 1.5x their largest size, one candidate per grid cell.
	constexpr float DesertRegionSpacingScale = 1.5f;

	// Farthest the nearest biome site can be from any point, in biome cells: a cell corner with all four
	// neighbouring sites jittered to their far extreme.
	constexpr float MaxNearestSiteDistance = 1.25f;

	// Power applied to the ridged dune noise: widens the hollows and sharpens the crests.
	constexpr float DesertDuneSharpness = 2.0f;

	// Oasis pond bottom below sea level; the bowl fills with the global sea water.
	constexpr float OasisWaterDepth = 150.0f;

	// Share of the oasis shore band, from the waterline outwards, taken by the bank up to the desert base.
	constexpr float OasisBankShare = 0.5f;

	// Detail noise fades out within this height of sea level, so the waterline is a smooth contour.
	constexpr float ShoreDetailFadeHeight = 200.0f;

	// Mountain lakes: one candidate per 400 m cell. The valley of the largest cirque reaches ~400 m from its centre
	// through the open front; lookups scan the 5x5 cells around a point, which covers that with margin.
	constexpr float LakeCellSize = 40000.0f;
	constexpr int32 LakeCellScanRadius = 2;
	constexpr float LakeChance = 0.35f;

	// A lake centre must lie at least this deep inside the mountain biome.
	constexpr float LakeMinMountainInterior = 0.3f;

	constexpr float LakeWaterBelowCenter = 1200.0f;
	constexpr float LakeBedDepth = 800.0f;

	// Rim check: the mountain surface is sampled just beyond the shore, and the lake is dropped if the ring
	// leaves the mountain biome or dips too close to the lowland edge height.
	constexpr int32 LakeRimSampleCount = 8;
	constexpr float LakeRimSampleOffset = 1200.0f;
	constexpr float LakeMinRimAboveLand = 500.0f;

	// Lake water is skipped where the floor is deeper than the bowl by more than this, i.e. over a drop-off.
	constexpr float LakeFloorTolerance = 600.0f;

	// Shore line irregularity, applied to the morphological distance.
	constexpr float ShoreWarpFrequency = 0.00015f;
	constexpr float ShoreWarpAmplitude = 1800.0f;

	// A cirque's back wall is reached 1.35x sooner than the mean radius, its open front 0.65x.
	constexpr float CirqueAsymmetry = 0.35f;

	// Slowest growth of the morphological distance over all shapes (the cirque front; rifts never drop below 1).
	constexpr float MinLakeShapeScale = 1.0f - CirqueAsymmetry;

	// Valley around the lake, as multiples of the lake radius in morphological distance.
	constexpr float LakeValleyRadiusScale = 2.2f;
	constexpr float LakeTransitionRadiusScale = LakeValleyRadiusScale * 1.5f;

	// Share of the raw mountain height kept at the valley edge; the rest is flattened into a saddle.
	constexpr float LakeValleyMountainBlend = 0.65f;
	constexpr float LakeShoreMinRise = 50.0f;
	constexpr float LakeShoreMaxRise = 300.0f;

	// Detail noise is softened over the water and the valley floor, then fades back in across the transition.
	constexpr float LakeDetailScale = 0.08f;
	constexpr float LakeDetailRadiusScale = 1.8f;

	// Farthest any lake reaches from its centre: the largest caldera's transition radius through a cirque's open front.
	constexpr float LakeMaxRadius = 7500.0f;
	constexpr float LakeMaxReach = LakeMaxRadius * LakeTransitionRadiusScale / MinLakeShapeScale + ShoreWarpAmplitude;

	// Rivers are sea straits: winding channels at sea level that cut the land from ocean to ocean. Two-layer domain
	// warp of the course: broad bends (~800 m wavelength) plus meanders (~250 m). Both layers are sampled from the
	// unwarped position, and each amplitude * frequency stays well below 1, so the warp never folds space into
	// pinched loops.
	constexpr float RiverBendWarpFrequency = 0.0000125f;
	constexpr float RiverBendWarpAmplitude = 16000.0f;
	constexpr float RiverMeanderFrequency = 0.00004f;
	constexpr float RiverMeanderAmplitude = 4000.0f;

	// Alternating wide reaches and narrows, in [0.7, 1.3], ~500 m wavelength.
	constexpr float RiverWidthVariationFrequency = 0.00002f;

	// A strait shoals out across this distance as it approaches a mountain cell, and is gone at the foothill.
	constexpr float RiverMountainFadeWidth = 30000.0f;

	// Step of the finite differences that turn the river noise into a distance in world units.
	constexpr float RiverGradientStep = 100.0f;

	// Upper bound of the river noise slope per noise unit, warp included.
	constexpr float RiverMaxNoiseSlope = 6.0f;

	// The strait bed is flat across this inner share of the water half-width.
	constexpr float RiverFlatBedShare = 0.4f;

	// Straits fade out across this band beyond StraitFreeRadius.
	constexpr float StraitFreeFadeWidth = 20000.0f;

	/** How fast the morphological distance grows along a unit direction from the centre, ignoring the shore warp. */
	float ComputeLakeShapeScale(const FMountainLakeData& Lake, float DirX, float DirY)
	{
		if (Lake.LakeType == EMountainLakeType::TectonicRift)
		{
			// Rotated 45 degrees so the rift runs along the mountain spine.
			constexpr float Diagonal = 0.7071f;
			const float RotX = DirX * Diagonal - DirY * Diagonal;
			const float RotY = DirX * Diagonal + DirY * Diagonal;
			return FMath::Sqrt(FMath::Square(RotX * Lake.AspectRatio) + FMath::Square(RotY));
		}

		return 1.0f + CirqueAsymmetry * FMath::Cos(FMath::Atan2(DirY, DirX) - Lake.CirqueAngle);
	}

	/**
	 * Morphological distance from the lake centre: equals Lake.Radius on the shore line.
	 * Rifts are ellipses with a 1:AspectRatio axis ratio, cirques are horseshoes with a steep back wall,
	 * and both get a warped, irregular shore.
	 */
	float ComputeLakeEffectiveDistance(const FMountainLakeData& Lake, float WorldX, float WorldY)
	{
		const float DX = WorldX - static_cast<float>(Lake.Center.X);
		const float DY = WorldY - static_cast<float>(Lake.Center.Y);

		const float ShoreWarp = FMath::PerlinNoise2D(FVector2D(WorldX * ShoreWarpFrequency, WorldY * ShoreWarpFrequency)) * ShoreWarpAmplitude;
		const float Distance = FMath::Sqrt(DX * DX + DY * DY);

		if (Distance <= KINDA_SMALL_NUMBER)
		{
			return FMath::Max(ShoreWarp, 0.0f);
		}

		// A negative warp near the centre is clamped, otherwise the symmetric beach profile would raise a mound there.
		const float Scale = ComputeLakeShapeScale(Lake, DX / Distance, DY / Distance);
		const float EffectiveDistance = Lake.LakeType == EMountainLakeType::TectonicRift
			? Distance * Scale + ShoreWarp
			: (Distance + ShoreWarp) * Scale;
		return FMath::Max(EffectiveDistance, 0.0f);
	}

	/**
	 * Terrain height reshaped by a lake: a gentle bowl below WaterZ, a walkable valley floor rising a few metres
	 * above the water, then a smooth blend back into the raw mountain surface at the transition radius.
	 */
	float ComputeLakeTerrainZ(const FMountainLakeData& Lake, float EffectiveDistance, float RawMountainZ)
	{
		const float Radius = Lake.Radius;
		const float ValleyRadius = Radius * LakeValleyRadiusScale;
		const float TransitionRadius = Radius * LakeTransitionRadiusScale;

		if (EffectiveDistance <= Radius)
		{
			const float T = EffectiveDistance / Radius;
			return Lake.WaterZ - Lake.BedDepth * (1.0f - T * T);
		}

		if (EffectiveDistance <= ValleyRadius)
		{
			// The valley floor is pulled towards the shore level, flattening the mountain into a saddle.
			const float ValleyT = (EffectiveDistance - Radius) / (ValleyRadius - Radius);
			const float ShoreZ = Lake.WaterZ + FMath::Lerp(LakeShoreMinRise, LakeShoreMaxRise, ValleyT);
			const float BlendToMountain = FMath::SmoothStep(0.0f, 1.0f, ValleyT);
			return FMath::Lerp(ShoreZ, RawMountainZ, BlendToMountain * LakeValleyMountainBlend);
		}

		if (EffectiveDistance <= TransitionRadius)
		{
			const float BlendOut = FMath::SmoothStep(0.0f, 1.0f, (EffectiveDistance - ValleyRadius) / (TransitionRadius - ValleyRadius));
			const float ValleyEdgeZ = FMath::Lerp(Lake.WaterZ + LakeShoreMaxRise, RawMountainZ, LakeValleyMountainBlend);
			return FMath::Lerp(ValleyEdgeZ, RawMountainZ, BlendOut);
		}

		return RawMountainZ;
	}

	/** True if the lake's valley, out to its transition radius, reaches the point. Outputs the morphological distance. */
	bool LakeReachesPoint(const FMountainLakeData& Lake, float WorldX, float WorldY, float& OutEffectiveDistance)
	{
		const float TransitionRadius = Lake.Radius * LakeTransitionRadiusScale;

		// Cheap Euclidean reject before the warped distance: no shape reaches farther than this.
		const float MaxReach = TransitionRadius / MinLakeShapeScale + ShoreWarpAmplitude;
		if (FVector2D::DistSquared(Lake.Center, FVector2D(WorldX, WorldY)) > FMath::Square(MaxReach))
		{
			return false;
		}

		OutEffectiveDistance = ComputeLakeEffectiveDistance(Lake, WorldX, WorldY);
		return OutEffectiveDistance <= TransitionRadius;
	}

	/** The lake the point lies deepest inside (smallest distance relative to the lake size), or bExists == false. */
	FMountainLakeData FindDeepestLake(TConstArrayView<FMountainLakeData> Lakes, float WorldX, float WorldY)
	{
		FMountainLakeData BestLake;
		float BestNormalizedDistance = TNumericLimits<float>::Max();
		for (const FMountainLakeData& Lake : Lakes)
		{
			float EffectiveDistance = 0.0f;
			if (!LakeReachesPoint(Lake, WorldX, WorldY, EffectiveDistance))
			{
				continue;
			}

			const float NormalizedDistance = EffectiveDistance / Lake.Radius;
			if (NormalizedDistance < BestNormalizedDistance)
			{
				BestNormalizedDistance = NormalizedDistance;
				BestLake = Lake;
			}
		}

		return BestLake;
	}

	/** Height of grid vertex (X, Y) in a height cache that has a one-cell border. */
	FORCEINLINE float CachedHeight(const TArray<float>& Heights, int32 HeightsPerSide, int32 X, int32 Y)
	{
		return Heights[(X + 1) + ((Y + 1) * HeightsPerSide)];
	}

	/** Appends the two up-facing triangles of a quad, split along the TL-BR or BL-TR diagonal. */
	FORCEINLINE void AppendQuad(TArray<int32>& Triangles, int32 BottomLeft, int32 BottomRight, int32 TopLeft, int32 TopRight, bool bSplitAlongBLTR)
	{
		if (bSplitAlongBLTR)
		{
			Triangles.Add(BottomLeft);
			Triangles.Add(TopLeft);
			Triangles.Add(TopRight);

			Triangles.Add(BottomLeft);
			Triangles.Add(TopRight);
			Triangles.Add(BottomRight);
		}
		else
		{
			Triangles.Add(BottomLeft);
			Triangles.Add(TopLeft);
			Triangles.Add(BottomRight);

			Triangles.Add(BottomRight);
			Triangles.Add(TopLeft);
			Triangles.Add(TopRight);
		}
	}

	/**
	 * Checkerboard diagonal. On a slope that rises in both X and Y the closer-height test picks the same
	 * diagonal for every quad, and those folds line up into ridges. Alternating the split breaks the ridge.
	 */
	FORCEINLINE bool SplitQuadCheckerboard(int32 QuadX, int32 QuadY)
	{
		return ((QuadX + QuadY) & 1) == 0;
	}

	void BuildTerrainTriangles(int32 QuadCount, TArray<int32>& OutTriangles)
	{
		const int32 VerticesPerSide = QuadCount + 1;
		OutTriangles.Reset(QuadCount * QuadCount * 6);

		for (int32 Y = 0; Y < QuadCount; ++Y)
		{
			for (int32 X = 0; X < QuadCount; ++X)
			{
				const int32 BottomLeft = X + (Y * VerticesPerSide);
				const int32 BottomRight = BottomLeft + 1;
				const int32 TopLeft = BottomLeft + VerticesPerSide;
				const int32 TopRight = TopLeft + 1;
				AppendQuad(OutTriangles, BottomLeft, BottomRight, TopLeft, TopRight, SplitQuadCheckerboard(X, Y));
			}
		}
	}

	/** Height on a triangle given UV in the unit square and the three corner heights. */
	FORCEINLINE float InterpolateTriangleHeight(float U0, float V0, float Z0, float U1, float V1, float Z1, float U2, float V2, float Z2, float U, float V)
	{
		const float Denom = (V1 - V2) * (U0 - U2) + (U2 - U1) * (V0 - V2);
		if (FMath::Abs(Denom) < KINDA_SMALL_NUMBER)
		{
			return (Z0 + Z1 + Z2) * (1.0f / 3.0f);
		}

		const float W0 = ((V1 - V2) * (U - U2) + (U2 - U1) * (V - V2)) / Denom;
		const float W1 = ((V2 - V0) * (U - U2) + (U0 - U2) * (V - V2)) / Denom;
		const float W2 = 1.0f - W0 - W1;
		return W0 * Z0 + W1 * Z1 + W2 * Z2;
	}

	/**
	 * Two biome IDs and the weight of the second, packed so a shared pair writes the same IDs on every vertex.
	 * R holds the lower ID, G the higher ID, B the weight of G in 0..255. The GPU then interpolates only B.
	 */
	FColor PackBiomeVertexColor(int32 PrimaryId, float PrimaryWeight, int32 SecondaryId, float SecondaryWeight)
	{
		const float Denom = PrimaryWeight + SecondaryWeight;
		const float BlendFactor = Denom > KINDA_SMALL_NUMBER ? SecondaryWeight / Denom : 0.0f;

		float R_ID;
		float G_ID;
		float B_Weight;
		if (PrimaryId < SecondaryId)
		{
			R_ID = static_cast<float>(PrimaryId);
			G_ID = static_cast<float>(SecondaryId);
			B_Weight = BlendFactor;
		}
		else
		{
			R_ID = static_cast<float>(SecondaryId);
			G_ID = static_cast<float>(PrimaryId);
			B_Weight = 1.0f - BlendFactor;
		}

		return FColor(
			static_cast<uint8>(R_ID),
			static_cast<uint8>(G_ID),
			static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(B_Weight * 255.0f), 0, 255)),
			255);
	}

	/** Heavier of the two packed biomes. B is the weight of G, so G wins from half weight upward. */
	uint8 DecodeDominantBiome(FColor Color)
	{
		return Color.B >= 128 ? Color.G : Color.R;
	}

	/** The two strictly positive weights, heaviest first. A missing second biome repeats the first with weight 0. */
	void SelectTopBiomeWeights(const float Weights[BiomeCount], int32& OutPrimaryId, float& OutPrimaryWeight, int32& OutSecondaryId, float& OutSecondaryWeight)
	{
		OutPrimaryId = static_cast<int32>(EBiomeType::Ocean);
		OutSecondaryId = static_cast<int32>(EBiomeType::Ocean);
		OutPrimaryWeight = -1.0f;
		OutSecondaryWeight = -1.0f;

		for (int32 BiomeIndex = 0; BiomeIndex < BiomeCount; ++BiomeIndex)
		{
			const float Weight = Weights[BiomeIndex];
			if (Weight <= 0.0f)
			{
				continue;
			}

			if (Weight > OutPrimaryWeight)
			{
				OutSecondaryWeight = OutPrimaryWeight;
				OutSecondaryId = OutPrimaryId;
				OutPrimaryWeight = Weight;
				OutPrimaryId = BiomeIndex;
			}
			else if (Weight > OutSecondaryWeight)
			{
				OutSecondaryWeight = Weight;
				OutSecondaryId = BiomeIndex;
			}
		}

		if (OutPrimaryWeight < 0.0f)
		{
			OutPrimaryWeight = 0.0f;
		}
		if (OutSecondaryWeight < 0.0f)
		{
			OutSecondaryId = OutPrimaryId;
			OutSecondaryWeight = 0.0f;
		}
	}

	/**
	 * Samples LOD-0 terrain at a local XY: dominant biome from the packed vertex colour, height on the adaptive
	 * triangle plane, bilinear normal. Returns false if the buffer is not a usable LOD-0 grid.
	 */
	bool SampleLod0Surface(const FTerrainMeshBuffers& Lod0, float LocalX, float LocalY, uint8& OutBiomeId, float& OutHeight,
		FVector& OutNormal)
	{
		const int32 QuadCount = Lod0.QuadCount;
		if (QuadCount <= 0 || Lod0.Step <= 0.0f || Lod0.Heights.Num() == 0 || Lod0.Normals.Num() == 0 || Lod0.Colors.Num() == 0)
		{
			return false;
		}

		const int32 VerticesPerSide = QuadCount + 1;
		const int32 GridVertexCount = VerticesPerSide * VerticesPerSide;
		if (Lod0.Normals.Num() < GridVertexCount || Lod0.Colors.Num() < GridVertexCount)
		{
			return false;
		}

		const float Step = Lod0.Step;
		const float MaxLocal = Step * static_cast<float>(QuadCount);
		const float ClampedX = FMath::Clamp(LocalX, 0.0f, MaxLocal - KINDA_SMALL_NUMBER);
		const float ClampedY = FMath::Clamp(LocalY, 0.0f, MaxLocal - KINDA_SMALL_NUMBER);

		const float GridX = ClampedX / Step;
		const float GridY = ClampedY / Step;
		const int32 QuadX = FMath::Clamp(FMath::FloorToInt(GridX), 0, QuadCount - 1);
		const int32 QuadY = FMath::Clamp(FMath::FloorToInt(GridY), 0, QuadCount - 1);
		const float FracX = FMath::Clamp(GridX - static_cast<float>(QuadX), 0.0f, 1.0f);
		const float FracY = FMath::Clamp(GridY - static_cast<float>(QuadY), 0.0f, 1.0f);

		const int32 NearX = QuadX + (FracX >= 0.5f ? 1 : 0);
		const int32 NearY = QuadY + (FracY >= 0.5f ? 1 : 0);
		OutBiomeId = DecodeDominantBiome(Lod0.Colors[NearX + NearY * VerticesPerSide]);

		const float Z00 = CachedHeight(Lod0.Heights, Lod0.HeightsPerSide, QuadX, QuadY);
		const float Z10 = CachedHeight(Lod0.Heights, Lod0.HeightsPerSide, QuadX + 1, QuadY);
		const float Z01 = CachedHeight(Lod0.Heights, Lod0.HeightsPerSide, QuadX, QuadY + 1);
		const float Z11 = CachedHeight(Lod0.Heights, Lod0.HeightsPerSide, QuadX + 1, QuadY + 1);

		// Same diagonal as BuildTerrainTriangles, so a height probe lands on the triangle that is drawn.
		const bool bSplitAlongBLTR = SplitQuadCheckerboard(QuadX, QuadY);
		if (bSplitAlongBLTR)
		{
			// Diagonal BL-TR. FracX <= FracY → BL-TL-TR, else BL-TR-BR.
			OutHeight = (FracX <= FracY)
				? InterpolateTriangleHeight(0.0f, 0.0f, Z00, 0.0f, 1.0f, Z01, 1.0f, 1.0f, Z11, FracX, FracY)
				: InterpolateTriangleHeight(0.0f, 0.0f, Z00, 1.0f, 1.0f, Z11, 1.0f, 0.0f, Z10, FracX, FracY);
		}
		else
		{
			// Diagonal TL-BR. FracX + FracY <= 1 → BL-TL-BR, else BR-TL-TR.
			OutHeight = (FracX + FracY <= 1.0f)
				? InterpolateTriangleHeight(0.0f, 0.0f, Z00, 0.0f, 1.0f, Z01, 1.0f, 0.0f, Z10, FracX, FracY)
				: InterpolateTriangleHeight(1.0f, 0.0f, Z10, 0.0f, 1.0f, Z01, 1.0f, 1.0f, Z11, FracX, FracY);
		}

		const int32 I00 = QuadX + QuadY * VerticesPerSide;
		const int32 I10 = I00 + 1;
		const int32 I01 = I00 + VerticesPerSide;
		const int32 I11 = I01 + 1;
		OutNormal = FMath::Lerp(
			FMath::Lerp(Lod0.Normals[I00], Lod0.Normals[I10], FracX),
			FMath::Lerp(Lod0.Normals[I01], Lod0.Normals[I11], FracX),
			FracY).GetSafeNormal();

		return true;
	}

	/**
	 * Appends a skirt around the grid perimeter: every edge vertex is duplicated SkirtDepth lower and joined to
	 * its original by outward-facing quads. Skirt vertices copy the edge normal and biome colour, so the wall shades
	 * like the surface edge and hides cracks against a neighbour at another LOD.
	 */
	void AppendSkirts(FTerrainMeshBuffers& Buffers, float SkirtDepth)
	{
		const int32 QuadCount = Buffers.QuadCount;
		const int32 VerticesPerSide = QuadCount + 1;

		// Perimeter loop walked counter-clockwise seen from above (+X, +Y, -X, -Y), so "outward" is always
		// to the right of the walking direction and one triangle order fits all four sides.
		TArray<int32, TInlineAllocator<128>> Perimeter;
		Perimeter.Reserve(QuadCount * 4);
		for (int32 X = 0; X < QuadCount; ++X)
		{
			Perimeter.Add(X);
		}
		for (int32 Y = 0; Y < QuadCount; ++Y)
		{
			Perimeter.Add(QuadCount + (Y * VerticesPerSide));
		}
		for (int32 X = QuadCount; X > 0; --X)
		{
			Perimeter.Add(X + (QuadCount * VerticesPerSide));
		}
		for (int32 Y = QuadCount; Y > 0; --Y)
		{
			Perimeter.Add(Y * VerticesPerSide);
		}

		// Skirt UVs continue outward by the skirt depth at the surface UV scale, so the wall shows texture
		// instead of a single edge texel stretched down its full height.
		const float SkirtUVOffset = SkirtDepth / (static_cast<float>(QuadCount) * Buffers.Step);
		const bool bHasColors = Buffers.Colors.Num() == Buffers.Vertices.Num();

		const int32 FirstSkirtVertex = Buffers.Vertices.Num();
		for (const int32 EdgeVertex : Perimeter)
		{
			const int32 GridX = EdgeVertex % VerticesPerSide;
			const int32 GridY = EdgeVertex / VerticesPerSide;
			const float OutwardX = GridX == 0 ? -1.0f : (GridX == QuadCount ? 1.0f : 0.0f);
			const float OutwardY = GridY == 0 ? -1.0f : (GridY == QuadCount ? 1.0f : 0.0f);

			// Copied first: TArray::Add rejects a reference into the array being grown.
			const FVector SkirtPosition = Buffers.Vertices[EdgeVertex] - FVector(0.0f, 0.0f, SkirtDepth);
			const FVector EdgeNormal = Buffers.Normals[EdgeVertex];
			const FVector2D SkirtUV = Buffers.UVs[EdgeVertex] + FVector2D(OutwardX, OutwardY) * SkirtUVOffset;
			const FProcMeshTangent EdgeTangent = Buffers.Tangents[EdgeVertex];

			Buffers.Vertices.Add(SkirtPosition);
			Buffers.Normals.Add(EdgeNormal);
			Buffers.UVs.Add(SkirtUV);
			Buffers.Tangents.Add(EdgeTangent);

			if (bHasColors)
			{
				const FColor EdgeColor = Buffers.Colors[EdgeVertex];
				Buffers.Colors.Add(EdgeColor);
			}
		}

		const int32 LoopLength = Perimeter.Num();
		for (int32 Index = 0; Index < LoopLength; ++Index)
		{
			const int32 NextIndex = (Index + 1) % LoopLength;
			const int32 TopA = Perimeter[Index];
			const int32 TopB = Perimeter[NextIndex];
			const int32 BottomA = FirstSkirtVertex + Index;
			const int32 BottomB = FirstSkirtVertex + NextIndex;

			Buffers.Triangles.Add(TopA);
			Buffers.Triangles.Add(TopB);
			Buffers.Triangles.Add(BottomA);

			Buffers.Triangles.Add(TopB);
			Buffers.Triangles.Add(BottomB);
			Buffers.Triangles.Add(BottomA);
		}
	}

	/**
	 * A pure node (R == G) that touches a real pair adopts that pair, with B pinned to 0 or 255.
	 * One ring is enough for the border triangle; the ring beyond it is separated by SplitInconsistentBiomeTriangles
	 * so a later pure node of (2, 2) never shares an edge with (2, 6).
	 */
	void HarmonizeBiomePairColors(TArray<FColor>& Colors, int32 Width)
	{
		const TArray<FColor> Source = Colors;
		const int32 NeighborOffsets[8][2] = {
			{-1, -1}, {0, -1}, {1, -1},
			{-1, 0},           {1, 0},
			{-1, 1},  {0, 1},  {1, 1}
		};

		for (int32 Y = 0; Y < Width; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const FColor Center = Source[X + Y * Width];
				if (Center.R != Center.G)
				{
					continue;
				}

				const uint8 Dominant = DecodeDominantBiome(Center);
				int32 BestScore = -1;
				FColor BestPair = Center;
				for (const int32 (&Offset)[2] : NeighborOffsets)
				{
					const int32 NeighborX = X + Offset[0];
					const int32 NeighborY = Y + Offset[1];
					if (NeighborX < 0 || NeighborY < 0 || NeighborX >= Width || NeighborY >= Width)
					{
						continue;
					}

					const FColor Neighbor = Source[NeighborX + NeighborY * Width];
					if (Neighbor.R == Neighbor.G || (Neighbor.R != Dominant && Neighbor.G != Dominant))
					{
						continue;
					}

					// Prefer the neighbor that is actually blending. Equal strength keeps the lower pair, so a
					// triple point always drops the same third biome.
					const int32 Score = 255 - FMath::Abs(static_cast<int32>(Neighbor.B) - 128);
					const bool bLowerPair = Neighbor.R < BestPair.R || (Neighbor.R == BestPair.R && Neighbor.G < BestPair.G);
					if (Score > BestScore || (Score == BestScore && bLowerPair))
					{
						BestScore = Score;
						BestPair = Neighbor;
					}
				}

				if (BestScore < 0)
				{
					continue;
				}

				Colors[X + Y * Width] = FColor(BestPair.R, BestPair.G, Dominant == BestPair.G ? 255 : 0, 255);
			}
		}
	}

	struct FBiomeVertexCopyKey
	{
		int32 Source = 0;
		uint8 R = 0;
		uint8 G = 0;
		uint8 B = 0;

		friend uint32 GetTypeHash(const FBiomeVertexCopyKey& Key)
		{
			return HashCombine(GetTypeHash(Key.Source), GetTypeHash((static_cast<uint32>(Key.R) << 16) | (static_cast<uint32>(Key.G) << 8) | Key.B));
		}

		bool operator==(const FBiomeVertexCopyKey& Other) const
		{
			return Source == Other.Source && R == Other.R && G == Other.G && B == Other.B;
		}
	};

	/**
	 * Gives every triangle one (R, G) pair. A corner whose pair disagrees is copied, the third biome is dropped,
	 * and B is pinned to 0 or 255. The copy is not shared with a neighbor triangle of a different pair, so the
	 * rasterizer cannot interpolate IDs along that edge.
	 */
	void SplitInconsistentBiomeTriangles(FTerrainMeshBuffers& Buffers)
	{
		TMap<FBiomeVertexCopyKey, int32> Copies;
		TArray<int32>& Triangles = Buffers.Triangles;

		for (int32 Triangle = 0; Triangle + 2 < Triangles.Num(); Triangle += 3)
		{
			const int32 CornerIndex[3] = {Triangles[Triangle], Triangles[Triangle + 1], Triangles[Triangle + 2]};
			if (!Buffers.Colors.IsValidIndex(CornerIndex[0]) || !Buffers.Colors.IsValidIndex(CornerIndex[1]) || !Buffers.Colors.IsValidIndex(CornerIndex[2]))
			{
				continue;
			}

			const FColor CornerColor[3] = {Buffers.Colors[CornerIndex[0]], Buffers.Colors[CornerIndex[1]], Buffers.Colors[CornerIndex[2]]};
			if (CornerColor[0].R == CornerColor[1].R && CornerColor[0].G == CornerColor[1].G
				&& CornerColor[0].R == CornerColor[2].R && CornerColor[0].G == CornerColor[2].G)
			{
				continue;
			}

			FColor Winner = CornerColor[0];
			int32 BestScore = -1;
			uint8 LowId = CornerColor[0].R;
			uint8 HighId = CornerColor[0].R;
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				LowId = FMath::Min(LowId, DecodeDominantBiome(CornerColor[Corner]));
				HighId = FMath::Max(HighId, DecodeDominantBiome(CornerColor[Corner]));
				if (CornerColor[Corner].R == CornerColor[Corner].G)
				{
					continue;
				}

				const int32 Score = 255 - FMath::Abs(static_cast<int32>(CornerColor[Corner].B) - 128);
				const bool bLowerPair = CornerColor[Corner].R < Winner.R || (CornerColor[Corner].R == Winner.R && CornerColor[Corner].G < Winner.G);
				if (Score > BestScore || (Score == BestScore && bLowerPair))
				{
					BestScore = Score;
					Winner = CornerColor[Corner];
				}
			}

			if (BestScore < 0)
			{
				Winner = FColor(LowId, HighId, 0, 255);
			}

			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				const FColor Color = CornerColor[Corner];
				if (Color.R == Winner.R && Color.G == Winner.G)
				{
					continue;
				}

				const uint8 Dominant = DecodeDominantBiome(Color);
				const uint8 PinnedB = Dominant == Winner.G ? 255 : 0;
				const FBiomeVertexCopyKey Key{CornerIndex[Corner], Winner.R, Winner.G, PinnedB};
				if (const int32* Existing = Copies.Find(Key))
				{
					Triangles[Triangle + Corner] = *Existing;
					continue;
				}

				const int32 Source = CornerIndex[Corner];
				const FVector Position = Buffers.Vertices[Source];
				const FVector Normal = Buffers.Normals[Source];
				const FVector2D UV = Buffers.UVs[Source];
				const FProcMeshTangent Tangent = Buffers.Tangents[Source];
				const int32 CopyIndex = Buffers.Vertices.Add(Position);
				Buffers.Normals.Add(Normal);
				Buffers.UVs.Add(UV);
				Buffers.Tangents.Add(Tangent);
				Buffers.Colors.Add(FColor(Winner.R, Winner.G, PinnedB, 255));
				Copies.Add(Key, CopyIndex);
				Triangles[Triangle + Corner] = CopyIndex;
			}
		}
	}
}

/**
 * Terrain math of one chunk. Owns copies of everything it reads (settings, init data, lake cache) and never
 * touches a UObject, so a worker thread can run it while the actor rebuilds, changes or is destroyed.
 */
class FTerrainGenerator : public FTerrainGenerationSettings
{
public:
	FTerrainGenerator(const FTerrainGenerationSettings& InSettings, const FChunkInitData& InInitData, float InCellSize, int32 InQuadsPerSide)
		: FTerrainGenerationSettings(InSettings)
		, InitData(InInitData)
		, CellSize(InCellSize)
		, QuadsPerSide(InQuadsPerSide)
		, ChunkWorldSize(InCellSize * static_cast<float>(InQuadsPerSide))
	{
		float SeedLow = 0.0f;
		float SeedHigh = 0.0f;
		GetSeedOffsets(SeedLow, SeedHigh);

		float OceanLimit = 0.0f;
		float MountainLimit = 0.0f;
		GetZoneLimits(OceanLimit, MountainLimit);

		// The lift is never smaller than what the world origin needs to reach full land height, so the spawn is
		// on land for every seed.
		StartIslandLift = FMath::Max(StartIslandStrength, MountainLimit - SampleContinentNoise(0.0f, 0.0f, SeedLow, SeedHigh));
	}

	/** Height at a world position, with the lakes found around that position rather than the chunk cache. */
	float GetHeightAt(float WorldX, float WorldY) const;

	FBiomeContext GetBiomeContext(float WorldX, float WorldY) const;

	/** Collects the validated lakes that can reach the chunk or its height border. Call before building geometry. */
	void CacheChunkLakes();

	/** Heights, triangles, vertex attributes and biome colours of the chunk grid at a LOD, optionally with skirts. */
	/** Builds terrain geometry at the requested LOD. No UObject access. */
	void BuildTerrainBuffers(int32 LOD, bool bWithSkirts, FTerrainMeshBuffers& OutBuffers) const;

	/**
	 * Independent grass grid per biome (SpawnStep). Samples height, biome and normals from an already-built LOD-0
	 * terrain buffer (skirts ignored). Fills local-space transforms and wind phases. No UObject access.
	 */
	void BuildGrassTransforms(const FTerrainMeshBuffers& Lod0Terrain, TMap<uint8, TArray<FTransform>>& OutTransforms,
		TMap<uint8, TArray<float>>& OutWindPhases) const;

	TArray<FBiomeGrassSpawnRule> GrassRules;

	/** Sea and lake surfaces over the given terrain grid; empty buffers where no water reaches the chunk. */
	void BuildWaterBuffers(const FTerrainMeshBuffers& Terrain, FTerrainMeshBuffers& OutWater) const;

private:
	static constexpr int32 NumLODs = ATerrainChunk::NumLODs;

	void GetSeedOffsets(float& OutSeedLow, float& OutSeedHigh) const;

	/** Biome of the Voronoi cell whose jittered site is CellCenter, from distance and latitude after a low-frequency warp. */
	EBiomeType DetermineBiomeForCell(FVector2D CellCenter) const;

	/** Jittered Voronoi site of a biome cell. */
	FVector2D GetBiomeSite(int32 CellX, int32 CellY) const;

	/** Voronoi site nearest to a point, without drawing any biome. */
	FVector2D FindNearestBiomeSite(const FVector2D& Point) const;

	/** Whether a biome site lies in a desert region: a disc between DesertRegionMinSize and MaxSize across. */
	bool IsInDesertRegion(const FVector2D& Site) const;

	/** Scans the Voronoi sites around a point; optionally returns every site that blends into it. */
	FBiomeContext QueryBiomeSites(float WorldX, float WorldY, TArray<FBiomeSite, TInlineAllocator<8>>* OutBlendSites) const;

	float GetBiomeBlendWidth() const;
	float GetBiomeInterior(float DistanceToEdge) const;

	/** Blend weight of one biome at a point, in [0, 1], weighted like the height blend in ComputeHeight. */
	float ComputeBiomeShare(float WorldX, float WorldY, EBiomeType Biome) const;
	void GetZoneLimits(float& OutOceanLimit, float& OutMountainLimit) const;

	/** Raw continent noise in [0, 1], before the start island lift and the straits. */
	float SampleContinentNoise(float WorldX, float WorldY, float SeedLow, float SeedHigh) const;

	/** 1 at the world origin, falling smoothly to 0 at StartIslandRadius. */
	float ComputeStartIslandBias(float WorldX, float WorldY) const;

	float SampleContinentalness(float WorldX, float WorldY, float SeedLow, float SeedHigh) const;
	float ComputeLandRamp(float MacroSample) const;
	/** Rolling hills in [-1, 1] at the given frequency. */
	float SampleHills(float WorldX, float WorldY, float Frequency, float SeedLow, float SeedHigh) const;

	/**
	 * Canyon wall at a point. Slope is 0 on the floor and 1 on the mesa. Steepness is 0 on both and 1 where the
	 * wall mask climbs fastest.
	 */
	struct FCanyonWallSample
	{
		float Slope = 1.0f;
		float Steepness = 0.0f;
	};

	/** Domain-warped canyon distance, shaped into the floor, wall and mesa. */
	FCanyonWallSample SampleCanyonWall(float WorldX, float WorldY, float SeedLow, float SeedHigh) const;

	/** Fine surface roughness for one biome; 0 for swamp and oasis. DistanceToEdge damps plateau detail on canyon walls. */
	float SampleBiomeDetail(float WorldX, float WorldY, EBiomeType Biome, float DistanceToEdge, float SeedLow, float SeedHigh) const;

	float ComputePlains(float WorldX, float WorldY, float Interior, float SeedLow, float SeedHigh) const;
	float ComputeTundra(float WorldX, float WorldY, float Interior, float SeedLow, float SeedHigh) const;
	float ComputeForest(float WorldX, float WorldY, float Interior, float SeedLow, float SeedHigh) const;
	float ComputeJungle(float WorldX, float WorldY, float Interior, float SeedLow, float SeedHigh) const;
	float ComputeSwamp(float WorldX, float WorldY, float SeedLow, float SeedHigh) const;

	/** Dune height above DesertBaseHeight deep inside a desert cell: gentle flanks, sharp crests. */
	float ComputeDesertDunes(float WorldX, float WorldY, float SeedLow, float SeedHigh) const;
	float ComputeDesert(float WorldX, float WorldY, float Interior, float SeedLow, float SeedHigh) const;

	/** OasisRadius capped at the grid size, so an oasis only reaches the cells next to its own. */
	float GetOasisRadius() const;

	/** Whether an oasis candidate at Center is kept: deep inside a desert cell and in a hollow between dunes. */
	bool IsOasisSiteValid(const FVector2D& Center, float SeedLow, float SeedHigh) const;

	/** Distance to the centre of the nearest valid oasis covering the point; false when no oasis covers it. */
	bool FindOasis(float WorldX, float WorldY, float SeedLow, float SeedHigh, float& OutDistance) const;

	/** Oasis ground at Distance from its centre, levelling the desert ground DesertZ into a shore, bank and pond. */
	float ComputeOasisZ(float Distance, float DesertZ) const;
	float ComputeMountains(float WorldX, float WorldY, float MacroSample, float Interior, float SeedLow, float SeedHigh, float DetailScale = 1.0f) const;
	float ComputePlateau(float WorldX, float WorldY, float DistanceToEdge, float SeedLow, float SeedHigh) const;
	float ComputeBiomeHeight(float WorldX, float WorldY, EBiomeType Biome, float MacroSample, float Interior, float DistanceToEdge,
		float SeedLow, float SeedHigh, float DetailScale = 1.0f) const;

	bool MakeLakeCandidate(int32 CellX, int32 CellY, FMountainLakeData& OutLake) const;
	bool ValidateLake(FMountainLakeData& Lake, float SeedLow, float SeedHigh) const;
	void GetMountainLakesNear(float WorldX, float WorldY, TArray<FMountainLakeData, TInlineAllocator<4>>& OutLakes) const;

	/** Signed, domain-warped river noise; strait centrelines are its zero lines. */
	float SampleRiverNoise(float WorldX, float WorldY, float SeedLow, float SeedHigh) const;

	/** 1 on a mountain foothill and inside the mass, 0 far from mountains. The strait cap fades with this. */
	float ComputeMountainRiverFade(float WorldX, float WorldY) const;

	/**
	 * Continentalness lowered by the river straits: below the ocean threshold across the water width, rising back
	 * across RiverShoreWidth. Never raises NaturalMacro.
	 */
	float ComputeStraitMacroCap(float WorldX, float WorldY, float SeedLow, float SeedHigh, float NaturalMacro) const;

	/**
	 * Final surface height at a world position. OutBiomeId receives the strict biome (sea, flooded shore, oasis,
	 * otherwise the nearest cell). OutVertexColor receives the two heaviest biomes and the weight of the higher ID.
	 */
	float ComputeHeight(float WorldX, float WorldY, TConstArrayView<FMountainLakeData> CandidateLakes, EBiomeType* OutBiomeId = nullptr,
		FColor* OutVertexColor = nullptr, float DetailScale = 1.0f) const;

	/** Hook for runtime terrain edits on top of the generated surface. */
	float GetTerrainDeltaHeight(float WorldX, float WorldY) const;

	FChunkInitData InitData;
	float CellSize;
	int32 QuadsPerSide;
	float ChunkWorldSize;
	TArray<FMountainLakeData> ChunkLakes;

	/** Continentalness added at the world origin; scaled by ComputeStartIslandBias elsewhere. */
	float StartIslandLift = 0.0f;
};

ATerrainChunk::ATerrainChunk()
{
	PrimaryActorTick.bCanEverTick = false;

	bReplicates = true;
	SetReplicateMovement(false);

	ProceduralMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ProceduralMesh"));
	SetRootComponent(ProceduralMesh);
	ProceduralMesh->SetMobility(EComponentMobility::Movable);
	ProceduralMesh->bUseAsyncCooking = true;
	ProceduralMesh->bUseComplexAsSimpleCollision = true;
	ProceduralMesh->SetCollisionProfileName(TEXT("BlockAll"));

	// PCG stays idle until GeneratePCG: the chunk has no surface to scatter on before its first mesh build.
	PCGComponent = CreateDefaultSubobject<UPCGComponent>(TEXT("PCGComponent"));
	PCGComponent->bActivated = false;
	PCGComponent->GenerationTrigger = EPCGComponentGenerationTrigger::GenerateOnDemand;

	// Clients pick their own LOD out to several hundred metres, so chunks must replicate far beyond the default cull distance.
	SetNetCullDistanceSquared(FMath::Square(16.0f * ChunkWorldSize));
}

void ATerrainChunk::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ATerrainChunk, InitData);
}

void ATerrainChunk::ResetChunkState()
{
	// Any build already on a worker carries the old serial and is dropped in OnBuildComplete. The callback itself
	// clears bIsGenerating; clearing it here would let a reuse start a second build beside the one still running.
	++BuildSerial;
	bRegenerationPending = false;
	bHasGeneratedMesh = false;
	bPCGGenerated = false;

	// Seed 0 is the pooled state. Replicating it forces OnRep even when the chunk is later reused at the same coordinates.
	InitData = FChunkInitData();

	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);

	if (ProceduralMesh)
	{
		ProceduralMesh->ClearAllMeshSections();
	}

	for (const TPair<uint8, TObjectPtr<UHierarchicalInstancedStaticMeshComponent>>& HISMPair : BiomeGrassHISMs)
	{
		if (UHierarchicalInstancedStaticMeshComponent* GrassHISM = HISMPair.Value.Get(); IsValid(GrassHISM))
		{
			GrassHISM->ClearInstances();
		}
	}
	bGrassBuilt = false;
	bCollisionApplied = false;

	if (IsValid(PCGComponent))
	{
		PCGComponent->CleanupLocal(/*bRemoveComponents=*/true);
	}
}

void ATerrainChunk::PlaceAtChunkCoords()
{
	const FVector ChunkLocation(
		static_cast<float>(InitData.Coords.X) * ChunkWorldSize,
		static_cast<float>(InitData.Coords.Y) * ChunkWorldSize,
		0.0f);
	SetActorLocation(ChunkLocation);
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
}

void ATerrainChunk::InitializeChunk(const FChunkInitData& InData)
{
	if (!HasAuthority())
	{
		return;
	}

	InitData = InData;
	PlaceAtChunkCoords();

	// A second increment on top of ResetChunkState. If a worker is still running, GenerateTerrainMesh only raises
	// bRegenerationPending; OnBuildComplete then starts the build for these coordinates.
	++BuildSerial;
	bPCGGenerated = false;
	bHasGeneratedMesh = false;
	ClearAllGrassInstances();
	if (IsValid(PCGComponent))
	{
		PCGComponent->CleanupLocal(/*bRemoveComponents=*/true);
	}

	CurrentLOD = ComputeTargetLOD(false);
	GenerateTerrainMesh();
}

void ATerrainChunk::BeginPlay()
{
	Super::BeginPlay();

	if (InitData.Seed != 0 && !bHasGeneratedMesh && !bIsGenerating)
	{
		CurrentLOD = ComputeTargetLOD(false);
		GenerateTerrainMesh();
	}

	// Every machine that renders (client, standalone, listen server) picks its own visual LOD.
	if (!IsNetMode(NM_DedicatedServer))
	{
		const float Interval = FMath::Max(LODUpdateInterval, 0.05f);

		// A random first delay spreads the checks of chunks spawned on the same frame across the interval.
		GetWorldTimerManager().SetTimer(LODTimerHandle, this, &ATerrainChunk::UpdateVisualLOD, Interval, true, FMath::FRandRange(0.0f, Interval));
	}
}

void ATerrainChunk::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(LODTimerHandle);

	Super::EndPlay(EndPlayReason);
}

void ATerrainChunk::OnRep_InitData()
{
	// Pooled on the server. SetActorEnableCollision does not replicate, so the client must reset locally.
	if (InitData.Seed == 0)
	{
		ResetChunkState();
		return;
	}

	PlaceAtChunkCoords();

	++BuildSerial;
	bPCGGenerated = false;
	bHasGeneratedMesh = false;
	ClearAllGrassInstances();
	if (ProceduralMesh)
	{
		ProceduralMesh->ClearAllMeshSections();
	}
	if (IsValid(PCGComponent))
	{
		PCGComponent->CleanupLocal(/*bRemoveComponents=*/true);
	}

	CurrentLOD = ComputeTargetLOD(false);
	GenerateTerrainMesh();
}

void ATerrainChunk::SetCollisionEnabled(bool bEnabled)
{
	if (!HasAuthority() || bServerCollisionEnabled == bEnabled)
	{
		return;
	}

	bServerCollisionEnabled = bEnabled;

	if (bHasGeneratedMesh || bIsGenerating)
	{
		GenerateTerrainMesh();
	}
}

bool ATerrainChunk::ShouldHaveCollision() const
{
	if (HasAuthority())
	{
		return bServerCollisionEnabled;
	}

	// Same square test as ATerrainManager: distance in chunks from the pawn to this chunk's footprint.
	// Visual LOD must not turn collision on; a forced fine mesh would otherwise cook every streamed chunk.
	const UWorld* World = GetWorld();
	const APlayerController* Controller = GEngine ? GEngine->GetFirstLocalPlayerController(World) : nullptr;
	const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!World || !Pawn)
	{
		return false;
	}

	int32 RadiusChunks = 2;
	for (TActorIterator<ATerrainManager> It(World); It; ++It)
	{
		RadiusChunks = FMath::Max(It->GetCollisionRadiusInChunks(), 0);
		break;
	}

	const float Radius = static_cast<float>(RadiusChunks);
	const float PointX = static_cast<float>(Pawn->GetActorLocation().X) / ChunkWorldSize;
	const float PointY = static_cast<float>(Pawn->GetActorLocation().Y) / ChunkWorldSize;
	const float ChunkX = static_cast<float>(InitData.Coords.X);
	const float ChunkY = static_cast<float>(InitData.Coords.Y);
	const float DX = FMath::Max3(ChunkX - PointX, 0.0f, PointX - (ChunkX + 1.0f));
	const float DY = FMath::Max3(ChunkY - PointY, 0.0f, PointY - (ChunkY + 1.0f));
	return (DX * DX + DY * DY) <= (Radius * Radius);
}

void ATerrainChunk::UpdateVisualLOD()
{
	if (InitData.Seed == 0 || IsHidden())
	{
		return;
	}

	const int32 TargetLOD = ComputeTargetLOD(true);
	const bool bCollisionChanged = !HasAuthority() && bHasGeneratedMesh && (ShouldHaveCollision() != bCollisionApplied);
	if (TargetLOD != CurrentLOD || bCollisionChanged)
	{
		CurrentLOD = TargetLOD;
		GenerateTerrainMesh();
	}
}

int32 ATerrainChunk::ComputeTargetLOD(bool bApplyHysteresis) const
{
	if (IsNetMode(NM_DedicatedServer))
	{
		return 0;
	}

	const APlayerController* LocalController = GEngine ? GEngine->GetFirstLocalPlayerController(GetWorld()) : nullptr;
	if (!LocalController || !LocalController->PlayerCameraManager)
	{
		return CurrentLOD;
	}

	const FVector CameraLocation = LocalController->PlayerCameraManager->GetCameraLocation();

	// Horizontal distance to the nearest point of the chunk square: the chunk under the camera is always at 0,
	// so a tall mountain can not push it to a coarser LOD and switch off the client's collision.
	const float ChunkSize = ChunkWorldSize;
	const FVector ChunkOrigin = GetActorLocation();
	const float MinX = static_cast<float>(ChunkOrigin.X);
	const float MinY = static_cast<float>(ChunkOrigin.Y);
	const float CameraX = static_cast<float>(CameraLocation.X);
	const float CameraY = static_cast<float>(CameraLocation.Y);
	const float DX = FMath::Max3(MinX - CameraX, 0.0f, CameraX - (MinX + ChunkSize));
	const float DY = FMath::Max3(MinY - CameraY, 0.0f, CameraY - (MinY + ChunkSize));
	const float Distance = FMath::Sqrt(DX * DX + DY * DY);

	auto LODForDistance = [this](float InDistance)
	{
		if (InDistance <= LOD1Distance)
		{
			return 0;
		}
		return InDistance <= LOD2Distance ? 1 : 2;
	};

	if (!bApplyHysteresis)
	{
		return LODForDistance(Distance);
	}

	const int32 CoarserLOD = LODForDistance(Distance - LODHysteresis);
	if (CoarserLOD > CurrentLOD)
	{
		return CoarserLOD;
	}

	const int32 FinerLOD = LODForDistance(Distance);
	return FinerLOD < CurrentLOD ? FinerLOD : CurrentLOD;
}

namespace
{
	const ATerrainManager* FindTerrainManager(const UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}

		for (TActorIterator<ATerrainManager> It(World); It; ++It)
		{
			return *It;
		}
		return nullptr;
	}
}

FTerrainGenerationSettings ATerrainChunk::ResolveGenerationSettings() const
{
	if (const ATerrainManager* Manager = FindTerrainManager(GetWorld()))
	{
		return Manager->GetGenerationSettings();
	}

	return FTerrainGenerationSettings();
}

float ATerrainChunk::GetHeightAt(float WorldX, float WorldY) const
{
	return FTerrainGenerator(ResolveGenerationSettings(), InitData, CellSize, QuadsPerSide).GetHeightAt(WorldX, WorldY);
}

FBiomeContext ATerrainChunk::GetBiomeContext(float WorldX, float WorldY, float /*SeedLow*/, float /*SeedHigh*/) const
{
	// The Voronoi layout depends only on InitData.Seed; the noise seed offsets are not needed.
	return FTerrainGenerator(ResolveGenerationSettings(), InitData, CellSize, QuadsPerSide).GetBiomeContext(WorldX, WorldY);
}

void FTerrainGenerator::GetSeedOffsets(float& OutSeedLow, float& OutSeedHigh) const
{
	// PerlinNoise2D repeats every 256 units, so integer offsets are reduced modulo 256:
	// same noise, but small offsets keep float32 precision in the fractional noise coordinate.
	const uint32 SeedBits = static_cast<uint32>(InitData.Seed);
	OutSeedLow = static_cast<float>(SeedBits % 256);
	OutSeedHigh = static_cast<float>((SeedBits >> 16) % 256);
}

namespace
{
	FBiomeWeight MakeBiomeWeight(EBiomeType Biome, float Weight)
	{
		FBiomeWeight Entry;
		Entry.Biome = Biome;
		Entry.Weight = Weight;
		return Entry;
	}

	FRingZoneSettings MakeRing(float MaxRadius, TArray<FBiomeWeight> Biomes)
	{
		FRingZoneSettings Zone;
		Zone.MaxRadius = MaxRadius;
		Zone.Biomes = MoveTemp(Biomes);
		return Zone;
	}
}

TArray<FRingZoneSettings> FRingZoneSettings::MakeDefaults()
{
	TArray<FRingZoneSettings> Zones;
	Zones.Add(MakeRing(200000.0f, { MakeBiomeWeight(EBiomeType::Plains, 1.0f) }));
	Zones.Add(MakeRing(400000.0f, { MakeBiomeWeight(EBiomeType::Forest, 1.0f), MakeBiomeWeight(EBiomeType::Plains, 0.25f) }));
	Zones.Add(MakeRing(600000.0f, { MakeBiomeWeight(EBiomeType::Mountains, 1.0f), MakeBiomeWeight(EBiomeType::Forest, 1.0f) }));
	Zones.Add(MakeRing(100000000.0f, { MakeBiomeWeight(EBiomeType::Swamp, 1.0f), MakeBiomeWeight(EBiomeType::Plateau, 1.0f) }));
	return Zones;
}

FTerrainGenerationSettings::FTerrainGenerationSettings()
{
	RingZones = FRingZoneSettings::MakeDefaults();
}

EBiomeType FTerrainGenerator::DetermineBiomeForCell(FVector2D CellCenter) const
{
	float SeedLow = 0.0f;
	float SeedHigh = 0.0f;
	GetSeedOffsets(SeedLow, SeedHigh);

	// The Voronoi site stays where the grid put it; only the biome label is measured on the warped point.
	constexpr float WarpFrequency = 0.000004f;
	const float WarpX = FMath::PerlinNoise2D(FVector2D(CellCenter.X * WarpFrequency + SeedLow, CellCenter.Y * WarpFrequency + SeedHigh)) * BiomeWarpAmplitude;
	const float WarpY = FMath::PerlinNoise2D(FVector2D(CellCenter.X * WarpFrequency + SeedHigh + 19.0f, CellCenter.Y * WarpFrequency + SeedLow + 37.0f)) * BiomeWarpAmplitude;
	const float X = static_cast<float>(CellCenter.X) + WarpX;
	const float Y = static_cast<float>(CellCenter.Y) + WarpY;
	const float Dist = static_cast<float>(FVector2D(X, Y).Size());

	if (Y > TundraLatitude)
	{
		return EBiomeType::Tundra;
	}

	if (Y < DesertLatitude)
	{
		// Shift the meridian so the jungle/desert border is not a straight line down the Y axis.
		constexpr float LongitudeFrequency = 0.00003f;
		constexpr float LongitudeAmplitude = 100000.0f;
		const float LongitudeNoise = FMath::PerlinNoise2D(FVector2D(X * LongitudeFrequency + SeedLow, Y * LongitudeFrequency + SeedHigh)) * LongitudeAmplitude;
		if (X + LongitudeNoise > JungleLongitude)
		{
			return EBiomeType::Jungle;
		}
		return EBiomeType::Desert;
	}

	int32 RingIndex = INDEX_NONE;
	float TightestRadius = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < RingZones.Num(); ++Index)
	{
		const float Radius = RingZones[Index].MaxRadius;
		if (Dist <= Radius && Radius < TightestRadius)
		{
			TightestRadius = Radius;
			RingIndex = Index;
		}
	}

	if (RingIndex == INDEX_NONE)
	{
		return EBiomeType::Plains;
	}

	const TArray<FBiomeWeight>& Biomes = RingZones[RingIndex].Biomes;
	float TotalWeight = 0.0f;
	for (const FBiomeWeight& Entry : Biomes)
	{
		if (Entry.Weight > 0.0f)
		{
			TotalWeight += Entry.Weight;
		}
	}
	if (TotalWeight <= 0.0f)
	{
		return EBiomeType::Plains;
	}

	// One low-frequency sample, mapped to [0, 1], picks a slice of the ring's total weight.
	constexpr float RingNoiseFrequency = 0.00004f;
	const float RingNoise = FMath::PerlinNoise2D(FVector2D(X * RingNoiseFrequency + SeedLow + 53.0f, Y * RingNoiseFrequency + SeedHigh + 71.0f));
	float Pick = (RingNoise + 1.0f) * 0.5f * TotalWeight;

	EBiomeType Chosen = EBiomeType::Plains;
	for (const FBiomeWeight& Entry : Biomes)
	{
		if (Entry.Weight <= 0.0f)
		{
			continue;
		}

		Chosen = Entry.Biome;
		if (Pick <= Entry.Weight)
		{
			return Entry.Biome;
		}
		Pick -= Entry.Weight;
	}

	return Chosen;
}


FVector2D FTerrainGenerator::GetBiomeSite(int32 CellX, int32 CellY) const
{
	using namespace TerrainChunkPrivate;

	// The site of cell (0, 0) sits on the world origin, so the origin always lies deep inside a plains cell rather
	// than possibly on a blended border.
	if (CellX == 0 && CellY == 0)
	{
		return FVector2D::ZeroVector;
	}

	const float BiomeCell = FMath::Max(BiomeCellSize, 1000.0f);
	const uint32 CellHash = HashCombine(HashCombine(GetTypeHash(CellX), GetTypeHash(CellY)), static_cast<uint32>(InitData.Seed) ^ 0x9E3779B9u);
	FRandomStream Stream(static_cast<int32>(CellHash));

	const float SiteX = (static_cast<float>(CellX) + Stream.FRandRange(BiomeSiteJitterMin, BiomeSiteJitterMax)) * BiomeCell;
	const float SiteY = (static_cast<float>(CellY) + Stream.FRandRange(BiomeSiteJitterMin, BiomeSiteJitterMax)) * BiomeCell;
	return FVector2D(SiteX, SiteY);
}

FVector2D FTerrainGenerator::FindNearestBiomeSite(const FVector2D& Point) const
{
	using namespace TerrainChunkPrivate;

	const float BiomeCell = FMath::Max(BiomeCellSize, 1000.0f);
	const int32 BaseCellX = FMath::FloorToInt(static_cast<float>(Point.X) / BiomeCell);
	const int32 BaseCellY = FMath::FloorToInt(static_cast<float>(Point.Y) / BiomeCell);

	FVector2D NearestSite = FVector2D::ZeroVector;
	double NearestDistSquared = TNumericLimits<double>::Max();
	for (int32 OffsetY = -BiomeScanRadius; OffsetY <= BiomeScanRadius; ++OffsetY)
	{
		for (int32 OffsetX = -BiomeScanRadius; OffsetX <= BiomeScanRadius; ++OffsetX)
		{
			const FVector2D Site = GetBiomeSite(BaseCellX + OffsetX, BaseCellY + OffsetY);
			const double DistSquared = FVector2D::DistSquared(Point, Site);
			if (DistSquared < NearestDistSquared)
			{
				NearestDistSquared = DistSquared;
				NearestSite = Site;
			}
		}
	}

	return NearestSite;
}

bool FTerrainGenerator::IsInDesertRegion(const FVector2D& Site) const
{
	using namespace TerrainChunkPrivate;

	if (DesertRegionChance <= 0.0f)
	{
		return false;
	}

	// Regions are spaced so that even the largest one stays inside its own grid cell: they never overlap, and a
	// site only needs the 3x3 grid cells around it.
	const float MaxSize = FMath::Max(DesertRegionMaxSize, 1000.0f);
	const float MinSize = FMath::Clamp(DesertRegionMinSize, 0.0f, MaxSize);
	const float Step = MaxSize * DesertRegionSpacingScale;
	const float CenterJitter = 0.5f * MaxSize / Step;
	const float BiomeCell = FMath::Max(BiomeCellSize, 1000.0f);

	const int32 BaseCellX = FMath::FloorToInt(static_cast<float>(Site.X) / Step);
	const int32 BaseCellY = FMath::FloorToInt(static_cast<float>(Site.Y) / Step);
	for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
	{
		for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
		{
			const int32 CellX = BaseCellX + OffsetX;
			const int32 CellY = BaseCellY + OffsetY;

			// The draw order is fixed, so every query sees the same region for a grid cell.
			const uint32 CellHash = HashCombine(HashCombine(GetTypeHash(CellX), GetTypeHash(CellY)), static_cast<uint32>(InitData.Seed) ^ 0x27D4EB2Fu);
			FRandomStream Stream(static_cast<int32>(CellHash));
			const FVector2D Center((static_cast<float>(CellX) + Stream.FRandRange(CenterJitter, 1.0f - CenterJitter)) * Step,
				(static_cast<float>(CellY) + Stream.FRandRange(CenterJitter, 1.0f - CenterJitter)) * Step);
			const float Radius = 0.5f * Stream.FRandRange(MinSize, MaxSize);
			const float Roll = Stream.FRand();

			// Deserts belong to the outer world, like mountains, and stay off the start island.
			const float CenterX = static_cast<float>(Center.X);
			const float CenterY = static_cast<float>(Center.Y);
			const float Remoteness = FMath::Clamp(static_cast<float>(Center.Size()) / FMath::Max(BiomeFalloffRadius, 1.0f), 0.0f, 1.0f);
			const float Chance = DesertRegionChance * FMath::SmoothStep(0.6f, 1.0f, Remoteness) * (1.0f - ComputeStartIslandBias(CenterX, CenterY));
			if (Roll >= Chance)
			{
				continue;
			}

			const float Distance = static_cast<float>(FVector2D::Distance(Site, Center));
			if (Distance <= Radius)
			{
				return true;
			}

			// A region smaller than a cell may catch no site at all, so the cell nearest to its centre always joins.
			// That site is never farther than MaxNearestSiteDistance cells from the centre.
			if (Distance <= BiomeCell * MaxNearestSiteDistance && FindNearestBiomeSite(Center) == Site)
			{
				return true;
			}
		}
	}

	return false;
}

FBiomeContext FTerrainGenerator::QueryBiomeSites(float WorldX, float WorldY, TArray<FBiomeSite, TInlineAllocator<8>>* OutBlendSites) const
{
	using namespace TerrainChunkPrivate;

	const float BiomeCell = FMath::Max(BiomeCellSize, 1000.0f);
	const int32 BaseCellX = FMath::FloorToInt(WorldX / BiomeCell);
	const int32 BaseCellY = FMath::FloorToInt(WorldY / BiomeCell);

	// Jittered sites of the 5x5 cells around the point, and the two nearest of them.
	FVector2D Sites[BiomeScanSide * BiomeScanSide];
	float SiteDistances[BiomeScanSide * BiomeScanSide];
	int32 NearestSite = 0;
	int32 SecondSite = 0;
	float Dist1 = TNumericLimits<float>::Max();
	float Dist2 = TNumericLimits<float>::Max();

	for (int32 OffsetY = -BiomeScanRadius; OffsetY <= BiomeScanRadius; ++OffsetY)
	{
		for (int32 OffsetX = -BiomeScanRadius; OffsetX <= BiomeScanRadius; ++OffsetX)
		{
			const int32 CellX = BaseCellX + OffsetX;
			const int32 CellY = BaseCellY + OffsetY;
			const int32 Index = (OffsetX + BiomeScanRadius) + (OffsetY + BiomeScanRadius) * BiomeScanSide;

			Sites[Index] = GetBiomeSite(CellX, CellY);

			const float Distance = FMath::Sqrt(FMath::Square(static_cast<float>(Sites[Index].X) - WorldX) + FMath::Square(static_cast<float>(Sites[Index].Y) - WorldY));
			SiteDistances[Index] = Distance;

			if (Distance < Dist1)
			{
				Dist2 = Dist1;
				SecondSite = NearestSite;
				Dist1 = Distance;
				NearestSite = Index;
			}
			else if (Distance < Dist2)
			{
				Dist2 = Distance;
				SecondSite = Index;
			}
		}
	}

	const float BlendWidth = GetBiomeBlendWidth();

	FBiomeContext Context;
	Context.PrimaryBiome = DetermineBiomeForCell(Sites[NearestSite]);
	Context.SecondaryBiome = DetermineBiomeForCell(Sites[SecondSite]);
	Context.DistanceToEdge = Dist2 - Dist1;
	Context.EdgeBlendWeight = FMath::SmoothStep(0.0f, BlendWidth, Context.DistanceToEdge);

	if (OutBlendSites)
	{
		OutBlendSites->Reset();
		OutBlendSites->Add(FBiomeSite{ Context.PrimaryBiome, Dist1 });

		const float BlendLimit = Dist1 + BlendWidth;
		for (int32 Index = 0; Index < BiomeScanSide * BiomeScanSide; ++Index)
		{
			if (Index == NearestSite || SiteDistances[Index] >= BlendLimit)
			{
				continue;
			}

			const EBiomeType Biome = (Index == SecondSite) ? Context.SecondaryBiome : DetermineBiomeForCell(Sites[Index]);
			OutBlendSites->Add(FBiomeSite{ Biome, SiteDistances[Index] });
		}
	}

	return Context;
}

FBiomeContext FTerrainGenerator::GetBiomeContext(float WorldX, float WorldY) const
{
	return QueryBiomeSites(WorldX, WorldY, nullptr);
}

float FTerrainGenerator::GetBiomeBlendWidth() const
{
	using namespace TerrainChunkPrivate;

	return FMath::Max(BiomeCellSize, 1000.0f) * BiomeBlendWidthScale;
}

float FTerrainGenerator::GetBiomeInterior(float DistanceToEdge) const
{
	using namespace TerrainChunkPrivate;

	const float InteriorWidth = FMath::Max(BiomeCellSize, 1000.0f) * BiomeInteriorWidthScale;
	return FMath::Clamp(DistanceToEdge / InteriorWidth, 0.0f, 1.0f);
}

float FTerrainGenerator::ComputeBiomeShare(float WorldX, float WorldY, EBiomeType Biome) const
{
	TArray<FBiomeSite, TInlineAllocator<8>> BlendSites;
	QueryBiomeSites(WorldX, WorldY, &BlendSites);

	const float BlendWidth = GetBiomeBlendWidth();
	const float Dist1 = BlendSites[0].Distance;
	float BiomeWeight = 0.0f;
	float WeightSum = 0.0f;
	for (const FBiomeSite& Site : BlendSites)
	{
		const float Weight = 1.0f - FMath::SmoothStep(0.0f, BlendWidth, Site.Distance - Dist1);
		WeightSum += Weight;
		if (Site.Biome == Biome)
		{
			BiomeWeight += Weight;
		}
	}

	return WeightSum > 0.0f ? BiomeWeight / WeightSum : 0.0f;
}

void FTerrainGenerator::GetZoneLimits(float& OutOceanLimit, float& OutMountainLimit) const
{
	OutOceanLimit = FMath::Clamp(OceanThreshold, 0.01f, 0.98f);
	OutMountainLimit = FMath::Clamp(MountainThreshold, OutOceanLimit + 0.01f, 0.99f);
}

float FTerrainGenerator::SampleContinentNoise(float WorldX, float WorldY, float SeedLow, float SeedHigh) const
{
	// Continent shapes.
	const FVector2D ContinentUV(WorldX * MacroFrequency + SeedLow, WorldY * MacroFrequency + SeedHigh);
	const float Continents = FMath::PerlinNoise2D(ContinentUV);

	// Large gulfs and peninsulas. The domain is turned 35 degrees so octave lattices do not line up.
	constexpr float GulfCos = 0.81915f;
	constexpr float GulfSin = 0.57358f;
	const float GulfFrequency = MacroFrequency * 2.3f;
	const float GulfX = (WorldX * GulfCos - WorldY * GulfSin) * GulfFrequency + SeedHigh + 41.0f;
	const float GulfY = (WorldX * GulfSin + WorldY * GulfCos) * GulfFrequency + SeedLow + 67.0f;
	const float Gulfs = FMath::PerlinNoise2D(FVector2D(GulfX, GulfY));

	// Capes and coves, ~160 m wavelength. Full strength at the coast, fading out inland so they do not ripple
	// island interiors.
	const float CoveFrequency = MacroFrequency * 5.1f;
	const FVector2D CoveUV(WorldX * CoveFrequency + SeedLow + 89.0f, WorldY * CoveFrequency + SeedHigh + 127.0f);
	const float Coves = FMath::PerlinNoise2D(CoveUV);
	const float CoveWeight = 0.08f * (1.0f - FMath::SmoothStep(0.0f, 0.45f, Continents));

	// Summed octaves rarely reach the single-octave extremes, so the sum is stretched back before the
	// remap: deep ocean and the highest peaks stay reachable.
	constexpr float OctaveRangeCompensation = 0.82f;
	const float Combined = (Continents * 0.72f + Gulfs * 0.20f + Coves * CoveWeight) / OctaveRangeCompensation;
	return FMath::Clamp((Combined + 1.0f) * 0.5f, 0.0f, 1.0f);
}

float FTerrainGenerator::ComputeStartIslandBias(float WorldX, float WorldY) const
{
	const float DistFromCenter = FMath::Sqrt(WorldX * WorldX + WorldY * WorldY);
	const float Proximity = FMath::Clamp(1.0f - DistFromCenter / FMath::Max(StartIslandRadius, 1.0f), 0.0f, 1.0f);
	return FMath::SmoothStep(0.0f, 1.0f, Proximity);
}

float FTerrainGenerator::SampleContinentalness(float WorldX, float WorldY, float SeedLow, float SeedHigh) const
{
	// Start island: the continent noise is lifted towards the world origin rather than clamped to a floor, so the
	// coastline still follows the noise and the island gets bays and capes instead of a circular shore.
	const float IslandMacro = FMath::Clamp(SampleContinentNoise(WorldX, WorldY, SeedLow, SeedHigh) + StartIslandLift * ComputeStartIslandBias(WorldX, WorldY), 0.0f, 1.0f);

	// River straits are part of the sea: they lower the mask itself, so every biome meets them with the regular
	// beach ramp, and mountain mass and lake placement see them too.
	return ComputeStraitMacroCap(WorldX, WorldY, SeedLow, SeedHigh, IslandMacro);
}

float FTerrainGenerator::ComputeLandRamp(float MacroSample) const
{
	float OceanLimit = 0.0f;
	float MountainLimit = 0.0f;
	GetZoneLimits(OceanLimit, MountainLimit);

	// Flat at both ends: a gentle beach at the waterline and no crease where biomes reach full height.
	return FMath::SmoothStep(OceanLimit, MountainLimit, MacroSample);
}

float FTerrainGenerator::SampleHills(float WorldX, float WorldY, float Frequency, float SeedLow, float SeedHigh) const
{
	if (Frequency <= 0.0f)
	{
		return 0.0f;
	}

	// Broad hill swells.
	const FVector2D HillsUV(WorldX * Frequency + SeedLow, WorldY * Frequency + SeedHigh);
	const float Swells = FMath::PerlinNoise2D(HillsUV);

	// Medium ridges, turned 60 degrees with their own seed shift.
	constexpr float RidgeCos = 0.5f;
	constexpr float RidgeSin = 0.86603f;
	const float RidgeFreq = Frequency * 2.15f;
	const float RidgeX = (WorldX * RidgeCos - WorldY * RidgeSin) * RidgeFreq + SeedHigh + 73.0f;
	const float RidgeY = (WorldX * RidgeSin + WorldY * RidgeCos) * RidgeFreq + SeedLow + 101.0f;
	const float Ridges = FMath::PerlinNoise2D(FVector2D(RidgeX, RidgeY));

	// Stretched back to the single-octave range so the amplitude setting keeps its meaning.
	constexpr float OctaveRangeCompensation = 0.85f;
	return FMath::Clamp((Swells * 0.70f + Ridges * 0.30f) / OctaveRangeCompensation, -1.0f, 1.0f);
}

float FTerrainGenerator::SampleBiomeDetail(float WorldX, float WorldY, EBiomeType Biome, float DistanceToEdge, float SeedLow, float SeedHigh) const
{
	float Frequency = 0.0f;
	float Amplitude = 0.0f;
	switch (Biome)
	{
	case EBiomeType::Plains:
		Frequency = PlainsDetailFrequency;
		Amplitude = PlainsDetailAmplitude;
		break;
	case EBiomeType::Forest:
		Frequency = ForestDetailFrequency;
		Amplitude = ForestDetailAmplitude;
		break;
	case EBiomeType::Plateau:
		Frequency = PlateauDetailFrequency;
		Amplitude = PlateauDetailAmplitude;
		break;
	case EBiomeType::Mountains:
		Frequency = MountainsDetailFrequency;
		Amplitude = MountainsDetailAmplitude;
		break;
	case EBiomeType::Desert:
		Frequency = DesertDetailFrequency;
		Amplitude = DesertDetailAmplitude;
		break;
	case EBiomeType::Tundra:
		Frequency = TundraDetailFrequency;
		Amplitude = TundraDetailAmplitude;
		break;
	case EBiomeType::Jungle:
		Frequency = JungleDetailFrequency;
		Amplitude = JungleDetailAmplitude;
		break;
	case EBiomeType::Foothills:
		Frequency = FoothillsDetailFrequency;
		Amplitude = FoothillsDetailAmplitude;
		break;
	default:
		// Ocean is sampled separately; swamp and oasis stay flat so puddles and ponds keep a clean shore.
		return 0.0f;
	}

	if (Frequency <= 0.0f || Amplitude <= 0.0f)
	{
		return 0.0f;
	}

	// On a plateau the fine noise stays on the mesa and the canyon floor. The wall mask fades it to nothing
	// where the wall is steepest, so it cannot sharpen the 1 m grid into spikes.
	if (Biome == EBiomeType::Plateau && CanyonFrequency > 0.0f)
	{
		using namespace TerrainChunkPrivate;

		const float Escarpment = FMath::SmoothStep(PlateauEscarpmentStartCm, PlateauEscarpmentEndCm, DistanceToEdge);
		if (Escarpment > 0.0f)
		{
			const float WallSteepness = SampleCanyonWall(WorldX, WorldY, SeedLow, SeedHigh).Steepness;
			Amplitude *= 1.0f - WallSteepness * Escarpment;
		}
	}

	const FVector2D DetailUV(WorldX * Frequency + SeedHigh, WorldY * Frequency + SeedLow);
	return FMath::PerlinNoise2D(DetailUV) * Amplitude;
}

float FTerrainGenerator::ComputePlains(float WorldX, float WorldY, float Interior, float SeedLow, float SeedHigh) const
{
	using namespace TerrainChunkPrivate;

	const float SmoothInterior = FMath::SmoothStep(0.0f, 1.0f, Interior);
	const float HillsNoise = SampleHills(WorldX, WorldY, PlainsHillsFrequency, SeedLow, SeedHigh);
	return LandEdgeZ + SmoothInterior * (PlainsRise + HillsNoise * PlainsHillsAmplitude);
}

float FTerrainGenerator::ComputeTundra(float WorldX, float WorldY, float Interior, float SeedLow, float SeedHigh) const
{
	using namespace TerrainChunkPrivate;

	const float SmoothInterior = FMath::SmoothStep(0.0f, 1.0f, Interior);
	const float HillsNoise = SampleHills(WorldX, WorldY, TundraHillsFrequency, SeedLow, SeedHigh);
	return LandEdgeZ + SmoothInterior * (PlainsRise + HillsNoise * TundraHillsAmplitude);
}

float FTerrainGenerator::ComputeForest(float WorldX, float WorldY, float Interior, float SeedLow, float SeedHigh) const
{
	using namespace TerrainChunkPrivate;

	// Neighbouring cells are evaluated at interior 0, where the forest sits at the common edge height.
	const float SmoothInterior = FMath::SmoothStep(0.0f, 1.0f, Interior);
	if (SmoothInterior <= 0.0f)
	{
		return LandEdgeZ;
	}

	// Forest hills of their own, independent of the map-wide hills: a main octave plus a finer one turned
	// 50 degrees, so the hills do not line up along the noise lattice.
	const FVector2D MainUV(WorldX * ForestHillsFrequency + SeedHigh + 233.0f, WorldY * ForestHillsFrequency + SeedLow + 263.0f);
	const float MainHills = FMath::PerlinNoise2D(MainUV);

	constexpr float FineCos = 0.64279f;
	constexpr float FineSin = 0.76604f;
	const float FineFrequency = ForestHillsFrequency * 2.1f;
	const float FineX = (WorldX * FineCos - WorldY * FineSin) * FineFrequency + SeedLow + 307.0f;
	const float FineY = (WorldX * FineSin + WorldY * FineCos) * FineFrequency + SeedHigh + 331.0f;
	const float FineHills = FMath::PerlinNoise2D(FVector2D(FineX, FineY));

	// Stretched back to the single-octave range, then mapped to [0, 1] so ForestHillsHeight is the full
	// difference between hollows and hill tops. Hills only rise above the coast height.
	constexpr float OctaveRangeCompensation = 0.8f;
	const float Hills = FMath::Clamp((MainHills * 0.75f + FineHills * 0.25f) / OctaveRangeCompensation, -1.0f, 1.0f);
	const float Hills01 = Hills * 0.5f + 0.5f;

	return LandEdgeZ + SmoothInterior * (ForestRise + Hills01 * ForestHillsHeight);
}

float FTerrainGenerator::ComputeJungle(float WorldX, float WorldY, float Interior, float SeedLow, float SeedHigh) const
{
	using namespace TerrainChunkPrivate;

	const float SmoothInterior = FMath::SmoothStep(0.0f, 1.0f, Interior);
	if (SmoothInterior <= 0.0f)
	{
		return LandEdgeZ;
	}

	const FVector2D MainUV(WorldX * JungleHillsFrequency + SeedHigh + 233.0f, WorldY * JungleHillsFrequency + SeedLow + 263.0f);
	const float MainHills = FMath::PerlinNoise2D(MainUV);

	constexpr float FineCos = 0.64279f;
	constexpr float FineSin = 0.76604f;
	const float FineFrequency = JungleHillsFrequency * 2.1f;
	const float FineX = (WorldX * FineCos - WorldY * FineSin) * FineFrequency + SeedLow + 307.0f;
	const float FineY = (WorldX * FineSin + WorldY * FineCos) * FineFrequency + SeedHigh + 331.0f;
	const float FineHills = FMath::PerlinNoise2D(FVector2D(FineX, FineY));

	constexpr float OctaveRangeCompensation = 0.8f;
	const float Hills = FMath::Clamp((MainHills * 0.75f + FineHills * 0.25f) / OctaveRangeCompensation, -1.0f, 1.0f);
	const float Hills01 = Hills * 0.5f + 0.5f;

	return LandEdgeZ + SmoothInterior * (ForestRise + Hills01 * JungleHillsHeight);
}

float FTerrainGenerator::ComputeSwamp(float WorldX, float WorldY, float SeedLow, float SeedHigh) const
{
	// A flat lowland just above the sea, the same across the whole cell: the slope from the neighbours comes from
	// the narrow swamp blend in ComputeHeight. Puddle noise pushes parts of it below Z = 0, where the sea water
	// fills them.
	const FVector2D PuddleUV(WorldX * SwampPuddleNoiseFrequency + SeedHigh + 571.0f, WorldY * SwampPuddleNoiseFrequency + SeedLow + 599.0f);
	return SwampBaseHeight + FMath::PerlinNoise2D(PuddleUV) * SwampPuddleDepth;
}

float FTerrainGenerator::ComputeDesertDunes(float WorldX, float WorldY, float SeedLow, float SeedHigh) const
{
	using namespace TerrainChunkPrivate;

	// Ridged noise: |n| is 0 along long winding lines, so 1 - |n| forms sharp crests there with gentle flanks
	// between them. The power widens the hollows and sharpens the crests further.
	const FVector2D DuneUV(WorldX * DesertDuneFrequency + SeedLow + 613.0f, WorldY * DesertDuneFrequency + SeedHigh + 641.0f);
	const float DuneNoise = FMath::Clamp(1.0f - FMath::Abs(FMath::PerlinNoise2D(DuneUV)), 0.0f, 1.0f);
	return FMath::Pow(DuneNoise, DesertDuneSharpness) * DesertDuneAmplitude;
}

float FTerrainGenerator::ComputeDesert(float WorldX, float WorldY, float Interior, float SeedLow, float SeedHigh) const
{
	// Dunes settle towards the cell border, so the desert meets its neighbours on its flat base rather than with
	// 25 m sand walls.
	const float SmoothInterior = FMath::SmoothStep(0.0f, 1.0f, Interior);
	if (SmoothInterior <= 0.0f)
	{
		return DesertBaseHeight;
	}

	return DesertBaseHeight + SmoothInterior * ComputeDesertDunes(WorldX, WorldY, SeedLow, SeedHigh);
}

float FTerrainGenerator::GetOasisRadius() const
{
	return FMath::Clamp(OasisRadius, 0.0f, FMath::Max(OasisGridSize, 1000.0f));
}

bool FTerrainGenerator::IsOasisSiteValid(const FVector2D& Center, float SeedLow, float SeedHigh) const
{
	const float CenterX = static_cast<float>(Center.X);
	const float CenterY = static_cast<float>(Center.Y);

	// DistanceToEdge changes at most twice as fast as the point moves, so with this margin at the centre every
	// neighbouring cell keeps zero blend weight across the whole oasis: the oasis sees pure desert ground.
	const FBiomeContext CenterContext = QueryBiomeSites(CenterX, CenterY, nullptr);
	if (CenterContext.PrimaryBiome != EBiomeType::Desert || CenterContext.DistanceToEdge < GetBiomeBlendWidth() + 2.0f * GetOasisRadius())
	{
		return false;
	}

	// Only in the hollows between the dunes.
	const float DuneHeight = ComputeDesert(CenterX, CenterY, GetBiomeInterior(CenterContext.DistanceToEdge), SeedLow, SeedHigh) - DesertBaseHeight;
	return DuneHeight <= OasisMaxDuneHeight;
}

bool FTerrainGenerator::FindOasis(float WorldX, float WorldY, float SeedLow, float SeedHigh, float& OutDistance) const
{
	const float Radius = GetOasisRadius();
	if (OasisSpawnChance <= 0.0f || Radius <= 0.0f)
	{
		return false;
	}

	// The radius never exceeds the grid size, so an oasis covering the point has its centre in the 3x3 cells
	// around it.
	const float Grid = FMath::Max(OasisGridSize, 1000.0f);
	const int32 BaseCellX = FMath::FloorToInt(WorldX / Grid);
	const int32 BaseCellY = FMath::FloorToInt(WorldY / Grid);
	const FVector2D Point(WorldX, WorldY);

	// The nearest oasis wins where two overlap. Its ground depends only on the distance, so both give the same
	// height where the distances are equal and the switch leaves no seam.
	bool bFound = false;
	OutDistance = Radius;
	for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
	{
		for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
		{
			const int32 CellX = BaseCellX + OffsetX;
			const int32 CellY = BaseCellY + OffsetY;

			// The draw order is fixed and independent of the query point, so every sample sees the same oasis.
			const uint32 CellHash = HashCombine(HashCombine(GetTypeHash(CellX), GetTypeHash(CellY)), static_cast<uint32>(InitData.Seed) ^ 0x5BD1E995u);
			FRandomStream Stream(static_cast<int32>(CellHash));
			const FVector2D Center((static_cast<float>(CellX) + Stream.FRand()) * Grid, (static_cast<float>(CellY) + Stream.FRand()) * Grid);
			if (Stream.FRand() > OasisSpawnChance)
			{
				continue;
			}

			const float Distance = static_cast<float>(FVector2D::Distance(Point, Center));
			if (Distance >= OutDistance || !IsOasisSiteValid(Center, SeedLow, SeedHigh))
			{
				continue;
			}

			OutDistance = Distance;
			bFound = true;
		}
	}

	return bFound;
}

float FTerrainGenerator::ComputeOasisZ(float Distance, float DesertZ) const
{
	using namespace TerrainChunkPrivate;

	const float Radius = GetOasisRadius();
	const float WaterRadius = FMath::Clamp(OasisWaterRadius, 0.0f, Radius * 0.8f);
	const float BankEnd = WaterRadius + (Radius - WaterRadius) * OasisBankShare;

	if (Distance < WaterRadius)
	{
		// Pond: a bowl below sea level, so the global sea water fills it up to the rim at WaterRadius.
		const float Alpha = Distance / WaterRadius;
		return -OasisWaterDepth * (1.0f - Alpha * Alpha);
	}

	if (Distance < BankEnd)
	{
		// Bank from the waterline up to the flat shore. Steepest at the water, so the shoreline is a clean
		// contour rather than a flat ring at Z = 0.
		const float Alpha = (Distance - WaterRadius) / FMath::Max(BankEnd - WaterRadius, KINDA_SMALL_NUMBER);
		return DesertBaseHeight * (1.0f - FMath::Square(1.0f - Alpha));
	}

	// Shore: the dunes level out to the flat desert base towards the bank.
	return FMath::Lerp(DesertBaseHeight, DesertZ, FMath::SmoothStep(BankEnd, Radius, Distance));
}

float FTerrainGenerator::ComputeMountains(float WorldX, float WorldY, float MacroSample, float Interior, float SeedLow, float SeedHigh, float DetailScale) const
{
	using namespace TerrainChunkPrivate;

	float OceanLimit = 0.0f;
	float MountainLimit = 0.0f;
	GetZoneLimits(OceanLimit, MountainLimit);

	// Massifs grow with continentalness, so the highest ranges stand in island interiors, and sink to the base
	// towards the cell border, so a mountain cell never meets its neighbours with a wall.
	const float MacroAlpha = FMath::Clamp((MacroSample - OceanLimit) / FMath::Max(EffectiveMaxMacro - OceanLimit, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
	const float MountainAlpha = MacroAlpha * FMath::Clamp(Interior, 0.0f, 1.0f);
	const float HillsNoise = SampleHills(WorldX, WorldY, MountainsBaseHillsFrequency, SeedLow, SeedHigh);

	// Anisotropic spine: the ridge domain is rotated 45 degrees and stretched along one axis,
	// so ridged noise forms long ranges instead of isolated cones.
	constexpr float Diagonal = 0.707f;
	const float StretchedX = (WorldX * Diagonal - WorldY * Diagonal) * RidgeFrequency + SeedHigh;
	const float StretchedY = (WorldX * Diagonal + WorldY * Diagonal) * (RidgeFrequency * 0.45f) + SeedLow;
	const float RawRidge = 1.0f - FMath::Abs(FMath::PerlinNoise2D(FVector2D(StretchedX, StretchedY)));
	const float SharpSpine = FMath::Pow(RawRidge, 1.7f);

	// Secondary rock outcrops and spurs.
	const FVector2D BranchUV(WorldX * RidgeFrequency * 2.2f + SeedLow, WorldY * RidgeFrequency * 2.2f + SeedHigh);
	const float RawBranch = 1.0f - FMath::Abs(FMath::PerlinNoise2D(BranchUV));
	const float SharpBranch = RawBranch * RawBranch;

	// Crags: fine ridged relief that breaks up the mid slopes.
	const float CragFrequency = RidgeFrequency * 4.7f;
	const FVector2D CragUV(WorldX * CragFrequency + SeedHigh + 151.0f, WorldY * CragFrequency + SeedLow + 179.0f);
	const float RawCrag = 1.0f - FMath::Abs(FMath::PerlinNoise2D(CragUV));
	const float SharpCrag = RawCrag * RawCrag;

	// The ridge skeleton is additive rather than purely multiplicative, so the massif stretches into
	// a narrow wall-like range instead of inflating into a dome. It dominates at the peak and only
	// outlines the footprint near the base. Branch and crag are the fine harmonics; DetailScale damps
	// them on coarse LODs. The spine itself stays full so the range keeps its height.
	const float BaseMass = FMath::Pow(MountainAlpha, 1.2f);
	const float FineHarmonics = (SharpBranch * 0.26f + SharpCrag * 0.12f) * DetailScale;
	const float SpineInfluence = SharpSpine * 0.62f + FineHarmonics;
	const float CombinedFactor = FMath::Clamp(BaseMass * 0.30f + (SpineInfluence * MountainAlpha * 0.70f), 0.0f, 1.0f);

	// At the cell border the base matches the other biomes with plains-sized hills, which fade out uphill.
	const float MountainBase = LandEdgeZ + (HillsNoise * MountainsBaseHillsAmplitude * (1.0f - MountainAlpha));
	return MountainBase + CombinedFactor * (MountainMaxHeight - LandEdgeZ);
}

FTerrainGenerator::FCanyonWallSample FTerrainGenerator::SampleCanyonWall(float WorldX, float WorldY, float SeedLow, float SeedHigh) const
{
	using namespace TerrainChunkPrivate;

	FCanyonWallSample Wall;
	if (CanyonFrequency <= 0.0f || CanyonWidth <= 0.0f)
	{
		return Wall;
	}

	// Domain warping bends the canyon tracts into meanders (up to 45 m sideways).
	constexpr float CanyonWarpFreq = 0.000035f;
	constexpr float CanyonWarpAmp = 4500.0f;
	const float WarpedX = WorldX + FMath::PerlinNoise2D(FVector2D(WorldX * CanyonWarpFreq + SeedLow, WorldY * CanyonWarpFreq)) * CanyonWarpAmp;
	const float WarpedY = WorldY + FMath::PerlinNoise2D(FVector2D(WorldX * CanyonWarpFreq, WorldY * CanyonWarpFreq + SeedHigh)) * CanyonWarpAmp;

	constexpr float CanyonOffset = 47.0f;
	const FVector2D CanyonUV(WarpedX * CanyonFrequency + SeedLow + CanyonOffset, WarpedY * CanyonFrequency + SeedHigh + CanyonOffset);
	const float RawCanyon = FMath::Abs(FMath::PerlinNoise2D(CanyonUV));

	// Higher-frequency noise erodes the rim so the wall line never runs straight.
	constexpr float RimNoiseFreq = 0.00020f;
	constexpr float RimNoiseAmp = 0.035f;
	const float RimNoise = FMath::PerlinNoise2D(FVector2D(WorldX * RimNoiseFreq + SeedHigh, WorldY * RimNoiseFreq + SeedLow)) * RimNoiseAmp;
	const float EffectiveCanyonDist = RawCanyon + RimNoise;

	// Flat floor inside CanyonFloorWidth, one smooth wall up to CanyonWidth, untouched mesa beyond.
	const float FloorWidth = FMath::Min(CanyonFloorWidth, CanyonWidth - KINDA_SMALL_NUMBER);
	const float Band = FMath::Max(CanyonWidth - FloorWidth, KINDA_SMALL_NUMBER);
	const float WallT = FMath::Clamp((EffectiveCanyonDist - FloorWidth) / Band, 0.0f, 1.0f);
	Wall.Slope = FMath::SmoothStep(0.0f, 1.0f, WallT);
	Wall.Steepness = FMath::Clamp(4.0f * WallT * (1.0f - WallT), 0.0f, 1.0f);
	return Wall;
}

float FTerrainGenerator::ComputePlateau(float WorldX, float WorldY, float DistanceToEdge, float SeedLow, float SeedHigh) const
{
	using namespace TerrainChunkPrivate;

	// Mesa: the rim rises in from the cell border, then the table top is cut by canyons.
	const float Escarpment = FMath::SmoothStep(PlateauEscarpmentStartCm, PlateauEscarpmentEndCm, DistanceToEdge);
	const float HillsNoise = SampleHills(WorldX, WorldY, PlateauHillsFrequency, SeedLow, SeedHigh);

	float CarvedPlateauHeight = PlateauHeight;

	// 1 on a canyon floor, 0 on the untouched mesa top.
	float CanyonCarve = 0.0f;

	if (Escarpment > 0.0f)
	{
		const FCanyonWallSample Canyon = SampleCanyonWall(WorldX, WorldY, SeedLow, SeedHigh);

		// The canyon floor is cut 88% of the way down to the lowland base. The wall is the smooth mask only:
		// terrace steps on the 1 m grid turn into a saw.
		constexpr float CanyonCutDepth = 0.88f;
		CanyonCarve = 1.0f - Canyon.Slope;
		CarvedPlateauHeight = PlateauHeight * (1.0f - CanyonCarve * CanyonCutDepth);
	}

	// Hills are flattened by 85% on the table top, easing to 65% on canyon floors so they keep the gentle
	// relief of a dry riverbed.
	constexpr float PlateauTopHillsCut = 0.85f;
	constexpr float CanyonFloorHillsCut = 0.65f;
	const float PlateauHillsDamping = 1.0f - (Escarpment * FMath::Lerp(PlateauTopHillsCut, CanyonFloorHillsCut, CanyonCarve));
	const float PlateauHills = HillsNoise * PlateauHillsAmplitude * PlateauHillsDamping;

	return LandEdgeZ + PlateauHills + Escarpment * CarvedPlateauHeight;
}

float FTerrainGenerator::ComputeBiomeHeight(float WorldX, float WorldY, EBiomeType Biome, float MacroSample, float Interior, float DistanceToEdge,
	float SeedLow, float SeedHigh, float DetailScale) const
{
	using namespace TerrainChunkPrivate;

	switch (Biome)
	{
	case EBiomeType::Plains:
		return ComputePlains(WorldX, WorldY, Interior, SeedLow, SeedHigh);
	case EBiomeType::Forest:
		return ComputeForest(WorldX, WorldY, Interior, SeedLow, SeedHigh);
	case EBiomeType::Swamp:
		return ComputeSwamp(WorldX, WorldY, SeedLow, SeedHigh);
	case EBiomeType::Plateau:
		return ComputePlateau(WorldX, WorldY, DistanceToEdge, SeedLow, SeedHigh);
	case EBiomeType::Mountains:
		return ComputeMountains(WorldX, WorldY, MacroSample, Interior, SeedLow, SeedHigh, DetailScale);
	case EBiomeType::Desert:
		return ComputeDesert(WorldX, WorldY, Interior, SeedLow, SeedHigh);
	case EBiomeType::Tundra:
		return ComputeTundra(WorldX, WorldY, Interior, SeedLow, SeedHigh);
	case EBiomeType::Jungle:
		return ComputeJungle(WorldX, WorldY, Interior, SeedLow, SeedHigh);
	case EBiomeType::Foothills:
		return ComputeMountains(WorldX, WorldY, MacroSample, Interior, SeedLow, SeedHigh, DetailScale);
	default:
		return LandEdgeZ;
	}
}

bool FTerrainGenerator::MakeLakeCandidate(int32 CellX, int32 CellY, FMountainLakeData& OutLake) const
{
	using namespace TerrainChunkPrivate;

	const uint32 CellHash = HashCombine(HashCombine(GetTypeHash(CellX), GetTypeHash(CellY)), static_cast<uint32>(InitData.Seed));
	FRandomStream Stream(static_cast<int32>(CellHash));

	// The draw order is fixed and independent of the query point, so every sample sees the same lake for a cell.
	if (Stream.FRand() > LakeChance)
	{
		return false;
	}

	const float CenterX = (static_cast<float>(CellX) + Stream.FRandRange(0.25f, 0.75f)) * LakeCellSize;
	const float CenterY = (static_cast<float>(CellY) + Stream.FRandRange(0.25f, 0.75f)) * LakeCellSize;

	const bool bIsCaldera = Stream.FRand() >= 0.5f;
	const float Radius = bIsCaldera
		? Stream.FRandRange(5500.0f, LakeMaxRadius)
		: Stream.FRandRange(2800.0f, 4200.0f);
	const bool bCliffShore = Stream.FRand() >= 0.5f;

	OutLake = FMountainLakeData();
	OutLake.Center = FVector2D(CenterX, CenterY);
	OutLake.Radius = Radius;
	OutLake.BedDepth = LakeBedDepth;
	OutLake.bCliffShore = bCliffShore;
	OutLake.LakeType = (Stream.FRand() >= 0.5f) ? EMountainLakeType::TectonicRift : EMountainLakeType::GlacialCirque;
	OutLake.AspectRatio = Stream.FRandRange(2.0f, 2.6f);
	OutLake.CirqueAngle = Stream.FRandRange(0.0f, 2.0f * PI);
	return true;
}

bool FTerrainGenerator::ValidateLake(FMountainLakeData& Lake, float SeedLow, float SeedHigh) const
{
	using namespace TerrainChunkPrivate;

	const float CenterX = static_cast<float>(Lake.Center.X);
	const float CenterY = static_cast<float>(Lake.Center.Y);

	// Lakes appear only on land, well inside a mountain cell, where no neighbouring biome blends in.
	const float MacroAtCenter = SampleContinentalness(CenterX, CenterY, SeedLow, SeedHigh);
	const float RampAtCenter = ComputeLandRamp(MacroAtCenter);
	if (RampAtCenter <= 0.0f)
	{
		return false;
	}

	const FBiomeContext CenterContext = QueryBiomeSites(CenterX, CenterY, nullptr);
	const float CenterInterior = GetBiomeInterior(CenterContext.DistanceToEdge);
	if (CenterContext.PrimaryBiome != EBiomeType::Mountains || CenterInterior < LakeMinMountainInterior)
	{
		return false;
	}

	// A lake on a narrow ridge or a steep flank would spill over its low side and hang in the air.
	// Samples follow the actual basin shape, just beyond the unwarped shore line.
	float MinRimZ = TNumericLimits<float>::Max();
	for (int32 AngleIndex = 0; AngleIndex < LakeRimSampleCount; ++AngleIndex)
	{
		const float Angle = static_cast<float>(AngleIndex) * (2.0f * PI / static_cast<float>(LakeRimSampleCount));
		const float DirX = FMath::Cos(Angle);
		const float DirY = FMath::Sin(Angle);
		const float ShoreDistance = Lake.Radius / ComputeLakeShapeScale(Lake, DirX, DirY);
		const float RimSampleDistance = ShoreDistance + LakeRimSampleOffset;
		const float SampleX = CenterX + DirX * RimSampleDistance;
		const float SampleY = CenterY + DirY * RimSampleDistance;

		const FBiomeContext SampleContext = QueryBiomeSites(SampleX, SampleY, nullptr);
		if (SampleContext.PrimaryBiome != EBiomeType::Mountains)
		{
			return false;
		}

		// The beach ramp scales the surface towards the coast, so it is part of the rim height.
		const float MacroAtSample = SampleContinentalness(SampleX, SampleY, SeedLow, SeedHigh);
		const float MountainZAtSample = ComputeMountains(SampleX, SampleY, MacroAtSample, GetBiomeInterior(SampleContext.DistanceToEdge),
			SeedLow, SeedHigh) * ComputeLandRamp(MacroAtSample);
		MinRimZ = FMath::Min(MinRimZ, MountainZAtSample);
	}

	if (MinRimZ <= LandEdgeZ + LakeMinRimAboveLand)
	{
		return false;
	}

	// The water level follows the actual mountain surface at the centre, so the lake sits in a shallow saddle.
	const float BaseMountCenter = ComputeMountains(CenterX, CenterY, MacroAtCenter, CenterInterior, SeedLow, SeedHigh) * RampAtCenter;

	Lake.bExists = true;
	Lake.WaterZ = BaseMountCenter - LakeWaterBelowCenter;
	return true;
}

void FTerrainGenerator::GetMountainLakesNear(float WorldX, float WorldY, TArray<FMountainLakeData, TInlineAllocator<4>>& OutLakes) const
{
	using namespace TerrainChunkPrivate;

	OutLakes.Reset();

	float SeedLow = 0.0f;
	float SeedHigh = 0.0f;
	GetSeedOffsets(SeedLow, SeedHigh);

	// Lakes are blended by the mountain weight on land, so most of the map skips the cell scan.
	float OceanLimit = 0.0f;
	float MountainLimit = 0.0f;
	GetZoneLimits(OceanLimit, MountainLimit);
	if (SampleContinentalness(WorldX, WorldY, SeedLow, SeedHigh) < OceanLimit)
	{
		return;
	}

	TArray<FBiomeSite, TInlineAllocator<8>> BlendSites;
	QueryBiomeSites(WorldX, WorldY, &BlendSites);
	const bool bNearMountains = BlendSites.ContainsByPredicate([](const FBiomeSite& Site)
	{
		return Site.Biome == EBiomeType::Mountains;
	});
	if (!bNearMountains)
	{
		return;
	}

	const int32 BaseCellX = FMath::FloorToInt(WorldX / LakeCellSize);
	const int32 BaseCellY = FMath::FloorToInt(WorldY / LakeCellSize);

	// Cells are visited in global (CellY, CellX) order whatever the query point, so callers that fold the
	// lakes in sequence get the same composition everywhere.
	for (int32 OffsetY = -LakeCellScanRadius; OffsetY <= LakeCellScanRadius; ++OffsetY)
	{
		for (int32 OffsetX = -LakeCellScanRadius; OffsetX <= LakeCellScanRadius; ++OffsetX)
		{
			FMountainLakeData Lake;
			if (!MakeLakeCandidate(BaseCellX + OffsetX, BaseCellY + OffsetY, Lake))
			{
				continue;
			}

			float EffectiveDistance = 0.0f;
			if (LakeReachesPoint(Lake, WorldX, WorldY, EffectiveDistance) && ValidateLake(Lake, SeedLow, SeedHigh))
			{
				OutLakes.Add(Lake);
			}
		}
	}
}

void FTerrainGenerator::CacheChunkLakes()
{
	using namespace TerrainChunkPrivate;

	ChunkLakes.Reset();

	float SeedLow = 0.0f;
	float SeedHigh = 0.0f;
	GetSeedOffsets(SeedLow, SeedHigh);

	// The height cache samples one coarse step beyond the chunk on every side.
	const float ChunkSize = ChunkWorldSize;
	const float Border = CellSize * static_cast<float>(1 << (NumLODs - 1));
	const float AreaMinX = static_cast<float>(InitData.Coords.X) * ChunkSize - Border;
	const float AreaMinY = static_cast<float>(InitData.Coords.Y) * ChunkSize - Border;
	const float AreaMaxX = AreaMinX + ChunkSize + 2.0f * Border;
	const float AreaMaxY = AreaMinY + ChunkSize + 2.0f * Border;

	const int32 MinCellX = FMath::FloorToInt((AreaMinX - LakeMaxReach) / LakeCellSize);
	const int32 MaxCellX = FMath::FloorToInt((AreaMaxX + LakeMaxReach) / LakeCellSize);
	const int32 MinCellY = FMath::FloorToInt((AreaMinY - LakeMaxReach) / LakeCellSize);
	const int32 MaxCellY = FMath::FloorToInt((AreaMaxY + LakeMaxReach) / LakeCellSize);

	// Same global (CellY, CellX) order as GetMountainLakesNear, so both fold overlapping valleys identically.
	for (int32 CellY = MinCellY; CellY <= MaxCellY; ++CellY)
	{
		for (int32 CellX = MinCellX; CellX <= MaxCellX; ++CellX)
		{
			FMountainLakeData Lake;
			if (!MakeLakeCandidate(CellX, CellY, Lake))
			{
				continue;
			}

			const float Reach = Lake.Radius * LakeTransitionRadiusScale / MinLakeShapeScale + ShoreWarpAmplitude;
			const float CenterX = static_cast<float>(Lake.Center.X);
			const float CenterY = static_cast<float>(Lake.Center.Y);
			const float DX = FMath::Max3(AreaMinX - CenterX, 0.0f, CenterX - AreaMaxX);
			const float DY = FMath::Max3(AreaMinY - CenterY, 0.0f, CenterY - AreaMaxY);
			if (DX * DX + DY * DY > Reach * Reach)
			{
				continue;
			}

			if (ValidateLake(Lake, SeedLow, SeedHigh))
			{
				ChunkLakes.Add(Lake);
			}
		}
	}
}

float FTerrainGenerator::SampleRiverNoise(float WorldX, float WorldY, float SeedLow, float SeedHigh) const
{
	using namespace TerrainChunkPrivate;

	// The broad warp swings the whole strait and the finer one winds it into meanders. Both offsets are taken
	// from the unwarped position and added, so their distortions sum instead of compounding.
	const float BendOffsetX = FMath::PerlinNoise2D(FVector2D(WorldX * RiverBendWarpFrequency + SeedLow, WorldY * RiverBendWarpFrequency + 17.0f)) * RiverBendWarpAmplitude;
	const float BendOffsetY = FMath::PerlinNoise2D(FVector2D(WorldX * RiverBendWarpFrequency + 31.0f, WorldY * RiverBendWarpFrequency + SeedHigh)) * RiverBendWarpAmplitude;

	const float MeanderOffsetX = FMath::PerlinNoise2D(FVector2D(WorldX * RiverMeanderFrequency + SeedHigh + 53.0f, WorldY * RiverMeanderFrequency)) * RiverMeanderAmplitude;
	const float MeanderOffsetY = FMath::PerlinNoise2D(FVector2D(WorldX * RiverMeanderFrequency, WorldY * RiverMeanderFrequency + SeedLow + 71.0f)) * RiverMeanderAmplitude;

	const float RiverX = WorldX + BendOffsetX + MeanderOffsetX;
	const float RiverY = WorldY + BendOffsetY + MeanderOffsetY;

	constexpr float RiverOffset = 83.0f;
	return FMath::PerlinNoise2D(FVector2D(RiverX * RiverFrequency + SeedHigh + RiverOffset, RiverY * RiverFrequency + SeedLow + RiverOffset));
}

float FTerrainGenerator::ComputeMountainRiverFade(float WorldX, float WorldY) const
{
	using namespace TerrainChunkPrivate;

	const FBiomeContext Context = QueryBiomeSites(WorldX, WorldY, nullptr);
	if (Context.PrimaryBiome == EBiomeType::Mountains)
	{
		return 1.0f;
	}
	if (Context.SecondaryBiome != EBiomeType::Mountains)
	{
		return 0.0f;
	}

	// DistanceToEdge is 0 on the cell border and grows as the point leaves the mountain.
	return 1.0f - FMath::SmoothStep(0.0f, RiverMountainFadeWidth, Context.DistanceToEdge);
}

float FTerrainGenerator::ComputeStraitMacroCap(float WorldX, float WorldY, float SeedLow, float SeedHigh, float NaturalMacro) const
{
	using namespace TerrainChunkPrivate;

	if (RiverFrequency <= 0.0f || RiverWidth <= 0.0f)
	{
		return NaturalMacro;
	}

	// Straits shoal out and end before the world origin, so the spawn area is never cut off by water.
	const float DistFromCenter = FMath::Sqrt(WorldX * WorldX + WorldY * WorldY);
	const float StraitStrength = FMath::SmoothStep(StraitFreeRadius, StraitFreeRadius + StraitFreeFadeWidth, DistFromCenter);
	if (StraitStrength <= 0.0f)
	{
		return NaturalMacro;
	}

	float OceanLimit = 0.0f;
	float MountainLimit = 0.0f;
	GetZoneLimits(OceanLimit, MountainLimit);

	// Continentalness that puts the ocean bed at -RiverDepth: the inverse of the bed profile in ComputeHeight.
	const float BedDepthShare = FMath::Clamp(RiverDepth / FMath::Max(OceanMaxDepth, 1.0f), 0.0f, 1.0f);
	const float StraitBedMacro = OceanLimit * FMath::Pow(1.0f - BedDepthShare, 1.0f / OceanShelfExponent);
	if (NaturalMacro <= StraitBedMacro)
	{
		return NaturalMacro;
	}

	const FVector2D WidthUV(WorldX * RiverWidthVariationFrequency + SeedLow + 97.0f, WorldY * RiverWidthVariationFrequency + SeedHigh + 97.0f);
	const float WidthVariation = 0.7f + 0.6f * FMath::Clamp((FMath::PerlinNoise2D(WidthUV) + 1.0f) * 0.5f, 0.0f, 1.0f);
	const float HalfWidth = 0.5f * RiverWidth * WidthVariation;

	// The cap rises linearly in continentalness beyond the waterline, so the beach ramp (a smoothstep of it) spans
	// exactly RiverShoreWidth. Beyond Reach the cap is above the natural value and changes nothing.
	const float CapSlope = (MountainLimit - OceanLimit) / FMath::Max(RiverShoreWidth, 100.0f);
	const float Reach = HalfWidth + FMath::Max(NaturalMacro - OceanLimit, 0.0f) / CapSlope;

	// The noise can not change faster than RiverMaxNoiseSlope per noise unit, so |Noise| / MaxSlope is a lower
	// bound of the distance to the centreline, and most of the map skips the gradient samples.
	const float Noise = SampleRiverNoise(WorldX, WorldY, SeedLow, SeedHigh);
	const float MaxSlope = RiverMaxNoiseSlope * RiverFrequency;
	if (FMath::Abs(Noise) >= Reach * MaxSlope)
	{
		return NaturalMacro;
	}

	// First-order distance to the zero line, |n| / |grad n|, in world units: the strait keeps the same width on the
	// ground wherever the noise is steep or shallow.
	const float NoiseX = SampleRiverNoise(WorldX + RiverGradientStep, WorldY, SeedLow, SeedHigh);
	const float NoiseY = SampleRiverNoise(WorldX, WorldY + RiverGradientStep, SeedLow, SeedHigh);
	const float Gradient = FMath::Sqrt(FMath::Square(NoiseX - Noise) + FMath::Square(NoiseY - Noise)) / RiverGradientStep;
	const float Distance = FMath::Abs(Noise) / FMath::Max(Gradient, MaxSlope * 0.01f);
	if (Distance >= Reach)
	{
		return NaturalMacro;
	}

	// Straits never cross deserts: the cap fades out across the blend band around desert cells, so a strait
	// running into a desert ends in a bay at its edge. Checked last, as only points near a strait pay for the
	// Voronoi query. A strait that reaches a mountain shoals out over RiverMountainFadeWidth and is gone at the foothill,
	// so the bed rises back to the untouched relief instead of cutting the mass.
	const float DesertKeep = 1.0f - ComputeBiomeShare(WorldX, WorldY, EBiomeType::Desert);
	const float MountainKeep = 1.0f - ComputeMountainRiverFade(WorldX, WorldY);
	const float FreeStrength = StraitStrength * DesertKeep * MountainKeep;
	if (FreeStrength <= 0.0f)
	{
		return NaturalMacro;
	}

	const float Cap = Distance >= HalfWidth
		? OceanLimit + (Distance - HalfWidth) * CapSlope
		: FMath::Lerp(StraitBedMacro, OceanLimit, FMath::SmoothStep(HalfWidth * RiverFlatBedShare, HalfWidth, Distance));
	return FMath::Lerp(NaturalMacro, FMath::Min(NaturalMacro, Cap), FreeStrength);
}

float FTerrainGenerator::GetHeightAt(float WorldX, float WorldY) const
{
	TArray<FMountainLakeData, TInlineAllocator<4>> NearbyLakes;
	GetMountainLakesNear(WorldX, WorldY, NearbyLakes);
	return ComputeHeight(WorldX, WorldY, NearbyLakes);
}

float FTerrainGenerator::ComputeHeight(float WorldX, float WorldY, TConstArrayView<FMountainLakeData> CandidateLakes, EBiomeType* OutBiomeId, FColor* OutVertexColor, float DetailScale) const
{
	using namespace TerrainChunkPrivate;

	float SeedLow = 0.0f;
	float SeedHigh = 0.0f;
	GetSeedOffsets(SeedLow, SeedHigh);

	const float MacroSample = SampleContinentalness(WorldX, WorldY, SeedLow, SeedHigh);

	float OceanLimit = 0.0f;
	float MountainLimit = 0.0f;
	GetZoneLimits(OceanLimit, MountainLimit);

	const bool bIsOcean = MacroSample < OceanLimit;

	// OceanAlpha goes from 0 in the deep basin to 1 at the shoreline; PlainsAlpha from 0 at the shoreline to 1
	// where land biomes reach full height.
	const float OceanAlpha = bIsOcean ? MacroSample / OceanLimit : 1.0f;
	const float PlainsAlpha = bIsOcean ? 0.0f : FMath::Clamp((MacroSample - OceanLimit) / (MountainLimit - OceanLimit), 0.0f, 1.0f);

	float BaseElevation = 0.0f;
	float DetailElevation = 0.0f;
	float MountainsWeight = 0.0f;
	bool bInOasis = false;
	FBiomeContext Context;
	float BiomeWeights[BiomeCount] = {};

	if (bIsOcean)
	{
		// Open sea and river straits alike. Steep drop into the deep basin, gentle shelf near the shore. Underwater
		// hills fade out towards the shoreline so they never break through Z = 0.
		const float OceanBed = FMath::Lerp(-OceanMaxDepth, 0.0f, FMath::Pow(OceanAlpha, OceanShelfExponent));
		const float UnderwaterHills = SampleHills(WorldX, WorldY, OceanHillsFrequency, SeedLow, SeedHigh)
			* OceanHillsAmplitude * (1.0f - OceanAlpha);
		BaseElevation = FMath::Min(0.0f, OceanBed + UnderwaterHills);

		if (OceanDetailFrequency > 0.0f && OceanDetailAmplitude > 0.0f)
		{
			const FVector2D OceanDetailUV(WorldX * OceanDetailFrequency + SeedHigh, WorldY * OceanDetailFrequency + SeedLow);
			DetailElevation = FMath::PerlinNoise2D(OceanDetailUV) * OceanDetailAmplitude * (1.0f - OceanAlpha);
		}

		BiomeWeights[static_cast<int32>(EBiomeType::Ocean)] = 1.0f;
	}
	else
	{
		TArray<FBiomeSite, TInlineAllocator<8>> BlendSites;
		Context = QueryBiomeSites(WorldX, WorldY, &BlendSites);

		// Height blend across cell borders. The primary cell has weight 1; every neighbouring cell fades in towards
		// its border with weight 1 - EdgeBlendWeight measured to that cell. Taking all neighbours rather than only
		// the second nearest keeps the height continuous where the second and third nearest sites swap, and both
		// sides of a border get the same average instead of each other's height.
		const float BlendWidth = GetBiomeBlendWidth();
		const float Dist1 = BlendSites[0].Distance;

		const int32 PrimaryIndex = static_cast<int32>(Context.PrimaryBiome);
		BiomeWeights[PrimaryIndex] = 1.0f;

		float NeighbourWeights[BiomeCount] = {};
		float WeightSum = 1.0f;
		for (int32 SiteIndex = 1; SiteIndex < BlendSites.Num(); ++SiteIndex)
		{
			const FBiomeSite& Site = BlendSites[SiteIndex];
			const float Weight = 1.0f - FMath::SmoothStep(0.0f, BlendWidth, Site.Distance - Dist1);
			NeighbourWeights[static_cast<int32>(Site.Biome)] += Weight;
			BiomeWeights[static_cast<int32>(Site.Biome)] += Weight;
			WeightSum += Weight;
		}

		const float InvWeightSum = 1.0f / WeightSum;

		// Share of the swamp, weighted like the other biomes but over the narrow SwampBlendWidth: the same formula
		// keeps it continuous, and it reaches 1 much closer to the border, so the land drops into the swamp as a
		// steep bank. The narrow width never exceeds BlendWidth, so wherever the other biomes have no weight left
		// the share is already 1.
		const int32 SwampIndex = static_cast<int32>(EBiomeType::Swamp);
		const float SwampWidth = FMath::Clamp(SwampBlendWidth, 100.0f, BlendWidth);
		float SwampWeight = (PrimaryIndex == SwampIndex) ? 1.0f : 0.0f;
		float NarrowWeightSum = 1.0f;
		for (int32 SiteIndex = 1; SiteIndex < BlendSites.Num(); ++SiteIndex)
		{
			const FBiomeSite& Site = BlendSites[SiteIndex];
			const float Weight = 1.0f - FMath::SmoothStep(0.0f, SwampWidth, Site.Distance - Dist1);
			NarrowWeightSum += Weight;
			if (Site.Biome == EBiomeType::Swamp)
			{
				SwampWeight += Weight;
			}
		}
		const float SwampShare = SwampWeight / NarrowWeightSum;

		// The other biomes blend over the regular width. The primary cell is evaluated at its interior depth;
		// neighbours at 0, since the point lies outside them. Detail roughness is sampled per biome and blended
		// the same way, so each biome keeps its own frequency and amplitude.
		const float PrimaryInterior = GetBiomeInterior(Context.DistanceToEdge);
		float OtherElevation = 0.0f;
		float OtherDetail = 0.0f;
		float OtherWeightSum = 0.0f;
		if (PrimaryIndex != SwampIndex)
		{
			OtherElevation = ComputeBiomeHeight(WorldX, WorldY, Context.PrimaryBiome, MacroSample, PrimaryInterior, Context.DistanceToEdge, SeedLow, SeedHigh);
			OtherDetail = SampleBiomeDetail(WorldX, WorldY, Context.PrimaryBiome, Context.DistanceToEdge, SeedLow, SeedHigh);
			OtherWeightSum = 1.0f;
		}

		for (int32 BiomeIndex = 0; BiomeIndex < BiomeCount; ++BiomeIndex)
		{
			const float Weight = NeighbourWeights[BiomeIndex];
			if (Weight <= 0.0f || BiomeIndex == SwampIndex)
			{
				continue;
			}

			const EBiomeType NeighbourBiome = static_cast<EBiomeType>(BiomeIndex);
			OtherElevation += ComputeBiomeHeight(WorldX, WorldY, NeighbourBiome, MacroSample, 0.0f, 0.0f, SeedLow, SeedHigh) * Weight;
			OtherDetail += SampleBiomeDetail(WorldX, WorldY, NeighbourBiome, /*DistanceToEdge=*/0.0f, SeedLow, SeedHigh) * Weight;
			OtherWeightSum += Weight;
		}

		float LandElevation = LandEdgeZ;
		if (OtherWeightSum > 0.0f)
		{
			LandElevation = OtherElevation / OtherWeightSum;
			DetailElevation = OtherDetail / OtherWeightSum;
		}

		if (SwampShare > 0.0f)
		{
			LandElevation = FMath::Lerp(LandElevation, ComputeSwamp(WorldX, WorldY, SeedLow, SeedHigh), SwampShare);
			DetailElevation *= 1.0f - SwampShare;
		}

		// Oases only exist deep inside desert cells, where no other biome blends in, so they reshape pure desert
		// ground. Detail noise fades out towards the centre to keep the shore flat and the pond clean.
		float OasisDistance = 0.0f;
		if (Context.PrimaryBiome == EBiomeType::Desert && FindOasis(WorldX, WorldY, SeedLow, SeedHigh, OasisDistance))
		{
			LandElevation = ComputeOasisZ(OasisDistance, LandElevation);
			DetailElevation *= FMath::SmoothStep(0.0f, GetOasisRadius(), OasisDistance);
			bInOasis = true;
		}

		// Beach ramp: every biome rises from the waterline and goes under water the same way, at the open sea
		// and along the river straits. Inland it is 1, so the swamp keeps its own level there.
		BaseElevation = LandElevation * ComputeLandRamp(MacroSample);

		MountainsWeight = BiomeWeights[static_cast<int32>(EBiomeType::Mountains)] * InvWeightSum;
	}

	// 0 inland, 1 at the shoreline and at sea.
	const float CoastWeight = 1.0f - FMath::SmoothStep(0.0f, FloodedShorePlainsAlpha, PlainsAlpha);

	// Lake valleys are blended by the mountain weight, so a valley reaching past a mountain cell meets the
	// neighbouring land smoothly. Overlapping valleys are folded in a fixed order; each one is the identity at its
	// transition radius, so lakes entering or leaving the list never leave a seam.
	if (MountainsWeight > 0.0f)
	{
		for (const FMountainLakeData& Lake : CandidateLakes)
		{
			float EffectiveDistance = 0.0f;
			if (!LakeReachesPoint(Lake, WorldX, WorldY, EffectiveDistance))
			{
				continue;
			}

			const float LakeTerrainZ = ComputeLakeTerrainZ(Lake, EffectiveDistance, BaseElevation);
			BaseElevation = FMath::Lerp(BaseElevation, LakeTerrainZ, MountainsWeight);

			// Rocky and grassy texture is kept along the water and the valley floor, without 8 m spikes.
			const float LakeDetail = FMath::Lerp(LakeDetailScale, 1.0f,
				FMath::SmoothStep(Lake.Radius * LakeDetailRadiusScale, Lake.Radius * LakeTransitionRadiusScale, EffectiveDistance));
			DetailElevation *= FMath::Lerp(1.0f, LakeDetail, MountainsWeight);
		}
	}

	// Strict ID: the sea and the river straits, plus shore that lies under water (flooded swamp edges), so PCG never
	// treats flooded ground as land. Inland swamp pools keep their biome. The whole oasis radius, pond included,
	// stays Oasis even where the bowl is below sea level, so PCG can tell oasis water from the ocean.
	const bool bFloodedShore = CoastWeight > 0.0f && BaseElevation < 0.0f;
	EBiomeType StrictBiome = Context.PrimaryBiome;
	if (bInOasis)
	{
		StrictBiome = EBiomeType::Oasis;
	}
	else if (bIsOcean || bFloodedShore)
	{
		StrictBiome = EBiomeType::Ocean;
	}
	else if (Context.PrimaryBiome == EBiomeType::Mountains && BaseElevation < FoothillsMaxHeight)
	{
		StrictBiome = EBiomeType::Foothills;
		DetailElevation = SampleBiomeDetail(WorldX, WorldY, EBiomeType::Foothills, Context.DistanceToEdge, SeedLow, SeedHigh);
	}

	if (OutBiomeId)
	{
		*OutBiomeId = StrictBiome;
	}

	if (OutVertexColor)
	{
		int32 PrimaryId = 0;
		int32 SecondaryId = 0;
		float PrimaryWeight = 0.0f;
		float SecondaryWeight = 0.0f;
		SelectTopBiomeWeights(BiomeWeights, PrimaryId, PrimaryWeight, SecondaryId, SecondaryWeight);

		// Past the blend the second weight drops to 0 and the pair would collapse to (ID, ID). The next node
		// inward still faces the same second-nearest site, so keep that id and let B say this node is entirely primary.
		if (!bIsOcean && PrimaryId == SecondaryId)
		{
			const int32 PartnerId = static_cast<int32>(Context.SecondaryBiome);
			if (PartnerId != PrimaryId)
			{
				SecondaryId = PartnerId;
				SecondaryWeight = 0.0f;
			}
		}

		// Sea and oasis are not Voronoi weights. They become the heavier biome and keep the previous leader as
		// the other ID, so a shore or oasis rim does not interpolate through unrelated biome numbers.
		const int32 StrictId = static_cast<int32>(StrictBiome);
		if ((StrictBiome == EBiomeType::Ocean || StrictBiome == EBiomeType::Oasis || StrictBiome == EBiomeType::Foothills) && StrictId != PrimaryId)
		{
			SecondaryId = PrimaryId;
			SecondaryWeight = PrimaryWeight;
			PrimaryId = StrictId;
			PrimaryWeight = SecondaryWeight + FMath::Max(SecondaryWeight, 1.0f);
		}
		else if (CoastWeight > 0.0f && PrimaryId != static_cast<int32>(EBiomeType::Ocean))
		{
			// The dry side of the waterline names the sea as its second biome, so the quad that crosses the shore
			// carries Ocean in R on every vertex and only the weight in B changes.
			SecondaryId = static_cast<int32>(EBiomeType::Ocean);
			SecondaryWeight = PrimaryWeight * CoastWeight;
		}

		*OutVertexColor = PackBiomeVertexColor(PrimaryId, PrimaryWeight, SecondaryId, SecondaryWeight);

		// A Voronoi vertex has a third biome in the weights. Drop it: the color keeps only the top pair,
		// and B is pinned so the node does not sit mid-blend between three IDs.
		int32 PositiveBiomes = 0;
		for (int32 BiomeIndex = 0; BiomeIndex < BiomeCount; ++BiomeIndex)
		{
			if (BiomeWeights[BiomeIndex] > 0.0f)
			{
				++PositiveBiomes;
			}
		}
		if (PositiveBiomes >= 3 && OutVertexColor->R != OutVertexColor->G)
		{
			const uint8 Dominant = DecodeDominantBiome(*OutVertexColor);
			OutVertexColor->B = Dominant == OutVertexColor->G ? 255 : 0;
		}
	}

	// No detail noise near sea level: it would break the waterline into grid-aligned steps.
	// DetailScale is 1 on LOD 0. Coarse LODs pass CellSize / Step, faded back to 1 within 4 m of the chunk edge.
	DetailElevation *= DetailScale;
	DetailElevation *= FMath::SmoothStep(0.0f, ShoreDetailFadeHeight, FMath::Abs(BaseElevation));

	return BaseElevation + DetailElevation + GetTerrainDeltaHeight(WorldX, WorldY);
}

float FTerrainGenerator::GetTerrainDeltaHeight(float /*WorldX*/, float /*WorldY*/) const
{
	return 0.0f;
}

void FTerrainGenerator::BuildTerrainBuffers(int32 LOD, bool bWithSkirts, FTerrainMeshBuffers& OutBuffers) const
{
	using namespace TerrainChunkPrivate;

	const int32 ClampedLOD = FMath::Clamp(LOD, 0, NumLODs - 1);
	const int32 QuadCount = QuadsPerSide >> ClampedLOD;
	const int32 VerticesPerSide = QuadCount + 1;
	const int32 VertexCount = VerticesPerSide * VerticesPerSide;
	const float Step = CellSize * static_cast<float>(QuadsPerSide / QuadCount);

	OutBuffers.QuadCount = QuadCount;
	OutBuffers.Step = Step;

	// Heights are cached with a one-cell border. LOD 0 reads it for CellSize normals.
	// Coarser LODs sample their normals directly over CellSize so a shared edge matches the fine neighbour.
	const int32 HeightsPerSide = VerticesPerSide + 2;
	OutBuffers.HeightsPerSide = HeightsPerSide;

	const float ChunkOriginX = static_cast<float>(InitData.Coords.X) * ChunkWorldSize;
	const float ChunkOriginY = static_cast<float>(InitData.Coords.Y) * ChunkWorldSize;

	TArray<float>& Heights = OutBuffers.Heights;
	Heights.SetNumUninitialized(HeightsPerSide * HeightsPerSide);

	// Packed biome pair per sample, in world space so it agrees with the neighbouring chunk along the seam.
	TArray<FColor> BiomeColors;
	BiomeColors.SetNumUninitialized(HeightsPerSide * HeightsPerSide);

	for (int32 Y = 0; Y < HeightsPerSide; ++Y)
	{
		const float LocalY = static_cast<float>(Y - 1) * Step;
		const float WorldY = ChunkOriginY + LocalY;
		for (int32 X = 0; X < HeightsPerSide; ++X)
		{
			const float LocalX = static_cast<float>(X - 1) * Step;
			const float WorldX = ChunkOriginX + LocalX;
			const int32 Index = X + (Y * HeightsPerSide);

			// LOD 0 keeps the full surface. Coarser LODs damp detail inside the chunk and restore it
			// across the outer 4 m, so a vertex shared with a finer neighbour has the same height.
			float DetailScale = 1.0f;
			if (ClampedLOD > 0)
			{
				const float EdgeDistance = FMath::Min(
					FMath::Min(LocalX, ChunkWorldSize - LocalX),
					FMath::Min(LocalY, ChunkWorldSize - LocalY));
				const float InteriorScale = CellSize / Step;
				const float EdgeBlend = FMath::SmoothStep(0.0f, 400.0f, EdgeDistance);
				DetailScale = FMath::Lerp(1.0f, InteriorScale, EdgeBlend);
			}

			Heights[Index] = ComputeHeight(WorldX, WorldY, ChunkLakes, nullptr, &BiomeColors[Index], DetailScale);
		}
	}

	// Pure nodes touching a border take that border's (R, G) with B pinned, before any triangle is built.
	HarmonizeBiomePairColors(BiomeColors, HeightsPerSide);

	auto HeightAt = [&Heights, HeightsPerSide](int32 X, int32 Y)
	{
		return CachedHeight(Heights, HeightsPerSide, X, Y);
	};

	BuildTerrainTriangles(QuadCount, OutBuffers.Triangles);

	const int32 SkirtVertexCount = bWithSkirts ? QuadCount * 4 : 0;
	OutBuffers.Vertices.Reset(VertexCount + SkirtVertexCount);
	OutBuffers.Normals.Reset(VertexCount + SkirtVertexCount);
	OutBuffers.UVs.Reset(VertexCount + SkirtVertexCount);
	OutBuffers.Tangents.Reset(VertexCount + SkirtVertexCount);
	OutBuffers.Colors.Reset(VertexCount + SkirtVertexCount);

	// Every LOD differences the unfiltered surface over CellSize, so a vertex shared by two LODs gets the
	// same normal. LOD 0 reads that spacing from the height cache. Coarser LODs sample ComputeHeight at
	// ±CellSize with DetailScale 1, not the filtered grid and not the coarse Step.
	const float NormalZ = (2.0f * CellSize) / FMath::Max(NormalStrength, 0.01f);
	const float InvQuads = 1.0f / static_cast<float>(QuadCount);
	const bool bGridNormal = FMath::IsNearlyEqual(Step, CellSize);

	for (int32 Y = 0; Y < VerticesPerSide; ++Y)
	{
		const float LocalY = static_cast<float>(Y) * Step;

		for (int32 X = 0; X < VerticesPerSide; ++X)
		{
			const float LocalX = static_cast<float>(X) * Step;

			auto SampleNormalHeight = [&](float OffsetX, float OffsetY) -> float
			{
				if (bGridNormal)
				{
					const int32 GridX = X + FMath::RoundToInt(OffsetX / Step);
					const int32 GridY = Y + FMath::RoundToInt(OffsetY / Step);
					return HeightAt(GridX, GridY);
				}

				return ComputeHeight(ChunkOriginX + LocalX + OffsetX, ChunkOriginY + LocalY + OffsetY, ChunkLakes, nullptr, nullptr);
			};

			const float HeightL = SampleNormalHeight(-CellSize, 0.0f);
			const float HeightR = SampleNormalHeight(CellSize, 0.0f);
			const float HeightD = SampleNormalHeight(0.0f, -CellSize);
			const float HeightU = SampleNormalHeight(0.0f, CellSize);

			OutBuffers.Vertices.Add(FVector(LocalX, LocalY, HeightAt(X, Y)));
			OutBuffers.Normals.Add(FVector(HeightL - HeightR, HeightD - HeightU, NormalZ).GetSafeNormal());
			OutBuffers.UVs.Add(FVector2D(static_cast<float>(X) * InvQuads, static_cast<float>(Y) * InvQuads));
			OutBuffers.Tangents.Add(FProcMeshTangent(1.0f, 0.0f, 0.0f));

			OutBuffers.Colors.Add(BiomeColors[(X + 1) + ((Y + 1) * HeightsPerSide)]);
		}
	}

	if (bWithSkirts)
	{
		AppendSkirts(OutBuffers, SkirtDepth);
	}

	// A triangle that still has two pairs gets its own corner copies. Shared grid vertices stay shared only
	// where every triangle that uses them agrees on (R, G).
	SplitInconsistentBiomeTriangles(OutBuffers);
}

void FTerrainGenerator::BuildGrassTransforms(const FTerrainMeshBuffers& Lod0Terrain, TMap<uint8, TArray<FTransform>>& OutTransforms,
	TMap<uint8, TArray<float>>& OutWindPhases) const
{
	using namespace TerrainChunkPrivate;

	OutTransforms.Reset();
	OutWindPhases.Reset();

	if (GrassRules.Num() == 0 || Lod0Terrain.QuadCount <= 0)
	{
		return;
	}

	const float ChunkOriginX = static_cast<float>(InitData.Coords.X) * ChunkWorldSize;
	const float ChunkOriginY = static_cast<float>(InitData.Coords.Y) * ChunkWorldSize;

	for (const FBiomeGrassSpawnRule& Rule : GrassRules)
	{
		if (Rule.BiomeId == static_cast<uint8>(EBiomeType::Ocean) || Rule.SpawnChance <= 0.0f)
		{
			continue;
		}

		// First rule for a biome ID wins when the request array has duplicates.
		if (OutTransforms.Contains(Rule.BiomeId))
		{
			continue;
		}

		const float SpawnStep = FMath::Max(Rule.SpawnStep, 10.0f);
		const float HalfStep = SpawnStep * 0.5f;
		TArray<FTransform>& Transforms = OutTransforms.Add(Rule.BiomeId);
		TArray<float>& WindPhases = OutWindPhases.Add(Rule.BiomeId);

		const int32 SamplesPerSide = FMath::Max(1, FMath::CeilToInt(ChunkWorldSize / SpawnStep));
		Transforms.Reserve(SamplesPerSide * SamplesPerSide / 2);
		WindPhases.Reserve(SamplesPerSide * SamplesPerSide / 2);

		// Cell centres of a grid covering the chunk; jitter stays inside the cell so samples do not leave the chunk.
		for (float LocalY = HalfStep; LocalY < ChunkWorldSize; LocalY += SpawnStep)
		{
			for (float LocalX = HalfStep; LocalX < ChunkWorldSize; LocalX += SpawnStep)
			{
				const float GridWorldX = ChunkOriginX + LocalX;
				const float GridWorldY = ChunkOriginY + LocalY;

				const uint32 GrassHash = HashCombine(
					HashCombine(GetTypeHash(GridWorldX), GetTypeHash(GridWorldY)),
					static_cast<uint32>(InitData.Seed) ^ (static_cast<uint32>(Rule.BiomeId) * 0x9E3779B9u));
				FRandomStream GrassStream(static_cast<int32>(GrassHash));

				const float SampleLocalX = LocalX + GrassStream.FRandRange(-HalfStep, HalfStep);
				const float SampleLocalY = LocalY + GrassStream.FRandRange(-HalfStep, HalfStep);

				uint8 SampleBiomeId = 0;
				float SurfaceZ = 0.0f;
				FVector SurfaceNormal = FVector::UpVector;
				if (!SampleLod0Surface(Lod0Terrain, SampleLocalX, SampleLocalY, SampleBiomeId, SurfaceZ, SurfaceNormal))
				{
					continue;
				}
				if (SampleBiomeId != Rule.BiomeId)
				{
					continue;
				}
				if (SurfaceZ < Rule.MinHeight || SurfaceZ > Rule.MaxHeight)
				{
					continue;
				}
				if (FVector::DotProduct(SurfaceNormal, FVector::UpVector) < Rule.MinUpDot)
				{
					continue;
				}
				if (GrassStream.FRand() > Rule.SpawnChance)
				{
					continue;
				}

				const float Scale = GrassStream.FRandRange(Rule.ScaleMin, Rule.ScaleMax);
				const FRotator Rotation(0.0f, GrassStream.FRandRange(0.0f, 360.0f), 0.0f);
				const float WindPhase = GrassStream.FRand();

				Transforms.Emplace(Rotation, FVector(SampleLocalX, SampleLocalY, SurfaceZ), FVector(Scale));
				WindPhases.Add(WindPhase);
			}
		}
	}
}

FTerrainBuildResult ATerrainChunk::CalculateTerrainData(FChunkInitData InInitData, float InCellSize, int32 InQuadsPerSide, const FTerrainBuildRequest& Request)
{
	FTerrainGenerator Generator(Request.Settings, InInitData, InCellSize, InQuadsPerSide);
	Generator.GrassRules = Request.GrassRules;

	// Lakes are found once per build; the terrain grids and the water mesh all reuse them.
	Generator.CacheChunkLakes();

	FTerrainBuildResult Result;
	if (Request.bDedicatedServer)
	{
		// Dedicated server: collision mesh only, never grass.
		Generator.BuildTerrainBuffers(0, false, Result.Terrain);
		return Result;
	}

	Generator.BuildTerrainBuffers(Request.LOD, true, Result.Terrain);
	Generator.BuildWaterBuffers(Result.Terrain, Result.Water);

	if (Request.bCollisionGrid)
	{
		Generator.BuildTerrainBuffers(0, false, Result.Collision);
	}

	// Grass only when the visual mesh itself is LOD 0 (bBuildGrass is false otherwise). No scratch LOD-0 build.
	if (Request.bBuildGrass)
	{
		Generator.BuildGrassTransforms(Result.Terrain, Result.GrassTransforms, Result.GrassWindPhases);
	}

	return Result;
}

void ATerrainChunk::GenerateTerrainMesh()
{
	if (!ProceduralMesh || InitData.Seed == 0 || IsHidden())
	{
		return;
	}

	if (bIsGenerating)
	{
		bRegenerationPending = true;
		return;
	}

	const bool bWantsCollision = ShouldHaveCollision();
	const bool bDedicatedServer = IsNetMode(NM_DedicatedServer);

	if (bDedicatedServer && !bWantsCollision)
	{
		// Nothing is rendered here: geometry exists only to carry collision, and only while it is enabled.
		ProceduralMesh->ClearAllMeshSections();
		bHasGeneratedMesh = true;
		return;
	}

	FTerrainBuildRequest Request;
	Request.Settings = ResolveGenerationSettings();
	ResolvedGrass.Reset();
	if (const ATerrainManager* Manager = FindTerrainManager(GetWorld()))
	{
		ResolvedGrass = Manager->GetGrassEntries();
	}
	Request.bDedicatedServer = bDedicatedServer;
	Request.LOD = bDedicatedServer ? 0 : CurrentLOD;

	// At LOD 0 the visual grid is the collision grid, so it carries collision itself. A rendering authority
	// (listen server, standalone) may show a coarse LOD while players near this chunk still need
	// full-resolution physics, so collision then comes from a hidden LOD 0 section.
	Request.bTerrainCollision = bWantsCollision && Request.LOD == 0;
	Request.bCollisionGrid = bWantsCollision && Request.LOD != 0;

	// Mesh pointers stay on the actor; the worker only gets numeric placement rules.
	// Grass: only at visual LOD 0, only once per InitData, never on a dedicated server.
	Request.bBuildGrass = false;
	Request.GrassRules.Reset();
	if (!bDedicatedServer && !Request.bDedicatedServer && Request.LOD == 0 && !bGrassBuilt)
	{
		Request.GrassRules.Reserve(ResolvedGrass.Num());
		for (const FBiomeGrassEntry& Entry : ResolvedGrass)
		{
			if (!Entry.Mesh || Entry.BiomeId == static_cast<uint8>(EBiomeType::Ocean))
			{
				continue;
			}

			FBiomeGrassSpawnRule Rule;
			Rule.BiomeId = Entry.BiomeId;
			Rule.SpawnStep = Entry.SpawnStep;
			Rule.SpawnChance = Entry.SpawnChance;
			Rule.MinHeight = Entry.MinHeight;
			Rule.MaxHeight = Entry.MaxHeight;
			Rule.MinUpDot = Entry.MinUpDot;
			Rule.ScaleMin = FMath::Min(Entry.ScaleMin, Entry.ScaleMax);
			Rule.ScaleMax = FMath::Max(Entry.ScaleMin, Entry.ScaleMax);
			Request.GrassRules.Add(Rule);
		}
		Request.bBuildGrass = Request.GrassRules.Num() > 0;
	}

	bIsGenerating = true;
	bRegenerationPending = false;

	const FChunkInitData LocalInitData = InitData;
	const float LocalCellSize = CellSize;
	const int32 LocalQuads = QuadsPerSide;
	const uint32 LocalSerial = BuildSerial;

	if (LocalInitData.bForceSynchronous && !bHasGeneratedMesh)
	{
		const FTerrainBuildResult Result = CalculateTerrainData(LocalInitData, LocalCellSize, LocalQuads, Request);
		OnBuildComplete(Request, Result, LocalSerial);
		return;
	}

	TWeakObjectPtr<ATerrainChunk> WeakThis(this);
	Async(EAsyncExecution::ThreadPool, [WeakThis, LocalInitData, LocalCellSize, LocalQuads, LocalSerial, Request = MoveTemp(Request)]() mutable
	{
		FTerrainBuildResult Result = CalculateTerrainData(LocalInitData, LocalCellSize, LocalQuads, Request);

		AsyncTask(ENamedThreads::GameThread, [WeakThis, LocalSerial, Request = MoveTemp(Request), Result = MoveTemp(Result)]()
		{
			if (ATerrainChunk* Chunk = WeakThis.Get())
			{
				Chunk->OnBuildComplete(Request, Result, LocalSerial);
			}
		});
	});
}

void ATerrainChunk::OnBuildComplete(const FTerrainBuildRequest& Request, const FTerrainBuildResult& Result, uint32 Serial)
{
	bIsGenerating = false;

	// A build started before InitData changed describes another chunk; the change has already queued a rebuild.
	if (Serial == BuildSerial)
	{
		ApplyMesh(Request, Result);
	}

	if (bRegenerationPending)
	{
		bRegenerationPending = false;
		GenerateTerrainMesh();
	}
}

void ATerrainChunk::ApplyMesh(const FTerrainBuildRequest& Request, const FTerrainBuildResult& Result)
{
	using namespace TerrainChunkPrivate;

	if (!ProceduralMesh)
	{
		return;
	}

	const FTerrainMeshBuffers& Terrain = Result.Terrain;

	if (Request.bDedicatedServer)
	{
		ProceduralMesh->ClearAllMeshSections();
		ProceduralMesh->CreateMeshSection(TerrainSectionIndex, Terrain.Vertices, Terrain.Triangles, Terrain.Normals,
			Terrain.UVs, Terrain.Colors, Terrain.Tangents, true);
	}
	else
	{
		ProceduralMesh->CreateMeshSection(TerrainSectionIndex, Terrain.Vertices, Terrain.Triangles, Terrain.Normals,
			Terrain.UVs, Terrain.Colors, Terrain.Tangents, Request.bTerrainCollision);
		ProceduralMesh->SetMaterial(TerrainSectionIndex, TerrainMaterial);

		const FTerrainMeshBuffers& Water = Result.Water;
		if (Water.Triangles.Num() > 0)
		{
			ProceduralMesh->CreateMeshSection(WaterSectionIndex, Water.Vertices, Water.Triangles, Water.Normals,
				Water.UVs, Water.Colors, Water.Tangents, false);
			ProceduralMesh->SetMaterial(WaterSectionIndex, WaterMaterial);
		}
		else
		{
			ProceduralMesh->ClearMeshSection(WaterSectionIndex);
		}

		const FTerrainMeshBuffers& Collision = Result.Collision;
		if (Request.bCollisionGrid)
		{
			ProceduralMesh->CreateMeshSection(CollisionSectionIndex, Collision.Vertices, Collision.Triangles, Collision.Normals,
				Collision.UVs, Collision.Colors, Collision.Tangents, true);
			ProceduralMesh->SetMeshSectionVisible(CollisionSectionIndex, false);
		}
		else
		{
			ProceduralMesh->ClearMeshSection(CollisionSectionIndex);
		}
	}

	bHasGeneratedMesh = true;
	bCollisionApplied = Request.bTerrainCollision || Request.bCollisionGrid;

	// Independent of terrain LOD: only builds that asked for grass refill the HISMs; others leave them alone.
	if (Request.bBuildGrass && !Request.bDedicatedServer && !IsNetMode(NM_DedicatedServer))
	{
		EnsureGrassComponents();
		for (const TPair<uint8, TObjectPtr<UHierarchicalInstancedStaticMeshComponent>>& HISMPair : BiomeGrassHISMs)
		{
			if (UHierarchicalInstancedStaticMeshComponent* GrassHISM = HISMPair.Value.Get())
			{
				GrassHISM->ClearInstances();
			}
		}

		for (const TPair<uint8, TArray<FTransform>>& Pair : Result.GrassTransforms)
		{
			if (TObjectPtr<UHierarchicalInstancedStaticMeshComponent>* Found = BiomeGrassHISMs.Find(Pair.Key))
			{
				if (UHierarchicalInstancedStaticMeshComponent* GrassHISM = Found->Get())
				{
					if (Pair.Value.Num() <= 0)
					{
						continue;
					}

					GrassHISM->AddInstances(Pair.Value, /*bShouldReturnIndices=*/false);

					// Custom data 0 = wind phase, 1 = reserved flatten/interaction strength.
					if (const TArray<float>* WindPhases = Result.GrassWindPhases.Find(Pair.Key))
					{
						const int32 Count = FMath::Min(Pair.Value.Num(), WindPhases->Num());
						for (int32 InstanceIndex = 0; InstanceIndex < Count; ++InstanceIndex)
						{
							GrassHISM->SetCustomDataValue(InstanceIndex, 0, (*WindPhases)[InstanceIndex], false);
							GrassHISM->SetCustomDataValue(InstanceIndex, 1, 0.0f, false);
						}
						GrassHISM->MarkRenderStateDirty();
					}
				}
			}
		}
		bGrassBuilt = true;
	}

	GeneratePCG();
}

void ATerrainChunk::EnsureGrassComponents()
{
	if (IsNetMode(NM_DedicatedServer))
	{
		return;
	}

	for (const FBiomeGrassEntry& Entry : ResolvedGrass)
	{
		if (!Entry.Mesh || Entry.BiomeId == static_cast<uint8>(EBiomeType::Ocean))
		{
			continue;
		}

		const int32 CullStart = FMath::Max(0, Entry.CullDistanceStart);
		const int32 CullEnd = FMath::Max(CullStart, Entry.CullDistanceEnd);

		if (TObjectPtr<UHierarchicalInstancedStaticMeshComponent>* Existing = BiomeGrassHISMs.Find(Entry.BiomeId))
		{
			if (UHierarchicalInstancedStaticMeshComponent* GrassHISM = Existing->Get())
			{
				GrassHISM->SetCullDistances(CullStart, CullEnd);
				GrassHISM->NumCustomDataFloats = 2;
			}
			continue;
		}

		const FName CompName = *FString::Printf(TEXT("GrassHISM_%u"), Entry.BiomeId);
		UHierarchicalInstancedStaticMeshComponent* GrassHISM = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, CompName);
		// Root ProceduralMesh is Movable. A Static child cannot attach to it.
		GrassHISM->SetMobility(EComponentMobility::Stationary);
		GrassHISM->SetupAttachment(GetRootComponent());
		GrassHISM->SetStaticMesh(Entry.Mesh);
		GrassHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		GrassHISM->SetGenerateOverlapEvents(false);
		GrassHISM->SetCanEverAffectNavigation(false);
		GrassHISM->SetCastShadow(false);
		GrassHISM->bAffectDynamicIndirectLighting = false;
		GrassHISM->bAffectDistanceFieldLighting = false;
		GrassHISM->SetReceivesDecals(false);
		GrassHISM->NumCustomDataFloats = 2;
		GrassHISM->SetCullDistances(CullStart, CullEnd);
		GrassHISM->RegisterComponent();

		BiomeGrassHISMs.Add(Entry.BiomeId, GrassHISM);
	}
}

void ATerrainChunk::ClearAllGrassInstances()
{
	for (const TPair<uint8, TObjectPtr<UHierarchicalInstancedStaticMeshComponent>>& HISMPair : BiomeGrassHISMs)
	{
		if (UHierarchicalInstancedStaticMeshComponent* GrassHISM = HISMPair.Value.Get())
		{
			GrassHISM->ClearInstances();
		}
	}
	bGrassBuilt = false;
}

void ATerrainChunk::RemoveGrassInRadius(FVector /*Location*/, float /*Radius*/)
{
	// Stub for terraforming. Intended path (game thread):
	// for (auto& Pair : BiomeGrassHISMs) {
	//   TArray<int32> Hits = Pair.Value->GetInstancesOverlappingSphere(Location, Radius, /*bSphereInWorldSpace=*/true);
	//   if (Hits.Num() > 0) { Pair.Value->RemoveInstances(Hits); }
	// }
}

void ATerrainChunk::GeneratePCG()
{
	if (bPCGGenerated || !PCGComponent || !ProceduralMesh || ProceduralMesh->GetNumSections() == 0)
	{
		return;
	}

	// Local, not networked: every machine builds the same terrain from InitData, so each scatters the same
	// deterministic result itself instead of the server multicasting it. Once per InitData, since LOD rebuilds
	// change only the mesh resolution.
	PCGComponent->bActivated = true;
	PCGComponent->GenerateLocal(/*bForce=*/true);
	bPCGGenerated = true;
}

void FTerrainGenerator::BuildWaterBuffers(const FTerrainMeshBuffers& Terrain, FTerrainMeshBuffers& OutWater) const
{
	using namespace TerrainChunkPrivate;

	const TArray<float>& Heights = Terrain.Heights;
	const int32 HeightsPerSide = Terrain.HeightsPerSide;
	const int32 QuadCount = Terrain.QuadCount;
	const int32 VerticesPerSide = QuadCount + 1;
	const int32 VertexCount = VerticesPerSide * VerticesPerSide;
	const float Step = Terrain.Step;

	OutWater = FTerrainMeshBuffers();
	OutWater.QuadCount = QuadCount;
	OutWater.Step = Step;

	if (QuadCount <= 0 || Heights.Num() < HeightsPerSide * HeightsPerSide)
	{
		return;
	}

	// Depth below the water surface at which waves reach full strength (vertex colour R = 255).
	constexpr float FullWaveDepth = 30.0f;

	// World-space UVs in metres, so wave textures tile seamlessly across chunk borders.
	constexpr float UVScale = 0.01f;

	// Lake water reaches slightly past the shore radius so its edge tucks under the rising bank
	// instead of leaving a gap at the waterline.
	const float LakeShoreOverlap = 2.0f * CellSize;

	constexpr float SeaLevel = 0.0f;
	constexpr int32 SeaLevelIndex = 0;

	// Sea water also covers quads whose terrain rises up to this height above sea level, so its edge runs
	// under the bank and the shader can fade the shoreline instead of the mesh ending at a quad border.
	constexpr float SeaShoreOverlapHeight = 150.0f;

	const float ChunkOriginX = static_cast<float>(InitData.Coords.X) * ChunkWorldSize;
	const float ChunkOriginY = static_cast<float>(InitData.Coords.Y) * ChunkWorldSize;

	// Water levels present in this chunk: the sea first, then each mountain lake that reaches it.
	TArray<float> WaterLevels;
	WaterLevels.Add(SeaLevel);
	TArray<FVector2D> LakeCenters;

	TArray<FVector>& WaterVertices = OutWater.Vertices;
	TArray<int32>& WaterTriangles = OutWater.Triangles;
	TArray<FVector>& WaterNormals = OutWater.Normals;
	TArray<FVector2D>& WaterUVs = OutWater.UVs;
	TArray<FColor>& WaterColors = OutWater.Colors;
	TArray<FProcMeshTangent>& WaterTangents = OutWater.Tangents;

	WaterVertices.Reserve(VertexCount);
	WaterNormals.Reserve(VertexCount);
	WaterUVs.Reserve(VertexCount);
	WaterColors.Reserve(VertexCount);
	WaterTangents.Reserve(VertexCount);
	WaterTriangles.Reserve(QuadCount * QuadCount * 6);

	// Grid vertices are shared between neighbouring quads of the same water level; INDEX_NONE means not emitted yet.
	// Each level has its own map, so a sea quad and a lake quad never weld across different surface heights.
	TArray<TArray<int32>> WaterVertexIndexPerLevel;
	WaterVertexIndexPerLevel.AddDefaulted();
	WaterVertexIndexPerLevel[SeaLevelIndex].Init(INDEX_NONE, VertexCount);

	auto FindOrAddLakeLevel = [&](const FMountainLakeData& Lake) -> int32
	{
		const int32 LakeIndex = LakeCenters.Find(Lake.Center);
		if (LakeIndex != INDEX_NONE)
		{
			return LakeIndex + 1;
		}

		LakeCenters.Add(Lake.Center);
		WaterVertexIndexPerLevel.AddDefaulted_GetRef().Init(INDEX_NONE, VertexCount);
		return WaterLevels.Add(Lake.WaterZ);
	};

	auto GetOrAddWaterVertex = [&](int32 X, int32 Y, int32 LevelIndex) -> int32
	{
		int32& Index = WaterVertexIndexPerLevel[LevelIndex][X + (Y * VerticesPerSide)];
		if (Index == INDEX_NONE)
		{
			const float LocalX = static_cast<float>(X) * Step;
			const float LocalY = static_cast<float>(Y) * Step;
			const float WaterZ = WaterLevels[LevelIndex];

			// Wave attenuation mask: 0 where the terrain touches or rises above the water, 1 over deep water.
			const float TerrainHeight = CachedHeight(Heights, HeightsPerSide, X, Y);
			const float DepthAlpha = FMath::Clamp((WaterZ - TerrainHeight) / FullWaveDepth, 0.0f, 1.0f);
			const uint8 WaveMask = static_cast<uint8>(FMath::RoundToInt(DepthAlpha * 255.0f));

			Index = WaterVertices.Add(FVector(LocalX, LocalY, WaterZ));
			WaterNormals.Add(FVector::UpVector);
			WaterTangents.Add(FProcMeshTangent(1.0f, 0.0f, 0.0f));
			WaterUVs.Add(FVector2D((ChunkOriginX + LocalX) * UVScale, (ChunkOriginY + LocalY) * UVScale));
			WaterColors.Add(FColor(WaveMask, WaveMask, WaveMask, 255));
		}
		return Index;
	};

	auto AnyCornerAtOrBelow = [&Heights, HeightsPerSide](int32 X, int32 Y, float Level, float Tolerance = 0.0f)
	{
		const float Limit = Level + Tolerance;
		return CachedHeight(Heights, HeightsPerSide, X, Y) <= Limit ||
			CachedHeight(Heights, HeightsPerSide, X + 1, Y) <= Limit ||
			CachedHeight(Heights, HeightsPerSide, X, Y + 1) <= Limit ||
			CachedHeight(Heights, HeightsPerSide, X + 1, Y + 1) <= Limit;
	};

	for (int32 Y = 0; Y < QuadCount; ++Y)
	{
		for (int32 X = 0; X < QuadCount; ++X)
		{
			int32 LevelIndex = INDEX_NONE;

			const float QuadCenterX = ChunkOriginX + (static_cast<float>(X) + 0.5f) * Step;
			const float QuadCenterY = ChunkOriginY + (static_cast<float>(Y) + 0.5f) * Step;
			const FMountainLakeData Lake = FindDeepestLake(ChunkLakes, QuadCenterX, QuadCenterY);

			bool bValidLakeFloor = false;
			if (Lake.bExists)
			{
				// The deepest corner must be submerged but no deeper than the bowl, otherwise the quad sits over a drop-off.
				const float LowestCornerZ = FMath::Min(
					FMath::Min(CachedHeight(Heights, HeightsPerSide, X, Y), CachedHeight(Heights, HeightsPerSide, X + 1, Y)),
					FMath::Min(CachedHeight(Heights, HeightsPerSide, X, Y + 1), CachedHeight(Heights, HeightsPerSide, X + 1, Y + 1)));
				const float BedDepthActual = Lake.WaterZ - LowestCornerZ;
				bValidLakeFloor = BedDepthActual >= 0.0f && BedDepthActual <= Lake.BedDepth + LakeFloorTolerance;
			}

			if (Lake.bExists &&
				ComputeLakeEffectiveDistance(Lake, QuadCenterX, QuadCenterY) <= Lake.Radius + LakeShoreOverlap &&
				bValidLakeFloor)
			{
				LevelIndex = FindOrAddLakeLevel(Lake);
			}
			else if (AnyCornerAtOrBelow(X, Y, SeaLevel, SeaShoreOverlapHeight))
			{
				LevelIndex = SeaLevelIndex;
			}

			if (LevelIndex == INDEX_NONE)
			{
				continue;
			}

			const int32 BottomLeft = GetOrAddWaterVertex(X, Y, LevelIndex);
			const int32 BottomRight = GetOrAddWaterVertex(X + 1, Y, LevelIndex);
			const int32 TopLeft = GetOrAddWaterVertex(X, Y + 1, LevelIndex);
			const int32 TopRight = GetOrAddWaterVertex(X + 1, Y + 1, LevelIndex);

			AppendQuad(WaterTriangles, BottomLeft, BottomRight, TopLeft, TopRight, false);
		}
	}
}
