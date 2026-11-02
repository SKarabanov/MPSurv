// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BasePrimaryLayout.generated.h"

class UCommonActivatableWidget;
class UCommonActivatableWidgetStack;
class UVerticalBox;

UENUM(BlueprintType)
enum class EUILayer : uint8
{
	Game,
	GameMenu,
	Menu,
	Modal,
	Toast
};

/** Root widget that owns one CommonUI stack per UI layer. */
UCLASS()
class MPSURV_API UBasePrimaryLayout : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Associates a stack with a layer. Called for the bound stacks when the widget is constructed. */
	UFUNCTION(BlueprintCallable, Category = "UI|Layers")
	void RegisterLayer(EUILayer Layer, UCommonActivatableWidgetStack* Stack);

	/** Pushes a widget class onto the stack registered for this layer. */
	UFUNCTION(BlueprintCallable, Category = "UI|Layers")
	UCommonActivatableWidget* PushWidgetToLayer(EUILayer Layer, TSubclassOf<UCommonActivatableWidget> WidgetClass);

	/** Removes a widget from whichever layer stack currently holds it. */
	UFUNCTION(BlueprintCallable, Category = "UI|Layers")
	void RemoveWidget(UCommonActivatableWidget* Widget);

	/** Drops every stacked widget immediately, without waiting for a transition. */
	void ClearAllStacks();

	/** True when Game Menu, Menu, or Modal currently has an active widget. */
	bool HasActiveMenu() const;

	/** True when any of the four layer stacks has an active widget. */
	bool HasActiveWidget() const;

	/** Non-blocking container for toast widgets. Toast is not an activatable stack. */
	UFUNCTION(BlueprintPure, Category = "UI|Layers")
	UVerticalBox* GetToastContainer() const { return ToastContainer; }

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "UI|Layers")
	TObjectPtr<UCommonActivatableWidgetStack> GameStack;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "UI|Layers")
	TObjectPtr<UCommonActivatableWidgetStack> GameMenuStack;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "UI|Layers")
	TObjectPtr<UCommonActivatableWidgetStack> MenuStack;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "UI|Layers")
	TObjectPtr<UCommonActivatableWidgetStack> ModalStack;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UVerticalBox> ToastContainer;

private:
	UPROPERTY(Transient)
	TMap<EUILayer, TObjectPtr<UCommonActivatableWidgetStack>> LayerStacks;
};
