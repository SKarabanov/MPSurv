// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/BaseScreenWidget.h"

void UBaseScreenWidget::CloseScreen()
{
	DeactivateWidget();
}

TOptional<FUIInputConfig> UBaseScreenWidget::GetDesiredInputConfig() const
{
	return InputConfig;
}

void UBaseScreenWidget::NativeOnActivated()
{
	Super::NativeOnActivated();
}

FReply UBaseScreenWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InputConfig.GetInputMode() == ECommonInputMode::Menu)
	{
		return FReply::Handled();
	}

	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}
