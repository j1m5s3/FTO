#include "UI/FTOMenuWidget.h"
#include "Core/FTOGameState.h"
#include "Core/FTOPlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "Styling/CoreStyle.h"

namespace
{
	const FLinearColor PanelColor(0.02f, 0.03f, 0.08f, 0.88f);
	const FLinearColor Accent(0.55f, 0.75f, 1.f);
}

UTextBlock* UFTOMenuWidget::MakeText(const FText& Text, int32 Size, const FLinearColor& Color)
{
	UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Block->SetText(Text);
	Block->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", Size));
	Block->SetColorAndOpacity(FSlateColor(Color));
	Block->SetJustification(ETextJustify::Center);
	return Block;
}

UButton* UFTOMenuWidget::AddButton(UVerticalBox* Box, const FText& Label, FName Handler)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	Button->SetBackgroundColor(FLinearColor(0.12f, 0.2f, 0.45f));
	Button->AddChild(MakeText(Label, 18, FLinearColor::White));

	FScriptDelegate Delegate;
	Delegate.BindUFunction(this, Handler);
	Button->OnClicked.Add(Delegate);

	if (UVerticalBoxSlot* BoxSlot = Box->AddChildToVerticalBox(Button))
	{
		BoxSlot->SetPadding(FMargin(0.f, 6.f));
		BoxSlot->SetHorizontalAlignment(HAlign_Fill);
	}
	return Button;
}

TSharedRef<SWidget> UFTOMenuWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Root;

		// Dim the game behind the menu.
		UBorder* Dim = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Dim->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.45f));
		if (UCanvasPanelSlot* DimSlot = Root->AddChildToCanvas(Dim))
		{
			DimSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			DimSlot->SetOffsets(FMargin(0.f));
		}

		USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Size->SetWidthOverride(520.f);
		if (UCanvasPanelSlot* SizeSlot = Root->AddChildToCanvas(Size))
		{
			SizeSlot->SetAnchors(FAnchors(0.5f, 0.5f));
			SizeSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			SizeSlot->SetAutoSize(true);
		}

		UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Panel->SetBrushColor(PanelColor);
		Panel->SetPadding(FMargin(32.f, 24.f));
		Size->AddChild(Panel);

		UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		Panel->AddChild(Box);

		Box->AddChildToVerticalBox(MakeText(INVTEXT("FTO"), 44, Accent));
		Box->AddChildToVerticalBox(MakeText(INVTEXT("Keep the city from falling into chaos"), 14, FLinearColor(0.8f, 0.8f, 0.85f)));

		StatusText = MakeText(FText::GetEmpty(), 13, FLinearColor(1.f, 0.85f, 0.35f));
		StatusText->SetAutoWrapText(true);
		if (UVerticalBoxSlot* StatusSlot = Box->AddChildToVerticalBox(StatusText))
		{
			StatusSlot->SetPadding(FMargin(0.f, 14.f));
		}

		StartButton = AddButton(Box, INVTEXT("Start shift"), GET_FUNCTION_NAME_CHECKED(UFTOMenuWidget, OnStartShift));
		NewShiftButton = AddButton(Box, INVTEXT("New shift (new city)"), GET_FUNCTION_NAME_CHECKED(UFTOMenuWidget, OnNewShift));
		HostButton = AddButton(Box, INVTEXT("Host an online game"), GET_FUNCTION_NAME_CHECKED(UFTOMenuWidget, OnHost));

		// Join row: address box + button.
		UHorizontalBox* JoinRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		AddressBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass());
		AddressBox->SetHintText(INVTEXT("Host's IP, e.g. 192.168.1.23"));
		if (UHorizontalBoxSlot* AddressSlot = JoinRow->AddChildToHorizontalBox(AddressBox))
		{
			AddressSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			AddressSlot->SetVerticalAlignment(VAlign_Center);
			AddressSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
		}
		JoinButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		JoinButton->SetBackgroundColor(FLinearColor(0.12f, 0.2f, 0.45f));
		JoinButton->AddChild(MakeText(INVTEXT("Join"), 18, FLinearColor::White));
		JoinButton->OnClicked.AddDynamic(this, &UFTOMenuWidget::OnJoin);
		JoinRow->AddChildToHorizontalBox(JoinButton);
		if (UVerticalBoxSlot* JoinSlot = Box->AddChildToVerticalBox(JoinRow))
		{
			JoinSlot->SetPadding(FMargin(0.f, 6.f));
		}

		AddButton(Box, INVTEXT("Back to the streets"), GET_FUNCTION_NAME_CHECKED(UFTOMenuWidget, OnResume));
		AddButton(Box, INVTEXT("Quit"), GET_FUNCTION_NAME_CHECKED(UFTOMenuWidget, OnQuit));

		Box->AddChildToVerticalBox(MakeText(INVTEXT("Esc opens and closes this menu"), 11, FLinearColor(0.6f, 0.6f, 0.65f)));

		RefreshState();
	}
	return Super::RebuildWidget();
}

void UFTOMenuWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	RefreshAccumulator += InDeltaTime;
	if (RefreshAccumulator >= 0.25f)
	{
		RefreshAccumulator = 0.f;
		RefreshState();
	}
}

void UFTOMenuWidget::RefreshState()
{
	const AFTOPlayerController* PC = GetOwningPlayer<AFTOPlayerController>();
	const UWorld* World = GetWorld();
	if (!PC || !World || !StatusText)
	{
		return;
	}

	const ENetMode NetMode = World->GetNetMode();
	const bool bIsHost = NetMode != NM_Client;
	const AFTOGameState* GS = World->GetGameState<AFTOGameState>();
	const EFTOShiftPhase Phase = GS ? GS->GetShiftPhase() : EFTOShiftPhase::Lobby;

	FString Status;
	switch (NetMode)
	{
	case NM_Standalone:   Status = TEXT("Playing solo. Host an online game to let friends join."); break;
	case NM_ListenServer: Status = FString::Printf(TEXT("Hosting! Friends join with your IP: %s  (port 7777)"), *AFTOPlayerController::GetLocalAddress()); break;
	case NM_Client:       Status = TEXT("Connected. The host starts and restarts shifts."); break;
	default:              break;
	}
	if (GS)
	{
		Status += FString::Printf(TEXT("\n%d/4 officers on duty."), GS->PlayerArray.Num());
	}
	StatusText->SetText(FText::FromString(Status));

	StartButton->SetVisibility(bIsHost && Phase == EFTOShiftPhase::Lobby ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	NewShiftButton->SetVisibility(bIsHost && Phase != EFTOShiftPhase::Lobby ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	HostButton->SetVisibility(NetMode == NM_Standalone ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	const ESlateVisibility JoinVisibility = NetMode == NM_Client ? ESlateVisibility::Collapsed : ESlateVisibility::Visible;
	AddressBox->SetVisibility(JoinVisibility);
	JoinButton->SetVisibility(JoinVisibility);
}

void UFTOMenuWidget::OnStartShift()
{
	if (AFTOPlayerController* PC = GetOwningPlayer<AFTOPlayerController>())
	{
		PC->ServerStartShift();
		PC->SetMenuVisible(false);
	}
}

void UFTOMenuWidget::OnNewShift()
{
	if (AFTOPlayerController* PC = GetOwningPlayer<AFTOPlayerController>())
	{
		PC->ServerNewShift();
	}
}

void UFTOMenuWidget::OnHost()
{
	if (AFTOPlayerController* PC = GetOwningPlayer<AFTOPlayerController>())
	{
		PC->HostOnline();
	}
}

void UFTOMenuWidget::OnJoin()
{
	AFTOPlayerController* PC = GetOwningPlayer<AFTOPlayerController>();
	const FString Address = AddressBox ? AddressBox->GetText().ToString().TrimStartAndEnd() : FString();
	if (PC && !Address.IsEmpty())
	{
		PC->JoinGame(Address);
	}
}

void UFTOMenuWidget::OnResume()
{
	if (AFTOPlayerController* PC = GetOwningPlayer<AFTOPlayerController>())
	{
		PC->SetMenuVisible(false);
	}
}

void UFTOMenuWidget::OnQuit()
{
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->ConsoleCommand(TEXT("quit"));
	}
}
