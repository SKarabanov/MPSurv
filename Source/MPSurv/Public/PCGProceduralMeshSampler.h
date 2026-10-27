// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGSettings.h"

#include "PCGProceduralMeshSampler.generated.h"

/**
 * PCG sampler that reads a dynamic procedural mesh. The stock mesh sampler needs cooked collision;
 * terrain chunks keep their surface in UProceduralMeshComponent sections.
 *
 * Vertex colors stay the terrain pair packing: R is the lower biome id, G the higher id, B the weight of G.
 * Point Color stores that color in 0-1. The BiomeId attribute is the dominant id (G when B >= 128, otherwise R).
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
