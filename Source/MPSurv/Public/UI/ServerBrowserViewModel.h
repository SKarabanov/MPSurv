// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "FindSessionsCallbackProxy.h"
#include "UI/BaseViewModel.h"
#include "ServerBrowserViewModel.generated.h"

class UServerRowViewModel;
class USteamSessionSubsystem;

/** View model for the server browser: search state and one row per advertised session. */
UCLASS(BlueprintType)
class MPSURV_API UServerBrowserViewModel : public UBaseViewModel
{
	GENERATED_BODY()

public:
	virtual void BeginDestroy() override;

	/** Queries Steam and rebuilds ServerList when the search finishes. */
	UFUNCTION(BlueprintCallable, Category = "Server Browser")
	void RefreshList();

	/** Joins the session stored on the selected row. */
	UFUNCTION(BlueprintCallable, Category = "Server Browser")
	void JoinServer(UServerRowViewModel* SelectedServer);

	void SetIsSearching(bool bNewIsSearching);
	bool IsSearching() const { return bIsSearching; }

	void SetServerList(const TArray<UServerRowViewModel*>& NewServerList);
	const TArray<UServerRowViewModel*>& GetServerList() const { return ServerList; }

	UPROPERTY(BlueprintReadWrite, FieldNotify, Getter = "IsSearching", Setter = "SetIsSearching", Category = "Server Browser")
	bool bIsSearching = false;

	UPROPERTY(BlueprintReadWrite, FieldNotify, Getter, Setter, Category = "Server Browser")
	TArray<UServerRowViewModel*> ServerList;

private:
	UFUNCTION()
	void HandleSessionsFound(const TArray<FBlueprintSessionResult>& Sessions, bool bWasSuccessful);

	USteamSessionSubsystem* GetSessionSubsystem() const;

	TWeakObjectPtr<USteamSessionSubsystem> BoundSessionSubsystem;
};
