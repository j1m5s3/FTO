#include "Core/FTOInputConfig.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

UInputAction* UFTOInputConfig::MakeAction(FName Name, int32 ValueType)
{
	UInputAction* Action = NewObject<UInputAction>(this, Name);
	Action->ValueType = static_cast<EInputActionValueType>(ValueType);
	return Action;
}

void UFTOInputConfig::MapAxis2D(UInputAction* Action, FKey Up, FKey Down, FKey Left, FKey Right)
{
	// Keys produce +X by default. Swizzle turns X into Y for forward/back; Negate flips direction.
	{
		FEnhancedActionKeyMapping& M = DefaultContext->MapKey(Action, Up);
		M.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(DefaultContext));
	}
	{
		FEnhancedActionKeyMapping& M = DefaultContext->MapKey(Action, Down);
		M.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(DefaultContext));
		M.Modifiers.Add(NewObject<UInputModifierNegate>(DefaultContext));
	}
	{
		FEnhancedActionKeyMapping& M = DefaultContext->MapKey(Action, Left);
		M.Modifiers.Add(NewObject<UInputModifierNegate>(DefaultContext));
	}
	DefaultContext->MapKey(Action, Right);
}

void UFTOInputConfig::Build()
{
	DefaultContext = NewObject<UInputMappingContext>(this, TEXT("IMC_FTODefault"));

	Move     = MakeAction(TEXT("IA_Move"),     (int32)EInputActionValueType::Axis2D);
	Look     = MakeAction(TEXT("IA_Look"),     (int32)EInputActionValueType::Axis2D);
	Jump     = MakeAction(TEXT("IA_Jump"),     (int32)EInputActionValueType::Boolean);
	Sprint   = MakeAction(TEXT("IA_Sprint"),   (int32)EInputActionValueType::Boolean);
	Interact = MakeAction(TEXT("IA_Interact"), (int32)EInputActionValueType::Boolean);
	Whistle  = MakeAction(TEXT("IA_Whistle"),  (int32)EInputActionValueType::Boolean);
	Menu     = MakeAction(TEXT("IA_Menu"),     (int32)EInputActionValueType::Boolean);

	// Movement
	MapAxis2D(Move, EKeys::W, EKeys::S, EKeys::A, EKeys::D);
	DefaultContext->MapKey(Move, EKeys::Gamepad_Left2D);

	// Camera (mouse Y is inverted to feel natural)
	{
		FEnhancedActionKeyMapping& M = DefaultContext->MapKey(Look, EKeys::Mouse2D);
		UInputModifierNegate* FlipY = NewObject<UInputModifierNegate>(DefaultContext);
		FlipY->bX = false;
		FlipY->bY = true;
		FlipY->bZ = false;
		M.Modifiers.Add(FlipY);
	}
	{
		FEnhancedActionKeyMapping& M = DefaultContext->MapKey(Look, EKeys::Gamepad_Right2D);
		UInputModifierNegate* FlipY = NewObject<UInputModifierNegate>(DefaultContext);
		FlipY->bX = false;
		FlipY->bY = true;
		FlipY->bZ = false;
		M.Modifiers.Add(FlipY);
	}

	DefaultContext->MapKey(Jump, EKeys::SpaceBar);
	DefaultContext->MapKey(Jump, EKeys::Gamepad_FaceButton_Bottom);

	DefaultContext->MapKey(Sprint, EKeys::LeftShift);
	DefaultContext->MapKey(Sprint, EKeys::Gamepad_LeftThumbstick);

	DefaultContext->MapKey(Interact, EKeys::E);
	DefaultContext->MapKey(Interact, EKeys::Gamepad_FaceButton_Left);

	DefaultContext->MapKey(Whistle, EKeys::Q);
	DefaultContext->MapKey(Whistle, EKeys::Gamepad_FaceButton_Top);

	DefaultContext->MapKey(Menu, EKeys::Escape);
	DefaultContext->MapKey(Menu, EKeys::Gamepad_Special_Right);
}
