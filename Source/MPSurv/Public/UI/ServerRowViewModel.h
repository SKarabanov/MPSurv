// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "FindSessionsCallbackProxy.h"
#include "UI/BaseViewModel.h"
#include "ServerRowViewModel.generated.h"

/** One advertised session, shaped for a server-browser list entry. */
UCLASS(BlueprintType)
class MPSURV_API UServerRowViewModel : public UBaseViewModel
{
	GENERATED_BODY()

public:
	void SetFromSessionResult(const FBlueprintSessionResult& InSessionResult);
	const FBlueprintSessionResult& GetSessionResult() const { return SessionResult; }

	void SetServerName(const FString& NewServerName);
	FString GetServerName() const { return ServerName; }

	void SetPing(const FString& NewPing);
	FString GetPing() const { return Ping; }

	void SetPlayerCount(const FString& NewPlayerCount);
	FString GetPlayerCount() const { return PlayerCount; }

	UPROPERTY(BlueprintReadWrite, FieldNotify, Getter, Setter, Category = "Server Row")
	FString ServerName;

	UPROPERTY(BlueprintReadWrite, FieldNotify, Getter, Setter, Category = "Server Row")
	FString Ping;

	UPROPERTY(BlueprintReadWrite, FieldNotify, Getter, Setter, Category = "Server Row")
	FString PlayerCount;

private:
	/** Original search result passed to JoinSteamSession. */
	UPROPERTY(BlueprintReadOnly, Category = "Server Row", meta = (AllowPrivateAccess = "true"))
	FBlueprintSessionResult SessionResult;
};
