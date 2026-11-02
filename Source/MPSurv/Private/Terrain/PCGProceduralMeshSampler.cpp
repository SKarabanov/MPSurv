// Copyright Epic Games, Inc. All Rights Reserved.

#include "Terrain/PCGProceduralMeshSampler.h"

#include "PCGContext.h"
#include "PCGPin.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGSpatialData.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataDomain.h"
#include "Metadata/Accessors/PCGAttributeAccessorFactory.h"
#include "Metadata/Accessors/PCGCustomAccessor.h"
#include "Modules/ModuleManager.h"
#include "Terrain/TerrainChunk.h"

DEFINE_LOG_CATEGORY_STATIC(LogPCGProceduralMeshSampler, Log, All);

namespace PCGProceduralMeshSamplerPrivate
{
	const FName BiomeIdAttributeName(TEXT("BiomeId"));
	const FName NormalPropertyName(TEXT("Normal"));

	void CollectActors(const FPCGContext* Context, TArray<AActor*>& OutActors)
	{
		const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
		for (const FPCGTaggedData& Input : Inputs)
		{
			const UPCGSpatialData* SpatialData = Cast<UPCGSpatialData>(Input.Data);
			if (SpatialData && SpatialData->TargetActor.IsValid())
			{
				OutActors.AddUnique(SpatialData->TargetActor.Get());
			}
		}

		if (OutActors.Num() == 0)
		{
			if (AActor* Owner = Context->GetTypedExecutionTarget<AActor>())
			{
				OutActors.Add(Owner);
			}
		}
	}

	uint8 DecodeDominantBiome(const FColor& VertexColor)
	{
		return VertexColor.B >= 128 ? VertexColor.G : VertexColor.R;
	}

	TUniquePtr<const IPCGAttributeAccessor> CreateNormalAccessor(const UPCGBasePointData* PointData)
	{
		return MakeUnique<FPCGCustomPointPropertyAccessor<FVector, FPCGPointTransform::ConstValueRange>>(
			PointData,
			[](int32 Index, FVector& OutValue, const FPCGPointTransform::ConstValueRange& Transform)
			{
				OutValue = Transform[Index].GetUnitAxis(EAxis::Z);
				return true;
			},
			PointData->GetConstTransformValueRange());
	}
}

class FPCGProceduralMeshSamplerElement : public IPCGElement
{
protected:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override
	{
		return true;
	}

	virtual bool IsCacheable(const UPCGSettings* InSettings) const override
	{
		return false;
	}

	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};

UPCGProceduralMeshSamplerSettings::UPCGProceduralMeshSamplerSettings()
{
#if WITH_EDITOR
	bExposeToLibrary = true;
	Category = NSLOCTEXT("PCGProceduralMeshSampler", "Category", "Sampler");
#endif
}

#if WITH_EDITOR
FName UPCGProceduralMeshSamplerSettings::GetDefaultNodeName() const
{
	return TEXT("PCG_ProceduralMeshSampler");
}

FText UPCGProceduralMeshSamplerSettings::GetDefaultNodeTitle() const
{
	return NSLOCTEXT("PCGProceduralMeshSampler", "NodeTitle", "PCG_ProceduralMeshSampler");
}

FText UPCGProceduralMeshSamplerSettings::GetNodeTooltipText() const
{
	return NSLOCTEXT("PCGProceduralMeshSampler", "NodeTooltip",
		"Samples the 65x65 LOD 0 terrain grid cached on the chunk. $Normal is the vertex normal, and the point rotation Z matches it. BiomeId is the dominant packed biome.");
}

EPCGSettingsType UPCGProceduralMeshSamplerSettings::GetType() const
{
	return EPCGSettingsType::Sampler;
}
#endif

TArray<FPCGPinProperties> UPCGProceduralMeshSamplerSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(PCGPinConstants::DefaultInputLabel, FPCGDataTypeIdentifier{EPCGDataType::Spatial},
		/*bAllowMultipleConnections=*/true, /*bAllowMultipleData=*/true,
		NSLOCTEXT("PCGProceduralMeshSampler", "InputPinTooltip",
			"Spatial data whose target actor is a terrain chunk. With no input, the PCG component owner is used."));
	return Pins;
}

TArray<FPCGPinProperties> UPCGProceduralMeshSamplerSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(PCGPinConstants::DefaultOutputLabel, FPCGDataTypeIdentifier{EPCGDataType::Point},
		/*bAllowMultipleConnections=*/true, /*bAllowMultipleData=*/true,
		NSLOCTEXT("PCGProceduralMeshSampler", "OutputPinTooltip",
			"One point per LOD 0 grid node. Normal is the vertex normal, and the point rotation Z matches it. BiomeId is the dominant biome."));
	return Pins;
}

FPCGElementPtr UPCGProceduralMeshSamplerSettings::CreateElement() const
{
	return MakeShared<FPCGProceduralMeshSamplerElement>();
}

bool FPCGProceduralMeshSamplerElement::ExecuteInternal(FPCGContext* Context) const
{
	using namespace PCGProceduralMeshSamplerPrivate;

	if (!Context || !IsInGameThread())
	{
		return true;
	}

	TArray<AActor*> Actors;
	CollectActors(Context, Actors);
	if (Actors.Num() == 0)
	{
		UE_LOG(LogPCGProceduralMeshSampler, Warning, TEXT("PCG_ProceduralMeshSampler: no actor on the input or on the PCG component."));
		return true;
	}

	for (AActor* Actor : Actors)
	{
		if (!IsValid(Actor))
		{
			continue;
		}

		ATerrainChunk* Chunk = Cast<ATerrainChunk>(Actor);
		if (!Chunk)
		{
			UE_LOG(LogPCGProceduralMeshSampler, Warning, TEXT("PCG_ProceduralMeshSampler: %s is not a terrain chunk."), *Actor->GetName());
			continue;
		}

		const TArray<FTerrainGridSample>& Grid = Chunk->GetCachedLOD0Grid();
		const int32 Side = ATerrainChunk::QuadsPerSide + 1;
		if (Grid.Num() != Side * Side)
		{
			UE_LOG(LogPCGProceduralMeshSampler, Warning, TEXT("PCG_ProceduralMeshSampler: %s has no cached LOD 0 grid."), *Actor->GetName());
			continue;
		}

		const int32 SurfaceCount = Grid.Num();
		const FTransform MeshToWorld = Chunk->GetActorTransform();

		UPCGBasePointData* PointData = FPCGContext::NewObject_AnyThread<UMPSurvTerrainPointData>(Context);
		if (!PointData)
		{
			UE_LOG(LogPCGProceduralMeshSampler, Warning, TEXT("PCG_ProceduralMeshSampler: failed to allocate point data."));
			continue;
		}

		PointData->SetNumPoints(SurfaceCount);
		PointData->AllocateProperties(EPCGPointNativeProperties::All);

		UPCGMetadata* Metadata = PointData->MutableMetadata();
		FPCGMetadataDomain* ElementsDomain = Metadata ? Metadata->GetMetadataDomain(PCGMetadataDomainID::Elements) : nullptr;
		FPCGMetadataAttribute<int32>* BiomeAttribute = nullptr;
		if (ElementsDomain)
		{
			BiomeAttribute = ElementsDomain->FindOrCreateAttribute<int32>(BiomeIdAttributeName, 0, /*bAllowsInterpolation=*/false, /*bOverrideParent=*/true);
		}

		if (!BiomeAttribute)
		{
			UE_LOG(LogPCGProceduralMeshSampler, Warning, TEXT("PCG_ProceduralMeshSampler: failed to create the BiomeId attribute."));
		}

		TPCGValueRange<FTransform> Transforms = PointData->GetTransformValueRange(/*bAllocate=*/true);
		TPCGValueRange<float> Densities = PointData->GetDensityValueRange(/*bAllocate=*/true);
		TPCGValueRange<int32> Seeds = PointData->GetSeedValueRange(/*bAllocate=*/true);
		TPCGValueRange<FVector4> Colors = PointData->GetColorValueRange(/*bAllocate=*/true);
		TPCGValueRange<int64> MetadataEntries = PointData->GetMetadataEntryValueRange(/*bAllocate=*/true);

		for (int32 VertexIndex = 0; VertexIndex < SurfaceCount; ++VertexIndex)
		{
			const FTerrainGridSample& Sample = Grid[VertexIndex];
			const int32 GridX = VertexIndex % Side;
			const int32 GridY = VertexIndex / Side;
			const FVector LocalPosition(static_cast<double>(GridX) * ATerrainChunk::CellSize, static_cast<double>(GridY) * ATerrainChunk::CellSize, Sample.Height);
			const FLinearColor LinearColor = Sample.VertexColor.ReinterpretAsLinear();

			FVector WorldNormal = MeshToWorld.TransformVectorNoScale(Sample.Normal).GetSafeNormal();
			if (WorldNormal.IsNearlyZero())
			{
				WorldNormal = FVector::UpVector;
			}

			const FVector WorldPosition = MeshToWorld.TransformPosition(LocalPosition);
			Transforms[VertexIndex] = FTransform(FRotationMatrix::MakeFromZ(WorldNormal).ToQuat(), WorldPosition);
			Densities[VertexIndex] = 1.0f;
			Seeds[VertexIndex] = VertexIndex;
			Colors[VertexIndex] = FVector4(LinearColor.R, LinearColor.G, LinearColor.B, LinearColor.A);

			if (ElementsDomain && MetadataEntries.IsValidIndex(VertexIndex))
			{
				MetadataEntries[VertexIndex] = PCGInvalidEntryKey;
				ElementsDomain->InitializeOnSet(MetadataEntries[VertexIndex]);
				if (BiomeAttribute)
				{
					BiomeAttribute->SetValue(MetadataEntries[VertexIndex], static_cast<int32>(DecodeDominantBiome(Sample.VertexColor)));
				}
			}
		}

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef();
		Output.Data = PointData;
		Output.Pin = PCGPinConstants::DefaultOutputLabel;
	}

	return true;
}

UPCGSpatialData* UMPSurvTerrainPointData::CopyInternal(FPCGContext* Context) const
{
	UMPSurvTerrainPointData* NewPointData = FPCGContext::NewObject_AnyThread<UMPSurvTerrainPointData>(Context);
	if (NewPointData && !SupportsSpatialDataInheritance())
	{
		const int32 NumPoints = GetNumPoints();
		NewPointData->SetNumPoints(NumPoints, /*bInitializeValues=*/false);
		CopyPropertiesTo(NewPointData, 0, 0, NumPoints, EPCGPointNativeProperties::All);
	}

	return NewPointData;
}

void RegisterMPSurvTerrainPointAccessors()
{
	FModuleManager::Get().LoadModuleChecked(TEXT("PCG"));

	FPCGAttributeAccessorMethods Methods;
	Methods.CreateConstAccessorFunc = [](const UPCGData* InData, const FPCGAttributePropertySelector& InSelector, bool) -> TUniquePtr<const IPCGAttributeAccessor>
	{
		if (InSelector.GetSelection() != EPCGAttributePropertySelection::Property
			|| InSelector.GetPropertyName() != PCGProceduralMeshSamplerPrivate::NormalPropertyName)
		{
			return {};
		}

		const UPCGBasePointData* PointData = Cast<UPCGBasePointData>(InData);
		return PointData ? PCGProceduralMeshSamplerPrivate::CreateNormalAccessor(PointData) : TUniquePtr<const IPCGAttributeAccessor>();
	};

	FPCGAttributeAccessorFactory::GetMutableInstance().RegisterMethods<UMPSurvTerrainPointData>(MoveTemp(Methods));
}

void UnregisterMPSurvTerrainPointAccessors()
{
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("PCG")))
	{
		return;
	}

	FPCGAttributeAccessorFactory::GetMutableInstance().UnregisterMethods<UMPSurvTerrainPointData>();
}
