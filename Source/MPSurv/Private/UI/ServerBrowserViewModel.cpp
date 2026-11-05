// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/ServerBrowserViewModel.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "SteamSessionSubsystem.h"
#include "UI/ServerRowViewModel.h"

DEFINE_LOG_CATEGORY_STATIC(LogServerBrowser, Log, All);

namespace
{
	constexpr int32 ServerBrowserMaxSearchResults = 50;
}

void UServerBrowserViewModel::BeginDestroy()
{
	if (USteamSessionSubsystem* SessionSubsystem = BoundSessionSubsystem.Get())
	{
		SessionSubsystem->OnSessionsFound.RemoveDynamic(this, &ThisClass::HandleSessionsFound);
	}

	Super::BeginDestroy();
}

void UServerBrowserViewModel::RefreshList()
{
	USteamSessionSubsystem* SessionSubsystem = GetSessionSubsystem();
	if (!SessionSubsystem)
	{
		UE_LOG(LogServerBrowser, Warning, TEXT("RefreshList failed because the Steam session subsystem is unavailable."));
		SetIsSearching(false);
		SetServerList(TArray<UServerRowViewModel*>());
		return;
	}

	if (!SessionSubsystem->OnSessionsFound.IsAlreadyBound(this, &ThisClass::HandleSessionsFound))
	{
		SessionSubsystem->OnSessionsFound.AddDynamic(this, &ThisClass::HandleSessionsFound);
		BoundSessionSubsystem = SessionSubsystem;
	}

	SetIsSearching(true);
	UE_LOG(LogServerBrowser, Log, TEXT("RefreshList: starting FindSteamSessions(MaxSearchResults=%d)."), ServerBrowserMaxSearchResults);
	SessionSubsystem->FindSteamSessions(ServerBrowserMaxSearchResults);
}

void UServerBrowserViewModel::JoinServer(UServerRowViewModel* SelectedServer)
{
	if (!SelectedServer)
	{
		UE_LOG(LogServerBrowser, Warning, TEXT("JoinServer ignored because no row was selected."));
		return;
	}

	USteamSessionSubsystem* SessionSubsystem = GetSessionSubsystem();
	if (!SessionSubsystem)
	{
		UE_LOG(LogServerBrowser, Warning, TEXT("JoinServer failed because the Steam session subsystem is unavailable."));
		return;
	}

	SessionSubsystem->JoinSteamSession(SelectedServer->GetSessionResult());
}

void UServerBrowserViewModel::SetIsSearching(bool bNewIsSearching)
{
	UE_MVVM_SET_PROPERTY_VALUE(bIsSearching, bNewIsSearching);
}

void UServerBrowserViewModel::SetServerList(const TArray<UServerRowViewModel*>& NewServerList)
{
	ServerList = NewServerList;
	// UE 5.8: expands to BroadcastFieldValueChanged(FFieldNotificationClassDescriptor::ServerList).
	UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(ServerList);
}

void UServerBrowserViewModel::HandleSessionsFound(const TArray<FBlueprintSessionResult>& Sessions, bool bWasSuccessful)
{
	SetIsSearching(false);

	TArray<UServerRowViewModel*> Rows;
	if (bWasSuccessful)
	{
		Rows.Reserve(Sessions.Num());
		for (const FBlueprintSessionResult& Session : Sessions)
		{
			UServerRowViewModel* Row = NewObject<UServerRowViewModel>(this);
			Row->SetFromSessionResult(Session);
			Rows.Add(Row);
		}
	}

	ServerList = Rows;
	// ListView bindings listen to FieldNotify; without this broadcast the widget keeps the previous empty list.
	UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(ServerList);

	UE_LOG(LogServerBrowser, Log, TEXT("HandleSessionsFound: bWasSuccessful=%s, Rows=%d"),
		bWasSuccessful ? TEXT("true") : TEXT("false"),
		Rows.Num());
}

USteamSessionSubsystem* UServerBrowserViewModel::GetSessionSubsystem() const
{
	const UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<USteamSessionSubsystem>() : nullptr;
}
