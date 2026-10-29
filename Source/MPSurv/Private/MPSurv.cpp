// Copyright Epic Games, Inc. All Rights Reserved.

#include "MPSurv.h"
#include "PCGProceduralMeshSampler.h"
#include "Modules/ModuleManager.h"

class FMPSurvModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		RegisterMPSurvTerrainPointAccessors();
	}

	virtual void ShutdownModule() override
	{
		UnregisterMPSurvTerrainPointAccessors();
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE(FMPSurvModule, MPSurv, "MPSurv");
