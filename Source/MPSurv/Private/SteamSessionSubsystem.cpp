// Fill out your copyright notice in the Description page of Project Settings.

#include "SteamSessionSubsystem.h"

#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Online/OnlineSessionNames.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogSteamSession, Log, All);

#ifndef SEARCH_PRESENCE
#define SEARCH_PRESENCE FName(TEXT("PRESENCESEARCH"))
#endif

USteamSessionSubsystem::USteamSessionSubsystem()
	: CreateSessionCompleteDelegate(FOnCreateSessionCompleteDelegate::CreateUObject(this, &ThisClass::OnCreateSessionComplete))
	, FindSessionsCompleteDelegate(FOnFindSessionsCompleteDelegate::CreateUObject(this, &ThisClass::OnFindSessionsComplete))
	, JoinSessionCompleteDelegate(FOnJoinSessionCompleteDelegate::CreateUObject(this, &ThisClass::OnJoinSessionComplete))
{
}

void USteamSessionSubsystem::Deinitialize()
{
	if (const IOnlineSessionPtr Sessions = GetSessions())
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteHandle);
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteHandle);
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteHandle);
	}

	CreateSessionCompleteHandle.Reset();
	FindSessionsCompleteHandle.Reset();
	JoinSessionCompleteHandle.Reset();
	SessionSearch.Reset();

	Super::Deinitialize();
}

void USteamSessionSubsystem::CreateSteamSession(int32 MaxPlayers)
{
	if (CreateSessionCompleteHandle.IsValid())
	{
		UE_LOG(LogSteamSession, Warning, TEXT("CreateSteamSession ignored because a create is already in progress."));
		return;
	}

	IOnlineSubsystem* OnlineSub = Online::GetSubsystem(GetWorld());
	IOnlineSessionPtr Sessions = OnlineSub ? OnlineSub->GetSessionInterface() : nullptr;
	FUniqueNetIdPtr UserId;
	if (!Sessions.IsValid() || !GetLocalUserNetId(UserId))
	{
		OnSessionCreated.Broadcast(false);
		return;
	}

	FOnlineSessionSettings Settings;
	Settings.bIsLANMatch = false;
	Settings.bUsesPresence = true;
	// Steam rejects the session when presence and lobby flags differ.
	Settings.bUseLobbiesIfAvailable = true;
	Settings.bAllowJoinInProgress = true;
	Settings.bShouldAdvertise = true;
	Settings.NumPublicConnections = MaxPlayers;
	Settings.Set(SETTING_MAPNAME, FString(TEXT("TestTerrain")), EOnlineDataAdvertisementType::ViaOnlineService);

	CreateSessionCompleteHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteDelegate);
	if (!Sessions->CreateSession(*UserId, NAME_GameSession, Settings) && CreateSessionCompleteHandle.IsValid())
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteHandle);
		OnSessionCreated.Broadcast(false);
	}
}

void USteamSessionSubsystem::FindSteamSessions(int32 MaxSearchResults)
{
	if (FindSessionsCompleteHandle.IsValid())
	{
		UE_LOG(LogSteamSession, Warning, TEXT("FindSteamSessions ignored because a search is already in progress."));
		return;
	}

	const IOnlineSessionPtr Sessions = GetSessions();
	FUniqueNetIdPtr UserId;
	if (!Sessions.IsValid() || !GetLocalUserNetId(UserId))
	{
		OnSessionsFound.Broadcast(TArray<FBlueprintSessionResult>(), false);
		return;
	}

	SessionSearch = MakeShared<FOnlineSessionSearch>();
	// AppId 480 is flooded with foreign lobbies; a low UI limit can drop the whole request before our session appears.
	constexpr int32 MinUsefulSearchResults = 10000;
	constexpr int32 ForcedSearchResults = 100000;
	SessionSearch->MaxSearchResults = (MaxSearchResults < MinUsefulSearchResults) ? ForcedSearchResults : MaxSearchResults;
	SessionSearch->bIsLanQuery = false;
	SessionSearch->QuerySettings.Set(SEARCH_PRESENCE, true, EOnlineComparisonOp::Equals);
	// Steam routes advertised sessions through the lobby search when this key is set.
	SessionSearch->QuerySettings.Set(SEARCH_LOBBIES, true, EOnlineComparisonOp::Equals);

	UE_LOG(LogTemp, Warning, TEXT("FindSteamSessions: MaxSearchResults=%d (caller asked %d), LAN=false, SEARCH_PRESENCE+SEARCH_LOBBIES."),
		SessionSearch->MaxSearchResults, MaxSearchResults);

	FindSessionsCompleteHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegate);
	if (!Sessions->FindSessions(*UserId, SessionSearch.ToSharedRef()) && FindSessionsCompleteHandle.IsValid())
	{
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteHandle);
		OnSessionsFound.Broadcast(TArray<FBlueprintSessionResult>(), false);
	}
}

void USteamSessionSubsystem::JoinSteamSession(const FBlueprintSessionResult& SessionToJoin)
{
	if (JoinSessionCompleteHandle.IsValid())
	{
		UE_LOG(LogSteamSession, Warning, TEXT("JoinSteamSession ignored because a join is already in progress."));
		return;
	}

	const IOnlineSessionPtr Sessions = GetSessions();
	FUniqueNetIdPtr UserId;
	if (!Sessions.IsValid() || !GetLocalUserNetId(UserId) || !SessionToJoin.OnlineResult.IsValid())
	{
		OnSessionJoined.Broadcast(false);
		return;
	}

	const FOnlineSessionSearchResult& SearchResult = SessionToJoin.OnlineResult;
	JoinSessionCompleteHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegate);
	if (!Sessions->JoinSession(*UserId, NAME_GameSession, SearchResult) && JoinSessionCompleteHandle.IsValid())
	{
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteHandle);
		OnSessionJoined.Broadcast(false);
	}
}

void USteamSessionSubsystem::OnCreateSessionComplete(FName /*SessionName*/, bool bWasSuccessful)
{
	if (const IOnlineSessionPtr Sessions = GetSessions())
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteHandle);
	}
	CreateSessionCompleteHandle.Reset();

	if (bWasSuccessful)
	{
		OnSessionCreated.Broadcast(true);
		if (UWorld* World = GetWorld())
		{
			World->ServerTravel(TEXT("/Game/MPS/Levels/L_GameWorld?listen"));
		}
		return;
	}

	OnSessionCreated.Broadcast(false);
}

void USteamSessionSubsystem::OnFindSessionsComplete(bool bWasSuccessful)
{
	if (const IOnlineSessionPtr Sessions = GetSessions())
	{
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteHandle);
	}
	FindSessionsCompleteHandle.Reset();

	const int32 RawResultCount = SessionSearch.IsValid() ? SessionSearch->SearchResults.Num() : 0;

	TArray<FBlueprintSessionResult> Results;
	if (bWasSuccessful && SessionSearch.IsValid())
	{
		Results.Reserve(SessionSearch->SearchResults.Num());
		for (const FOnlineSessionSearchResult& Result : SessionSearch->SearchResults)
		{
			FBlueprintSessionResult BlueprintResult;
			BlueprintResult.OnlineResult = Result;
			Results.Add(BlueprintResult);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("OnFindSessionsComplete: bWasSuccessful=%s, RawSearchResults=%d, BroadcastCount=%d"),
		bWasSuccessful ? TEXT("true") : TEXT("false"),
		RawResultCount,
		Results.Num());

	OnSessionsFound.Broadcast(Results, bWasSuccessful);
}

void USteamSessionSubsystem::OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
	const IOnlineSessionPtr SessionInterface = GetSessions();
	if (SessionInterface.IsValid())
	{
		SessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteHandle);
	}
	JoinSessionCompleteHandle.Reset();

	if (Result != EOnJoinSessionCompleteResult::Success || !SessionInterface.IsValid())
	{
		OnSessionJoined.Broadcast(false);
		return;
	}

	FString ConnectString;
	if (!SessionInterface->GetResolvedConnectString(SessionName, ConnectString))
	{
		OnSessionJoined.Broadcast(false);
		return;
	}

	APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PlayerController)
	{
		OnSessionJoined.Broadcast(false);
		return;
	}

	PlayerController->ClientTravel(ConnectString, TRAVEL_Absolute);
	OnSessionJoined.Broadcast(true);
}

bool USteamSessionSubsystem::GetLocalUserNetId(FUniqueNetIdPtr& OutUserId) const
{
	if (const UWorld* World = GetWorld())
	{
		if (const APlayerController* PlayerController = World->GetFirstPlayerController())
		{
			if (const ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
			{
				const FUniqueNetIdRepl PreferredId = LocalPlayer->GetPreferredUniqueNetId();
				if (PreferredId.IsValid())
				{
					OutUserId = PreferredId.GetUniqueNetId();
					if (OutUserId.IsValid())
					{
						return true;
					}
				}
			}
		}
	}

	const IOnlineSubsystem* OnlineSub = Online::GetSubsystem(GetWorld());
	const IOnlineIdentityPtr Identity = OnlineSub ? OnlineSub->GetIdentityInterface() : nullptr;
	if (Identity.IsValid())
	{
		OutUserId = Identity->GetUniquePlayerId(0);
		return OutUserId.IsValid();
	}

	return false;
}

IOnlineSessionPtr USteamSessionSubsystem::GetSessions() const
{
	const IOnlineSubsystem* OnlineSub = Online::GetSubsystem(GetWorld());
	return OnlineSub ? OnlineSub->GetSessionInterface() : nullptr;
}
