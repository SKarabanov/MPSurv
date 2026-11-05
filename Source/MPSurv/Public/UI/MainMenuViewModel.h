// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/BaseViewModel.h"
#include "MainMenuViewModel.generated.h"

class USteamSessionSubsystem;

/** View model for the main menu: local player name and host. Find Game only opens the server browser in UI. */
UCLASS(BlueprintType)
class MPSURV_API UMainMenuViewModel : public UBaseViewModel
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Main Menu")
	void HostGame();

	void SetCurrentPlayerName(const FString& NewCurrentPlayerName);
	FString GetCurrentPlayerName() const { return CurrentPlayerName; }

	UPROPERTY(BlueprintReadWrite, FieldNotify, Getter, Setter, Category = "Main Menu")
	FString CurrentPlayerName;

private:
	USteamSessionSubsystem* GetSessionSubsystem() const;
};
