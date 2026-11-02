// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "UI/BasePrimaryLayout.h"
#include "UI/BaseToastWidget.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "UIManagerSubsystem.generated.h"

class APlayerController;
class UBaseViewModel;
class UCommonActivatableWidget;
class UGlobalStateViewModel;

/** Creates the primary UI layout for a local player and pushes widgets onto its layers. */
UCLASS()
class MPSURV_API UUIManagerSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void PlayerControllerChanged(APlayerController* NewPlayerController) override;

	/** Creates the root layout, shows it on this local player's screen, and keeps it for later pushes. */
	UFUNCTION(BlueprintCallable, Category = "UI")
	void InitializeLayout(TSubclassOf<UBasePrimaryLayout> LayoutClass);

	/** Adds a view model to the game instance collection so any screen can bind it by context name. */
	UFUNCTION(BlueprintCallable, Category = "UI")
	void RegisterGlobalViewModel(UBaseViewModel* ViewModel, FName ContextName);

	/** Pushes a widget onto a layer of the current layout. */
	UFUNCTION(BlueprintCallable, Category = "UI")
	UCommonActivatableWidget* PushWidget(EUILayer Layer, TSubclassOf<UCommonActivatableWidget> WidgetClass);

	/** Removes a widget that was previously pushed onto the current layout. */
	UFUNCTION(BlueprintCallable, Category = "UI")
	void RemoveWidget(UCommonActivatableWidget* Widget);

	/** Shows the toast immediately, or queues it once MaxVisibleToasts are already on screen. */
	UFUNCTION(BlueprintCallable, Category = "UI")
	void ShowToast(TSubclassOf<UBaseToastWidget> ToastClass, const FText& Message, float Duration);

	/** Releases menu capture and gives the game viewport permanent mouse capture. */
	UFUNCTION(BlueprintCallable, Category = "UI")
	void ApplyGameplayInputMode();

	UFUNCTION(BlueprintPure, Category = "UI")
	UBasePrimaryLayout* GetPrimaryLayout() const { return PrimaryLayout; }

private:
	class UMVVMViewModelCollectionObject* GetGlobalViewModelCollection() const;
	void SpawnToast(TSubclassOf<UBaseToastWidget> ToastClass, const FText& Message, float Duration);
	void ClearPrimaryLayout(bool bApplyGameplayInput);
	void HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	void HandlePostWorldInitialization(UWorld* World, const UWorld::InitializationValues IVS);
	void HandleWorldBeginPlay();
	void TryApplyGameplayInputMode(UWorld* World);
	bool ShouldApplyGameplayInput() const;

	UFUNCTION()
	void HandleToastFinished();

	UFUNCTION()
	void ApplyGameplayInputModeNextTick();

	FDelegateHandle WorldCleanupHandle;
	FDelegateHandle PostWorldInitHandle;
	TArray<TWeakObjectPtr<UWorld>> BeginPlayWorlds;

	/** World the current layout was created in. Used to drop it when that world is cleaned up. */
	TWeakObjectPtr<UWorld> PrimaryLayoutWorld;

	UPROPERTY(Transient)
	TObjectPtr<UBasePrimaryLayout> PrimaryLayout;

	UPROPERTY(Transient)
	TObjectPtr<UGlobalStateViewModel> GlobalStateViewModel;

	UPROPERTY(Transient)
	TArray<FToastData> ToastQueue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Toast", meta = (AllowPrivateAccess = "true", ClampMin = "1"))
	int32 MaxVisibleToasts = 3;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "UI|Toast", meta = (AllowPrivateAccess = "true"))
	int32 CurrentVisibleToasts = 0;
};
