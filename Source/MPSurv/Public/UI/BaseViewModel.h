// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MVVMViewModelBase.h"
#include "BaseViewModel.generated.h"

/** Shared base for menu and gameplay view models. */
UCLASS(Abstract, Blueprintable)
class MPSURV_API UBaseViewModel : public UMVVMViewModelBase
{
	GENERATED_BODY()
};
