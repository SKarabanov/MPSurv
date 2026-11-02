// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/BaseViewModel.h"
#include "GlobalStateViewModel.generated.h"

/** Shared state available to every screen through the global view model collection. */
UCLASS(BlueprintType)
class MPSURV_API UGlobalStateViewModel : public UBaseViewModel
{
	GENERATED_BODY()

public:
	void SetCurrentPlayerName(const FString& NewCurrentPlayerName);
	FString GetCurrentPlayerName() const { return CurrentPlayerName; }

	UPROPERTY(BlueprintReadWrite, FieldNotify, Getter, Setter, Category = "Global State")
	FString CurrentPlayerName;
};
