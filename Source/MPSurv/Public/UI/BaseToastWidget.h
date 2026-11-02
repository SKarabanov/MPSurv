// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BaseToastWidget.generated.h"

class UBaseToastWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnToastFinished);

/** One queued toast: which widget to show, the text, and how long it stays. */
USTRUCT(BlueprintType)
struct FToastData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Toast")
	TSubclassOf<UBaseToastWidget> ToastClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Toast")
	FText Message;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Toast")
	float Duration = 0.f;
};

/** Non-blocking popup. It does not join the activatable widget stack and does not take input focus. */
UCLASS(Abstract, Blueprintable)
class MPSURV_API UBaseToastWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Stores the message and finishes the toast after Duration seconds. */
	UFUNCTION(BlueprintCallable, Category = "UI|Toast")
	void InitToast(const FText& Message, float Duration);

	/** Notifies listeners that this toast is done, then removes it. */
	UFUNCTION(BlueprintCallable, Category = "UI|Toast")
	void FinishToast();

	UFUNCTION(BlueprintPure, Category = "UI|Toast")
	FText GetToastMessage() const { return ToastMessage; }

	UPROPERTY(BlueprintAssignable, Category = "UI|Toast")
	FOnToastFinished OnToastFinishedDelegate;

private:
	UPROPERTY(BlueprintReadOnly, Category = "UI|Toast", meta = (AllowPrivateAccess = "true"))
	FText ToastMessage;

	FTimerHandle DismissTimer;
	bool bFinished = false;
};
