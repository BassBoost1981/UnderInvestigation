#include "UIRadioWidget.h"

#include "UIRaceDirector.h"
#include "UIStyle.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Styling/CoreStyle.h"

namespace
{
	constexpr int32 MaxLines = 6;

	UTextBlock* MakeText(UWidgetTree* Tree, const FLinearColor& Color)
	{
		UTextBlock* Text = Tree->ConstructWidget<UTextBlock>();
		Text->SetFont(FCoreStyle::GetDefaultFontStyle("Mono", 13));
		Text->SetColorAndOpacity(FSlateColor(Color));
		return Text;
	}

	// The presentation layer words the structured reports from the core.
	FString Wording(const FUISimRadio& Message, const FString& Code)
	{
		switch (Message.Type)
		{
		case EUISimRadio::Spin: return FString::Printf(TEXT("%s hat sich gedreht, faehrt weiter"), *Code);
		case EUISimRadio::CarOffTrack: return FString::Printf(TEXT("%s steht neben der Strecke, Fahrer ok"), *Code);
		case EUISimRadio::CarOnTrack: return FString::Printf(TEXT("%s steht AUF der Strecke, Truemmer!"), *Code);
		case EUISimRadio::RecoveryNeedsFlag:
			return FString::Printf(TEXT("Bergung %s bereit, brauchen %s"), *Code,
				Message.bOnTrack ? TEXT("doppelt Gelb") : TEXT("Gelb"));
		case EUISimRadio::TrackClear: return FString::Printf(TEXT("%s geborgen, Strecke frei"), *Code);
		}
		return FString();
	}
}

TSharedRef<SWidget> UUIRadioWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		WidgetTree->RootWidget = Canvas;

		UBorder* Panel = WidgetTree->ConstructWidget<UBorder>();
		Panel->SetBrushColor(FLinearColor::Transparent); // the card of the desk is the background
		Panel->SetPadding(FMargin(12.0f));
		UCanvasPanelSlot* PanelSlot = Canvas->AddChildToCanvas(Panel);
		PanelSlot->SetAutoSize(true);
		PanelSlot->SetPosition(UIStyle::ControlCard.Min + FVector2D(0.0, 124.0)); // below the control panel

		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>();
		Panel->SetContent(Column);
		UTextBlock* Title = MakeText(WidgetTree, FLinearColor(1.0f, 0.85f, 0.2f));
		Title->SetText(FText::FromString(TEXT("FUNK STRECKENPOSTEN")));
		Column->AddChildToVerticalBox(Title);
		for (int32 Index = 0; Index < MaxLines; ++Index)
		{
			UTextBlock* Line = MakeText(WidgetTree, FLinearColor::White);
			Column->AddChildToVerticalBox(Line);
			Lines.Add(Line);
		}
	}
	return Super::RebuildWidget();
}

void UUIRadioWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const AUIRaceDirector* Race = Director.Get();
	if (!Race || !Race->Session().IsStarted())
	{
		return;
	}
	const TArray<FUISimRadio>& Radio = Race->Session().Radio();
	const TArray<FUISimEntry>& Entries = Race->Session().Entries();
	const int32 First = FMath::Max(0, Radio.Num() - Lines.Num());
	for (int32 Line = 0; Line < Lines.Num(); ++Line)
	{
		const int32 Index = First + Line;
		if (!Radio.IsValidIndex(Index))
		{
			Lines[Line]->SetText(FText::FromString(Radio.IsEmpty() && Line == 0 ? TEXT("(keine Meldungen)") : TEXT("")));
			continue;
		}
		const FUISimRadio& Message = Radio[Index];
		Lines[Line]->SetText(FText::FromString(FString::Printf(TEXT("%9s Posten %2d: %s"),
			*AUIRaceDirector::FormatRaceTime(Message.Time), Message.MarshalPost,
			*Wording(Message, Entries[Message.Car].Code))));
		const bool bUrgent = Message.Type == EUISimRadio::CarOnTrack || Message.Type == EUISimRadio::RecoveryNeedsFlag;
		const bool bClear = Message.Type == EUISimRadio::TrackClear;
		Lines[Line]->SetColorAndOpacity(FSlateColor(bUrgent ? FLinearColor(1.0f, 0.35f, 0.25f)
			: bClear ? FLinearColor(0.4f, 1.0f, 0.4f) : FLinearColor::White));
	}
}
