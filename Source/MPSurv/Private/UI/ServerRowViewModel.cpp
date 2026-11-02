// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/ServerRowViewModel.h"

#include "Online/OnlineSessionNames.h"
#include "OnlineSessionSettings.h"

void UServerRowViewModel::SetFromSessionResult(const FBlueprintSessionResult& InSessionResult)
{
	SessionResult = InSessionResult;

	const FOnlineSession& Session = InSessionResult.OnlineResult.Session;
	FString ResolvedName = Session.OwningUserName;
	if (ResolvedName.IsEmpty())
	{
		Session.SessionSettings.Get(SETTING_MAPNAME, ResolvedName);
	}
	if (ResolvedName.IsEmpty())
	{
		ResolvedName = TEXT("Unknown");
	}
	SetServerName(ResolvedName);

	const int32 PingInMs = InSessionResult.OnlineResult.PingInMs;
	SetPing(PingInMs >= MAX_QUERY_PING ? FString(TEXT("-")) : FString::FromInt(PingInMs));

	const int32 MaxPlayers = Session.SessionSettings.NumPublicConnections;
	const int32 CurrentPlayers = FMath::Max(0, MaxPlayers - Session.NumOpenPublicConnections);
	SetPlayerCount(FString::Printf(TEXT("%d/%d"), CurrentPlayers, MaxPlayers));
}

void UServerRowViewModel::SetServerName(const FString& NewServerName)
{
	UE_MVVM_SET_PROPERTY_VALUE(ServerName, NewServerName);
}

void UServerRowViewModel::SetPing(const FString& NewPing)
{
	UE_MVVM_SET_PROPERTY_VALUE(Ping, NewPing);
}

void UServerRowViewModel::SetPlayerCount(const FString& NewPlayerCount)
{
	UE_MVVM_SET_PROPERTY_VALUE(PlayerCount, NewPlayerCount);
}
