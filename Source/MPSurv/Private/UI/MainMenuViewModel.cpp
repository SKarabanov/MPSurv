// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/MainMenuViewModel.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "SteamSessionSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogMainMenu, Log, All);

namespace
{
	constexpr int32 MainMenuMaxPlayers = 4;
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

void UMainMenuViewModel::SetCurrentPlayerName(const FString& NewCurrentPlayerName)
{
	UE_MVVM_SET_PROPERTY_VALUE(CurrentPlayerName, NewCurrentPlayerName);
}

USteamSessionSubsystem* UMainMenuViewModel::GetSessionSubsystem() const
{
	const UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<USteamSessionSubsystem>() : nullptr;
}
