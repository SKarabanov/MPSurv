// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/BasePrimaryLayout.h"

#include "CommonActivatableWidget.h"
#include "Widgets/CommonActivatableWidgetContainer.h"

void UBasePrimaryLayout::NativeConstruct()
{
	Super::NativeConstruct();

	RegisterLayer(EUILayer::Game, GameStack);
	RegisterLayer(EUILayer::GameMenu, GameMenuStack);
	RegisterLayer(EUILayer::Menu, MenuStack);
	RegisterLayer(EUILayer::Modal, ModalStack);
}

void UBasePrimaryLayout::RegisterLayer(EUILayer Layer, UCommonActivatableWidgetStack* Stack)
{
	if (Stack)
	{
		LayerStacks.Add(Layer, Stack);
	}
}

UCommonActivatableWidget* UBasePrimaryLayout::PushWidgetToLayer(EUILayer Layer, TSubclassOf<UCommonActivatableWidget> WidgetClass)
{
	if (UCommonActivatableWidgetStack* Stack = LayerStacks.FindRef(Layer))
	{
		return Stack->AddWidget<UCommonActivatableWidget>(WidgetClass);
	}

	return nullptr;
}

void UBasePrimaryLayout::ClearAllStacks()
{
	UCommonActivatableWidgetStack* Stacks[] = { GameStack, GameMenuStack, MenuStack, ModalStack };
	for (UCommonActivatableWidgetStack* Stack : Stacks)
	{
		if (!Stack)
		{
			continue;
		}

		Stack->SetTransitionDuration(0.f);
		Stack->ClearWidgets();
	}
}

bool UBasePrimaryLayout::HasActiveMenu() const
{
	const EUILayer MenuLayers[] = { EUILayer::GameMenu, EUILayer::Menu, EUILayer::Modal };
	for (const EUILayer Layer : MenuLayers)
	{
		const UCommonActivatableWidgetStack* Stack = LayerStacks.FindRef(Layer);
		if (Stack && Stack->GetActiveWidget())
		{
			return true;
		}
	}

	return false;
}

bool UBasePrimaryLayout::HasActiveWidget() const
{
	const UCommonActivatableWidgetStack* Stacks[] = { GameStack, GameMenuStack, MenuStack, ModalStack };
	for (const UCommonActivatableWidgetStack* Stack : Stacks)
	{
		if (Stack && Stack->GetActiveWidget())
		{
			return true;
		}
	}

	return false;
}

void UBasePrimaryLayout::RemoveWidget(UCommonActivatableWidget* Widget)
{
	if (!Widget)
	{
		return;
	}

	for (const TPair<EUILayer, TObjectPtr<UCommonActivatableWidgetStack>>& Layer : LayerStacks)
	{
		UCommonActivatableWidgetStack* Stack = Layer.Value;
		if (Stack && Stack->GetWidgetList().Contains(Widget))
		{
			Stack->RemoveWidget(*Widget);
			return;
		}
	}
}
