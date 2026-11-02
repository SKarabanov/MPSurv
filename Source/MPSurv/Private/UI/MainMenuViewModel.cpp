// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/MainMenuViewModel.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "SteamSessionSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogMainMenu, Log, All);

namespace
{
	constexpr int32 MainMenuMaxPlayers = 4;
	constexpr int32 MainMenuMaxSearchResults = 20;
}

void UMainMenuViewModel::BeginDestroy()
{
	if (USteamSessionSubsystem* SessionSubsystem = BoundSessionSubsystem.Get())
	{
		SessionSubsystem->OnSessionsFound.RemoveDynamic(this, &ThisClass::HandleSessionsFound);
	}

	Super::BeginDestroy();
}

void UMainMenuViewModel::HostGame()
{
	USteamSessionSubsystem* SessionSubsystem = GetSessionSubsystem();
	if (!SessionSubsystem)
	{
		UE_LOG(LogMainMenu, Warning, TEXT("HostGame failed because the Steam session subsystem is unavailable."));
		return;
	}

	SessionSubsystem->CreateSteamSession(MainMenuMaxPlayers);
}

void UMainMenuViewModel::FindGames()
{
	USteamSessionSubsystem* SessionSubsystem = GetSessionSubsystem();
	if (!SessionSubsystem)
	{
		UE_LOG(LogMainMenu, Warning, TEXT("FindGames failed because the Steam session subsystem is unavailable."));
		return;
	}

	if (!SessionSubsystem->OnSessionsFound.IsAlreadyBound(this, &ThisClass::HandleSessionsFound))
	{
		SessionSubsystem->OnSessionsFound.AddDynamic(this, &ThisClass::HandleSessionsFound);
		BoundSessionSubsystem = SessionSubsystem;
	}

	SetIsSearchingForSessions(true);
	SessionSubsystem->FindSteamSessions(MainMenuMaxSearchResults);
}

void UMainMenuViewModel::SetCurrentPlayerName(const FString& NewCurrentPlayerName)
{
	UE_MVVM_SET_PROPERTY_VALUE(CurrentPlayerName, NewCurrentPlayerName);
}

void UMainMenuViewModel::SetIsSearchingForSessions(bool bNewIsSearchingForSessions)
{
	UE_MVVM_SET_PROPERTY_VALUE(bIsSearchingForSessions, bNewIsSearchingForSessions);
}

void UMainMenuViewModel::SetFoundSessions(const TArray<FBlueprintSessionResult>& NewFoundSessions)
{
	FoundSessions = NewFoundSessions;
	UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(FoundSessions);
}

void UMainMenuViewModel::HandleSessionsFound(const TArray<FBlueprintSessionResult>& Sessions, bool bWasSuccessful)
{
	SetIsSearchingForSessions(false);
	SetFoundSessions(bWasSuccessful ? Sessions : TArray<FBlueprintSessionResult>());
}

USteamSessionSubsystem* UMainMenuViewModel::GetSessionSubsystem() const
{
	const UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<USteamSessionSubsystem>() : nullptr;
}
