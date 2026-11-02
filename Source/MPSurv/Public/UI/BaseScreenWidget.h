// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "CommonInputModeTypes.h"
#include "Engine/EngineBaseTypes.h"
#include "BaseScreenWidget.generated.h"

/** Base screen pushed onto a CommonUI layer. Input mode and mouse capture come from InputConfig. */
UCLASS(Abstract, Blueprintable)
class MPSURV_API UBaseScreenWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	/** Deactivates this screen so its layer stack shows the previous widget. */
	UFUNCTION(BlueprintCallable, Category = "UI")
	void CloseScreen();

protected:
	virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;
	virtual void NativeOnActivated() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	FUIInputConfig InputConfig = FUIInputConfig(ECommonInputMode::Menu, EMouseCaptureMode::NoCapture);
};
