// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/UIManagerSubsystem.h"

#include "UI/BaseToastWidget.h"
#include "CommonInputModeTypes.h"
#include "Components/VerticalBox.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "Input/CommonUIActionRouterBase.h"
#include "Input/UIActionBindingHandle.h"
#include "UI/GlobalStateViewModel.h"
#include "MVVMGameSubsystem.h"
#include "Subsystems/SubsystemCollection.h"
#include "Types/MVVMViewModelContext.h"

DEFINE_LOG_CATEGORY_STATIC(LogUIManager, Log, All);

namespace
{
	const FName GlobalStateContextName(TEXT("GlobalState"));
}

void UUIManagerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &ThisClass::HandleWorldCleanup);
	// UE 5.8 has no FWorldDelegates::OnWorldBeginPlay. Begin play is broadcast per world.
	PostWorldInitHandle = FWorldDelegates::OnPostWorldInitialization.AddUObject(this, &ThisClass::HandlePostWorldInitialization);
	if (UWorld* World = GetWorld())
	{
		HandlePostWorldInitialization(World, UWorld::InitializationValues());
	}
}

void UUIManagerSubsystem::Deinitialize()
{
	FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
	WorldCleanupHandle.Reset();
	FWorldDelegates::OnPostWorldInitialization.Remove(PostWorldInitHandle);
	PostWorldInitHandle.Reset();
	for (const TWeakObjectPtr<UWorld>& WorldPtr : BeginPlayWorlds)
	{
		if (UWorld* World = WorldPtr.Get())
		{
			World->OnWorldBeginPlay.RemoveAll(this);
		}
	}
	BeginPlayWorlds.Reset();

	if (GlobalStateViewModel)
	{
		if (UMVVMViewModelCollectionObject* Collection = GetGlobalViewModelCollection())
		{
			FMVVMViewModelContext Context;
			Context.ContextClass = GlobalStateViewModel->GetClass();
			Context.ContextName = GlobalStateContextName;
			Collection->RemoveViewModel(Context);
		}
		GlobalStateViewModel = nullptr;
	}

	ClearPrimaryLayout(false);

	Super::Deinitialize();
}

void UUIManagerSubsystem::PlayerControllerChanged(APlayerController* NewPlayerController)
{
	Super::PlayerControllerChanged(NewPlayerController);
	if (!NewPlayerController)
	{
		return;
	}

	TryApplyGameplayInputMode(NewPlayerController->GetWorld());
}

void UUIManagerSubsystem::InitializeLayout(TSubclassOf<UBasePrimaryLayout> LayoutClass)
{
	if (!GlobalStateViewModel)
	{
		GlobalStateViewModel = NewObject<UGlobalStateViewModel>(this);
		RegisterGlobalViewModel(GlobalStateViewModel, GlobalStateContextName);
	}

	if (PrimaryLayout)
	{
		ClearPrimaryLayout(true);
	}

	if (!LayoutClass)
	{
		UE_LOG(LogUIManager, Warning, TEXT("InitializeLayout ignored because LayoutClass is null."));
		return;
	}

	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	APlayerController* PlayerController = LocalPlayer ? LocalPlayer->GetPlayerController(GetWorld()) : nullptr;
	if (!PlayerController)
	{
		UE_LOG(LogUIManager, Warning, TEXT("InitializeLayout failed because the local player has no player controller."));
		return;
	}

	PrimaryLayout = CreateWidget<UBasePrimaryLayout>(PlayerController, LayoutClass);
	if (!PrimaryLayout)
	{
		return;
	}

	PrimaryLayout->AddToPlayerScreen();
	PrimaryLayoutWorld = PrimaryLayout->GetWorld();
	if (!PrimaryLayout->HasActiveMenu())
	{
		ApplyGameplayInputMode();
	}
}

void UUIManagerSubsystem::ApplyGameplayInputMode()
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	if (!LocalPlayer)
	{
		return;
	}

	if (UCommonUIActionRouterBase* ActionRouter = ULocalPlayer::GetSubsystem<UCommonUIActionRouterBase>(LocalPlayer))
	{
		const FUIInputConfig GameConfig(
			ECommonInputMode::Game,
			EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown,
			EMouseLockMode::LockOnCapture,
			true);
		ActionRouter->SetActiveUIInputConfig(GameConfig);
	}

	if (UGameViewportClient* ViewportClient = LocalPlayer->ViewportClient)
	{
		ViewportClient->SetMouseCaptureMode(EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown);
		ViewportClient->SetHideCursorDuringCapture(true);
		ViewportClient->SetMouseLockMode(EMouseLockMode::LockOnCapture);
	}

	if (APlayerController* PlayerController = LocalPlayer->GetPlayerController(GetWorld()))
	{
		PlayerController->bShowMouseCursor = false;
		PlayerController->ResetIgnoreLookInput();
		PlayerController->ResetIgnoreMoveInput();
		PlayerController->SetInputMode(FInputModeGameOnly());
	}

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

void UUIManagerSubsystem::ApplyGameplayInputModeNextTick()
{
	if (ShouldApplyGameplayInput())
	{
		ApplyGameplayInputMode();
	}
}

void UUIManagerSubsystem::TryApplyGameplayInputMode(UWorld* World)
{
	if (!ShouldApplyGameplayInput())
	{
		return;
	}

	ApplyGameplayInputMode();

	if (World)
	{
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &ThisClass::ApplyGameplayInputModeNextTick));
	}
}

bool UUIManagerSubsystem::ShouldApplyGameplayInput() const
{
	return !PrimaryLayout || !PrimaryLayout->HasActiveWidget();
}

void UUIManagerSubsystem::ClearPrimaryLayout(bool bApplyGameplayInput)
{
	ToastQueue.Empty();
	CurrentVisibleToasts = 0;

	if (PrimaryLayout)
	{
		PrimaryLayout->ClearAllStacks();
		PrimaryLayout->RemoveFromParent();
		PrimaryLayout = nullptr;
	}
	PrimaryLayoutWorld.Reset();

	if (bApplyGameplayInput)
	{
		ApplyGameplayInputMode();
	}
}

void UUIManagerSubsystem::HandleWorldCleanup(UWorld* World, bool /*bSessionEnded*/, bool /*bCleanupResources*/)
{
	if (!PrimaryLayout || PrimaryLayoutWorld.Get() != World)
	{
		return;
	}

	ClearPrimaryLayout(false);
}

void UUIManagerSubsystem::HandlePostWorldInitialization(UWorld* World, const UWorld::InitializationValues /*IVS*/)
{
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	World->OnWorldBeginPlay.RemoveAll(this);
	World->OnWorldBeginPlay.AddUObject(this, &ThisClass::HandleWorldBeginPlay);
	BeginPlayWorlds.AddUnique(World);
}

void UUIManagerSubsystem::HandleWorldBeginPlay()
{
	TryApplyGameplayInputMode(GetWorld());
}

void UUIManagerSubsystem::RegisterGlobalViewModel(UBaseViewModel* ViewModel, FName ContextName)
{
	UMVVMViewModelCollectionObject* Collection = GetGlobalViewModelCollection();
	if (!ViewModel || ContextName.IsNone() || !Collection)
	{
		UE_LOG(LogUIManager, Warning, TEXT("RegisterGlobalViewModel failed for context %s."), *ContextName.ToString());
		return;
	}

	FMVVMViewModelContext Context;
	Context.ContextClass = ViewModel->GetClass();
	Context.ContextName = ContextName;
	if (!Collection->AddViewModelInstance(Context, ViewModel))
	{
		UE_LOG(LogUIManager, Warning, TEXT("View model context %s is already registered."), *ContextName.ToString());
	}
}

UMVVMViewModelCollectionObject* UUIManagerSubsystem::GetGlobalViewModelCollection() const
{
	const UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UMVVMGameSubsystem* ViewModelSubsystem = GameInstance ? GameInstance->GetSubsystem<UMVVMGameSubsystem>() : nullptr;
	return ViewModelSubsystem ? ViewModelSubsystem->GetViewModelCollection() : nullptr;
}

UCommonActivatableWidget* UUIManagerSubsystem::PushWidget(EUILayer Layer, TSubclassOf<UCommonActivatableWidget> WidgetClass)
{
	return PrimaryLayout ? PrimaryLayout->PushWidgetToLayer(Layer, WidgetClass) : nullptr;
}

void UUIManagerSubsystem::RemoveWidget(UCommonActivatableWidget* Widget)
{
	if (PrimaryLayout)
	{
		PrimaryLayout->RemoveWidget(Widget);
	}
}

void UUIManagerSubsystem::ShowToast(TSubclassOf<UBaseToastWidget> ToastClass, const FText& Message, float Duration)
{
	if (!ToastClass)
	{
		UE_LOG(LogUIManager, Warning, TEXT("ShowToast ignored because the toast class is null."));
		return;
	}

	if (CurrentVisibleToasts < MaxVisibleToasts)
	{
		SpawnToast(ToastClass, Message, Duration);
		return;
	}

	FToastData QueuedToast;
	QueuedToast.ToastClass = ToastClass;
	QueuedToast.Message = Message;
	QueuedToast.Duration = Duration;
	ToastQueue.Add(QueuedToast);
}

void UUIManagerSubsystem::SpawnToast(TSubclassOf<UBaseToastWidget> ToastClass, const FText& Message, float Duration)
{
	UVerticalBox* ToastContainer = PrimaryLayout ? PrimaryLayout->GetToastContainer() : nullptr;
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	APlayerController* PlayerController = LocalPlayer ? LocalPlayer->GetPlayerController(GetWorld()) : nullptr;
	if (!ToastClass || !ToastContainer || !PlayerController)
	{
		UE_LOG(LogUIManager, Warning, TEXT("SpawnToast failed because the toast class, container, or player controller is missing."));
		return;
	}

	UBaseToastWidget* CreatedToast = CreateWidget<UBaseToastWidget>(PlayerController, ToastClass);
	if (!CreatedToast)
	{
		return;
	}

	++CurrentVisibleToasts;
	CreatedToast->OnToastFinishedDelegate.AddDynamic(this, &UUIManagerSubsystem::HandleToastFinished);
	CreatedToast->InitToast(Message, Duration);
	ToastContainer->AddChild(CreatedToast);
}

void UUIManagerSubsystem::HandleToastFinished()
{
	if (CurrentVisibleToasts > 0)
	{
		--CurrentVisibleToasts;
	}

	if (ToastQueue.Num() == 0 || !PrimaryLayout)
	{
		return;
	}

	const FToastData NextToast = ToastQueue[0];
	ToastQueue.RemoveAt(0);
	SpawnToast(NextToast.ToastClass, NextToast.Message, NextToast.Duration);
}
