// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/GlobalStateViewModel.h"

void UGlobalStateViewModel::SetCurrentPlayerName(const FString& NewCurrentPlayerName)
{
	UE_MVVM_SET_PROPERTY_VALUE(CurrentPlayerName, NewCurrentPlayerName);
}
