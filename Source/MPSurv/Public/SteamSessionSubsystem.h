// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "FindSessionsCallbackProxy.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SteamSessionSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSteamSessionCreatedDelegate, bool, bWasSuccessful);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSteamSessionsFoundDelegate, const TArray<FBlueprintSessionResult>&, Sessions, bool, bWasSuccessful);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSteamSessionJoinedDelegate, bool, bWasSuccessful);

/** Creates, searches, and joins Steam game sessions for the lifetime of the game instance. */
UCLASS()
class MPSURV_API USteamSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	USteamSessionSubsystem();

	virtual void Deinitialize() override;

	/** Lobby metadata key so AppId 480 searches return only MPSurv sessions. */
	static const FName MPSURV_SESSION_KEY;

	/** Fired when Steam finishes creating the session. bWasSuccessful is false when the request is rejected. */
	UPROPERTY(BlueprintAssignable, Category = "Steam|Session")
	FSteamSessionCreatedDelegate OnSessionCreated;

	/** Fired when a search finishes. Sessions is empty when bWasSuccessful is false. */
	UPROPERTY(BlueprintAssignable, Category = "Steam|Session")
	FSteamSessionsFoundDelegate OnSessionsFound;

	/** Fired after a join attempt. On success the local player travels to the host. */
	UPROPERTY(BlueprintAssignable, Category = "Steam|Session")
	FSteamSessionJoinedDelegate OnSessionJoined;

	/** Creates a public Steam session, then opens TestTerrain as a listen server. */
	UFUNCTION(BlueprintCallable, Category = "Steam|Session")
	void CreateSteamSession(int32 MaxPlayers);

	/** Queries public Steam lobbies. */
	UFUNCTION(BlueprintCallable, Category = "Steam|Session")
	void FindSteamSessions(int32 MaxSearchResults);

	/** Joins a result previously returned by OnSessionsFound. */
	UFUNCTION(BlueprintCallable, Category = "Steam|Session")
	void JoinSteamSession(const FBlueprintSessionResult& SessionToJoin);

private:
	void OnCreateSessionComplete(FName SessionName, bool bWasSuccessful);
	void OnFindSessionsComplete(bool bWasSuccessful);
	void OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);

	bool GetLocalUserNetId(FUniqueNetIdPtr& OutUserId) const;
	IOnlineSessionPtr GetSessions() const;

	FOnCreateSessionCompleteDelegate CreateSessionCompleteDelegate;
	FOnFindSessionsCompleteDelegate FindSessionsCompleteDelegate;
	FOnJoinSessionCompleteDelegate JoinSessionCompleteDelegate;

	FDelegateHandle CreateSessionCompleteHandle;
	FDelegateHandle FindSessionsCompleteHandle;
	FDelegateHandle JoinSessionCompleteHandle;

	TSharedPtr<FOnlineSessionSearch> SessionSearch;
};
