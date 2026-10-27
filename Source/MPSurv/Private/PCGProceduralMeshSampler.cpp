// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGProceduralMeshSampler.h"

#include "PCGContext.h"
#include "PCGPin.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGSpatialData.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataDomain.h"
#include "ProceduralMeshComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogPCGProceduralMeshSampler, Log, All);

namespace PCGProceduralMeshSamplerPrivate
{
	/** Visual terrain section. This is LOD 0 when the chunk is not also keeping a hidden collision mesh. */
	constexpr int32 TerrainSectionIndex = 0;

	/** Hidden full-resolution terrain. Present when the visible mesh is a coarser LOD. */
	constexpr int32 CollisionSectionIndex = 2;

	const FName BiomeIdAttributeName(TEXT("BiomeId"));

	const FProcMeshSection* FindTerrainSection(UProceduralMeshComponent* Mesh)
	{
		if (!Mesh)
		{
			return nullptr;
		}

		if (const FProcMeshSection* CollisionSection = Mesh->GetProcMeshSection(CollisionSectionIndex))
		{
			if (CollisionSection->ProcVertexBuffer.Num() > 0)
			{
				return CollisionSection;
			}
		}

		const FProcMeshSection* TerrainSection = Mesh->GetProcMeshSection(TerrainSectionIndex);
		if (TerrainSection && TerrainSection->ProcVertexBuffer.Num() > 0)
		{
			return TerrainSection;
		}

		return nullptr;
	}

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
		"Samples vertices of a dynamic procedural mesh. Point Color is the vertex color. BiomeId is the dominant packed biome.");
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
			"Spatial data whose target actor owns the procedural mesh. With no input, the PCG component owner is used."));
	return Pins;
}

TArray<FPCGPinProperties> UPCGProceduralMeshSamplerSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(PCGPinConstants::DefaultOutputLabel, FPCGDataTypeIdentifier{EPCGDataType::Point},
		/*bAllowMultipleConnections=*/true, /*bAllowMultipleData=*/true,
		NSLOCTEXT("PCGProceduralMeshSampler", "OutputPinTooltip",
			"One point per terrain vertex. Color is the vertex color, BiomeId is the dominant biome."));
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

		UProceduralMeshComponent* Mesh = Actor->FindComponentByClass<UProceduralMeshComponent>();
		if (!IsValid(Mesh))
		{
			UE_LOG(LogPCGProceduralMeshSampler, Warning, TEXT("PCG_ProceduralMeshSampler: %s has no procedural mesh."), *Actor->GetName());
			continue;
		}

		const FProcMeshSection* Section = FindTerrainSection(Mesh);
		if (!Section || Section->ProcVertexBuffer.Num() == 0)
		{
			UE_LOG(LogPCGProceduralMeshSampler, Warning, TEXT("PCG_ProceduralMeshSampler: %s has no terrain vertices in section 0 or 2."), *Actor->GetName());
			continue;
		}

		const TArray<FProcMeshVertex> Vertices = Section->ProcVertexBuffer;
		const FTransform MeshToWorld = Mesh->GetComponentTransform();

		UPCGBasePointData* PointData = FPCGContext::NewPointData_AnyThread(Context);
		if (!PointData)
		{
			UE_LOG(LogPCGProceduralMeshSampler, Warning, TEXT("PCG_ProceduralMeshSampler: failed to allocate point data."));
			continue;
		}

		PointData->SetNumPoints(Vertices.Num());
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

		for (int32 VertexIndex = 0; VertexIndex < Vertices.Num(); ++VertexIndex)
		{
			const FProcMeshVertex& Vertex = Vertices[VertexIndex];
			const FLinearColor LinearColor = Vertex.Color.ReinterpretAsLinear();

			Transforms[VertexIndex] = FTransform(MeshToWorld.TransformPosition(Vertex.Position));
			Densities[VertexIndex] = 1.0f;
			Seeds[VertexIndex] = VertexIndex;
			Colors[VertexIndex] = FVector4(LinearColor.R, LinearColor.G, LinearColor.B, LinearColor.A);

			if (ElementsDomain && BiomeAttribute && MetadataEntries.IsValidIndex(VertexIndex))
			{
				MetadataEntries[VertexIndex] = PCGInvalidEntryKey;
				ElementsDomain->InitializeOnSet(MetadataEntries[VertexIndex]);
				BiomeAttribute->SetValue(MetadataEntries[VertexIndex], static_cast<int32>(DecodeDominantBiome(Vertex.Color)));
			}
		}

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef();
		Output.Data = PointData;
		Output.Pin = PCGPinConstants::DefaultOutputLabel;
	}

	return true;
}
