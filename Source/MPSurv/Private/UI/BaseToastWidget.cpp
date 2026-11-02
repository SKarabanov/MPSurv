// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/BaseToastWidget.h"

#include "Engine/World.h"
#include "TimerManager.h"

void UBaseToastWidget::InitToast(const FText& Message, float Duration)
{
	ToastMessage = Message;

	if (Duration <= 0.f)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	World->GetTimerManager().SetTimer(DismissTimer, this, &UBaseToastWidget::FinishToast, Duration, false);
}

void UBaseToastWidget::FinishToast()
{
	if (bFinished)
	{
		return;
	}

	bFinished = true;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DismissTimer);
	}

	OnToastFinishedDelegate.Broadcast();
	RemoveFromParent();
}
