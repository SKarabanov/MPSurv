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

const FName USteamSessionSubsystem::MPSURV_SESSION_KEY = FName(TEXT("MPSurvGameKey"));
const int32 USteamSessionSubsystem::MPSURV_SESSION_VALUE = 1;

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
	// Required for k_ELobbyTypePublic when invites are enabled; without presence join Steam OSS
	// still returns Public with invites=false, but discovery/join-via-presence paths stay broken.
	Settings.bAllowJoinViaPresence = true;
	Settings.bAllowJoinViaPresenceFriendsOnly = false;
	Settings.bAllowInvites = true;
	Settings.NumPublicConnections = FMath::Max(MaxPlayers, 2);
	Settings.Set(SETTING_MAPNAME, FString(TEXT("L_GameWorld")), EOnlineDataAdvertisementType::ViaOnlineService);
	// Numerical lobby filter is reliable on AppId 480; string filters often return 0 matches.
	Settings.Set(MPSURV_SESSION_KEY, MPSURV_SESSION_VALUE, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	UE_LOG(LogTemp, Warning, TEXT("CreateSteamSession: MaxPlayers=%d, advertise=true, presenceJoin=true, filter=%s=%d"),
		Settings.NumPublicConnections, *MPSURV_SESSION_KEY.ToString(), MPSURV_SESSION_VALUE);

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
	// Steam RequestLobbyList hard-caps around 50; larger values do not help AppId 480 discovery.
	constexpr int32 SteamLobbyResultCap = 50;
	SessionSearch->MaxSearchResults = FMath::Clamp(MaxSearchResults > 0 ? MaxSearchResults : SteamLobbyResultCap, 1, SteamLobbyResultCap);
	SessionSearch->bIsLanQuery = false;
	SessionSearch->QuerySettings.Set(SEARCH_PRESENCE, true, EOnlineComparisonOp::Equals);
	// Steam routes advertised sessions through the lobby search when this key is set.
	SessionSearch->QuerySettings.Set(SEARCH_LOBBIES, true, EOnlineComparisonOp::Equals);
	SessionSearch->QuerySettings.Set(MPSURV_SESSION_KEY, MPSURV_SESSION_VALUE, EOnlineComparisonOp::Equals);

	UE_LOG(LogTemp, Warning, TEXT("FindSteamSessions: MaxSearchResults=%d (caller asked %d), LAN=false, filter=%s=%d"),
		SessionSearch->MaxSearchResults, MaxSearchResults, *MPSURV_SESSION_KEY.ToString(), MPSURV_SESSION_VALUE);

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

void USteamSessionSubsystem::OnCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
	const IOnlineSessionPtr Sessions = GetSessions();
	if (Sessions.IsValid())
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteHandle);
	}
	CreateSessionCompleteHandle.Reset();

	if (!bWasSuccessful || !Sessions.IsValid())
	{
		OnSessionCreated.Broadcast(false);
		return;
	}

	// Move Pending -> InProgress so the lobby is treated as an active joinable match.
	if (!Sessions->StartSession(SessionName))
	{
		UE_LOG(LogSteamSession, Warning, TEXT("StartSession(%s) failed after create; continuing with travel."), *SessionName.ToString());
	}

	if (const FNamedOnlineSession* NamedSession = Sessions->GetNamedSession(SessionName))
	{
		UE_LOG(LogTemp, Warning, TEXT("CreateSteamSession complete: SessionState=%d Id=%s"),
			static_cast<int32>(NamedSession->SessionState),
			NamedSession->SessionInfo.IsValid() ? *NamedSession->SessionInfo->GetSessionId().ToString() : TEXT("invalid"));
	}

	OnSessionCreated.Broadcast(true);
	if (UWorld* World = GetWorld())
	{
		World->ServerTravel(TEXT("/Game/MPS/Levels/L_GameWorld?listen"));
	}
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
