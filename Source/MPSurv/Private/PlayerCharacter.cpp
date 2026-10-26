// Fill out your copyright notice in the Description page of Project Settings.

#include "PlayerCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputActionValue.h"

APlayerCharacter::APlayerCharacter()
{
	// The camera orbits freely; the body turns towards the movement direction instead of the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->bOrientRotationToMovement = true;
		Movement->RotationRate = FRotator(0.0f, 500.0f, 0.0f);
	}

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(GetCapsuleComponent());
	SpringArm->TargetArmLength = 300.0f;
	SpringArm->bUsePawnControlRotation = true;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	Camera->bUsePawnControlRotation = false;
}

void APlayerCharacter::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();

	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (!PlayerController || !DefaultMappingContext)
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(DefaultMappingContext, 0);
	}
}

void APlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EnhancedInput)
	{
		return;
	}

	if (IA_Look)
	{
		EnhancedInput->BindAction(IA_Look, ETriggerEvent::Triggered, this, &APlayerCharacter::CameraLook);
	}

	if (IA_Walk)
	{
		EnhancedInput->BindAction(IA_Walk, ETriggerEvent::Triggered, this, &APlayerCharacter::Walk);
	}
}

void APlayerCharacter::CameraLook(const FInputActionValue& Value)
{
	const FVector2D LookAxis = Value.Get<FVector2D>();

	AddControllerYawInput(LookAxis.X);
	AddControllerPitchInput(LookAxis.Y);
}

void APlayerCharacter::Walk(const FInputActionValue& Value)
{
	const AController* CurrentController = GetController();
	if (!CurrentController)
	{
		return;
	}

	const FVector2D MoveAxis = Value.Get<FVector2D>();

	// Only the yaw is used, so looking up or down does not tilt the movement plane.
	const FRotator YawRotation(0.0f, CurrentController->GetControlRotation().Yaw, 0.0f);
	const FRotationMatrix YawMatrix(YawRotation);

	AddMovementInput(YawMatrix.GetUnitAxis(EAxis::X), MoveAxis.Y);
	AddMovementInput(YawMatrix.GetUnitAxis(EAxis::Y), MoveAxis.X);
}
