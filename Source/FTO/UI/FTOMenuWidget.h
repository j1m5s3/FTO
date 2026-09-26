#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FTOMenuWidget.generated.h"

class UButton;
class UEditableTextBox;
class UTextBlock;
class UVerticalBox;

/**
 * The pause / lobby menu: start or restart the shift (host), host online, join by IP, quit.
 * Built entirely in C++ (no widget blueprint asset), styled to match the canvas HUD.
 */
UCLASS()
class FTO_API UFTOMenuWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UFUNCTION() void OnStartShift();
	UFUNCTION() void OnNewShift();
	UFUNCTION() void OnHost();
	UFUNCTION() void OnJoin();
	UFUNCTION() void OnResume();
	UFUNCTION() void OnQuit();

	UButton* AddButton(UVerticalBox* Box, const FText& Label, FName Handler);
	UTextBlock* MakeText(const FText& Text, int32 Size, const FLinearColor& Color);

	/** Refreshes which options apply (host vs client, lobby vs on duty). */
	void RefreshState();
	void PlayClick();

	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText;
	UPROPERTY(Transient) TObjectPtr<UButton> StartButton;
	UPROPERTY(Transient) TObjectPtr<UButton> NewShiftButton;
	UPROPERTY(Transient) TObjectPtr<UButton> HostButton;
	UPROPERTY(Transient) TObjectPtr<UButton> JoinButton;
	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> AddressBox;

	float RefreshAccumulator = 0.f;
};
