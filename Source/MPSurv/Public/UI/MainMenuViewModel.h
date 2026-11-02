// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/BaseViewModel.h"
#include "FindSessionsCallbackProxy.h"
#include "MainMenuViewModel.generated.h"

class USteamSessionSubsystem;

/** View model for the main menu: local player name, session search state, and found Steam sessions. */
UCLASS(BlueprintType)
class MPSURV_API UMainMenuViewModel : public UBaseViewModel
{
	GENERATED_BODY()

public:
	virtual void BeginDestroy() override;

	UFUNCTION(BlueprintCallable, Category = "Main Menu")
	void HostGame();

	UFUNCTION(BlueprintCallable, Category = "Main Menu")
	void FindGames();

	void SetCurrentPlayerName(const FString& NewCurrentPlayerName);
	FString GetCurrentPlayerName() const { return CurrentPlayerName; }

	void SetIsSearchingForSessions(bool bNewIsSearchingForSessions);
	bool IsSearchingForSessions() const { return bIsSearchingForSessions; }

	void SetFoundSessions(const TArray<FBlueprintSessionResult>& NewFoundSessions);
	TArray<FBlueprintSessionResult> GetFoundSessions() const { return FoundSessions; }

	UPROPERTY(BlueprintReadWrite, FieldNotify, Getter, Setter, Category = "Main Menu")
	FString CurrentPlayerName;

	UPROPERTY(BlueprintReadWrite, FieldNotify, Getter = "IsSearchingForSessions", Setter = "SetIsSearchingForSessions", Category = "Main Menu")
	bool bIsSearchingForSessions = false;

	UPROPERTY(BlueprintReadWrite, FieldNotify, Getter, Setter, Category = "Main Menu")
	TArray<FBlueprintSessionResult> FoundSessions;

private:
	UFUNCTION()
	void HandleSessionsFound(const TArray<FBlueprintSessionResult>& Sessions, bool bWasSuccessful);

	USteamSessionSubsystem* GetSessionSubsystem() const;

	TWeakObjectPtr<USteamSessionSubsystem> BoundSessionSubsystem;
};
