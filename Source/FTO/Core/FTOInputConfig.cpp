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
	Horn     = MakeAction(TEXT("IA_Horn"),     (int32)EInputActionValueType::Boolean);
	Camera   = MakeAction(TEXT("IA_Camera"),   (int32)EInputActionValueType::Boolean);
	Tackle   = MakeAction(TEXT("IA_Tackle"),   (int32)EInputActionValueType::Boolean);
	Draw     = MakeAction(TEXT("IA_Draw"),     (int32)EInputActionValueType::Boolean);
	Fire     = MakeAction(TEXT("IA_Fire"),     (int32)EInputActionValueType::Boolean);
	Reload   = MakeAction(TEXT("IA_Reload"),   (int32)EInputActionValueType::Boolean);
	NextWeapon = MakeAction(TEXT("IA_NextWeapon"), (int32)EInputActionValueType::Boolean);
	PrevWeapon = MakeAction(TEXT("IA_PrevWeapon"), (int32)EInputActionValueType::Boolean);
	for (int32 Slot = 0; Slot < 3; ++Slot)
	{
		Slots.Add(MakeAction(*FString::Printf(TEXT("IA_Slot%d"), Slot + 1), (int32)EInputActionValueType::Boolean));
	}

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

	DefaultContext->MapKey(Horn, EKeys::H);
	DefaultContext->MapKey(Horn, EKeys::Gamepad_RightShoulder);

	DefaultContext->MapKey(Camera, EKeys::C);
	DefaultContext->MapKey(Camera, EKeys::Gamepad_RightThumbstick);

	DefaultContext->MapKey(Tackle, EKeys::F);
	DefaultContext->MapKey(Tackle, EKeys::Gamepad_FaceButton_Right);

	// Weapons
	DefaultContext->MapKey(Draw, EKeys::RightMouseButton);
	DefaultContext->MapKey(Draw, EKeys::Gamepad_LeftTrigger);
	DefaultContext->MapKey(Fire, EKeys::LeftMouseButton);
	DefaultContext->MapKey(Fire, EKeys::Gamepad_RightTrigger);
	DefaultContext->MapKey(Reload, EKeys::R);
	DefaultContext->MapKey(Reload, EKeys::Gamepad_RightShoulder);
	DefaultContext->MapKey(NextWeapon, EKeys::MouseScrollDown);
	DefaultContext->MapKey(NextWeapon, EKeys::Gamepad_DPad_Right);
	DefaultContext->MapKey(PrevWeapon, EKeys::MouseScrollUp);
	DefaultContext->MapKey(PrevWeapon, EKeys::Gamepad_DPad_Left);
	DefaultContext->MapKey(Slots[0], EKeys::One);
	DefaultContext->MapKey(Slots[1], EKeys::Two);
	DefaultContext->MapKey(Slots[2], EKeys::Three);

	// Radio
	Radio      = MakeAction(TEXT("IA_Radio"),      (int32)EInputActionValueType::Boolean);
	RadioWheel = MakeAction(TEXT("IA_RadioWheel"), (int32)EInputActionValueType::Boolean);
	DefaultContext->MapKey(Radio, EKeys::V);
	DefaultContext->MapKey(Radio, EKeys::Gamepad_DPad_Down);
	DefaultContext->MapKey(RadioWheel, EKeys::T);
	DefaultContext->MapKey(RadioWheel, EKeys::Gamepad_DPad_Up);

	WheelContext = NewObject<UInputMappingContext>(this, TEXT("IMC_FTORadioWheel"));
	RadioAim = MakeAction(TEXT("IA_RadioAim"), (int32)EInputActionValueType::Axis2D);
	RadioAimStick = MakeAction(TEXT("IA_RadioAimStick"), (int32)EInputActionValueType::Axis2D);
	WheelContext->MapKey(RadioAim, EKeys::Mouse2D);
	WheelContext->MapKey(RadioAimStick, EKeys::Gamepad_Right2D);
	const FKey CalloutKeys[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four };
	for (int32 i = 0; i < UE_ARRAY_COUNT(CalloutKeys); ++i)
	{
		Callouts.Add(MakeAction(*FString::Printf(TEXT("IA_Callout%d"), i + 1), (int32)EInputActionValueType::Boolean));
		WheelContext->MapKey(Callouts[i], CalloutKeys[i]);
	}

	// End of shift
	VoteOvertime = MakeAction(TEXT("IA_VoteOvertime"), (int32)EInputActionValueType::Boolean);
	VoteClockOff = MakeAction(TEXT("IA_VoteClockOff"), (int32)EInputActionValueType::Boolean);
	DefaultContext->MapKey(VoteOvertime, EKeys::Y);
	DefaultContext->MapKey(VoteOvertime, EKeys::Gamepad_LeftShoulder);
	DefaultContext->MapKey(VoteClockOff, EKeys::N);
	DefaultContext->MapKey(VoteClockOff, EKeys::Gamepad_Special_Left);
}
