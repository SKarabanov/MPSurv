// Copyright Epic Games, Inc. All Rights Reserved.

#include "MPSurv.h"
#include "PCGProceduralMeshSampler.h"
#include "Modules/ModuleManager.h"

void StartTerrainResidentDiagnostics();
void StopTerrainResidentDiagnostics();

class FMPSurvModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		RegisterMPSurvTerrainPointAccessors();
		StartTerrainResidentDiagnostics();
	}

	virtual void ShutdownModule() override
	{
		StopTerrainResidentDiagnostics();
		UnregisterMPSurvTerrainPointAccessors();
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE(FMPSurvModule, MPSurv, "MPSurv");
