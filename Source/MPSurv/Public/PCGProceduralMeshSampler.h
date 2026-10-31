// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Data/PCGPointArrayData.h"
#include "PCGSettings.h"

#include "PCGProceduralMeshSampler.generated.h"

/** Point data from the terrain sampler. Exposes $Normal as the point's up axis, which is the vertex normal. */
UCLASS()
class MPSURV_API UMPSurvTerrainPointData : public UPCGPointArrayData
{
	GENERATED_BODY()

public:
	virtual UPCGSpatialData* CopyInternal(FPCGContext* Context) const override;
};

void RegisterMPSurvTerrainPointAccessors();
void UnregisterMPSurvTerrainPointAccessors();

/**
 * PCG sampler that reads ATerrainChunk::GetCachedLOD0Grid, the 65×65 LOD 0 surface.
 * The stock mesh sampler needs cooked collision; this node uses the height cache instead.
 *
 * Vertex colors stay the terrain pair packing: R is the lower biome id, G the higher id, B the weight of G.
 * Point Color stores that color in 0-1. The BiomeId attribute is the dominant id (G when B >= 128, otherwise R).
 * $Normal and the point rotation Z are the vertex normal.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural))
class MPSURV_API UPCGProceduralMeshSamplerSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	UPCGProceduralMeshSamplerSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override;
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual EPCGSettingsType GetType() const override;
#endif

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

protected:
	virtual FPCGElementPtr CreateElement() const override;
};
