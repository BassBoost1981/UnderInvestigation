#include "UISeasonWidget.h"

#include "UIRaceDirector.h"
#include "UISimCareer.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SViewport.h"

DEFINE_LOG_CATEGORY_STATIC(LogUISim, Log, All); // same category name as UIRaceDirector.cpp

namespace
{
	// The one button (in ButtonArea coordinates): start the next race, or a new season after the last one.
	const FBox2D StartButton(FVector2D(0.0, 4.0), FVector2D(220.0, 34.0));
}

UTextBlock* UUISeasonWidget::MakeText(int32 Size, const FLinearColor& Color)
{
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>();
	Text->SetFont(FCoreStyle::GetDefaultFontStyle("Mono", Size));
	Text->SetColorAndOpacity(FSlateColor(Color));
	Text->SetVisibility(ESlateVisibility::HitTestInvisible);
	return Text;
}

TSharedRef<SWidget> UUISeasonWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		WidgetTree->RootWidget = Canvas;

		// Covers the whole view: there is no race to look at between the races.
		UBorder* Panel = WidgetTree->ConstructWidget<UBorder>();
		Panel->SetBrushColor(FLinearColor(0.01f, 0.012f, 0.02f, 0.97f));
		Panel->SetPadding(FMargin(36.0f, 28.0f));
		UCanvasPanelSlot* PanelSlot = Canvas->AddChildToCanvas(Panel);
		PanelSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
		PanelSlot->SetOffsets(FMargin(0.0f));

		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>();
		Panel->SetContent(Column);
		Header = MakeText(15, FLinearColor::White);
		Column->AddChildToVerticalBox(Header)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 14.0f));

		USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>();
		Box->SetWidthOverride(StartButton.Max.X);
		Box->SetHeightOverride(StartButton.Max.Y + 4.0);
		ButtonArea = WidgetTree->ConstructWidget<UBorder>();
		ButtonArea->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.01f)); // visible, so it takes the clicks
		Box->SetContent(ButtonArea);
		UVerticalBoxSlot* BoxSlot = Column->AddChildToVerticalBox(Box);
		BoxSlot->SetHorizontalAlignment(HAlign_Left);
		BoxSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 16.0f));

		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		Column->AddChildToVerticalBox(Row);
		CalendarText = MakeText(10, FLinearColor(0.85f, 0.88f, 0.95f));
		Row->AddChildToHorizontalBox(CalendarText)->SetPadding(FMargin(0.0f, 0.0f, 36.0f, 0.0f));
		DriversText = MakeText(10, FLinearColor(0.85f, 0.88f, 0.95f));
		Row->AddChildToHorizontalBox(DriversText)->SetPadding(FMargin(0.0f, 0.0f, 36.0f, 0.0f));
		TeamsText = MakeText(10, FLinearColor(0.85f, 0.88f, 0.95f));
		Row->AddChildToHorizontalBox(TeamsText);
	}
	return Super::RebuildWidget();
}

void UUISeasonWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const FUISimCareer& Career = FUISimCareer::Get();
	if (!Career.IsOpen() || !Header)
	{
		return;
	}
	const TArray<FUISimSeasonRace> Calendar = Career.Calendar();
	const TArray<FUISimStanding> Drivers = Career.Drivers();
	const TArray<FUISimStanding> Teams = Career.Teams();
	const int32 Next = Career.NextRace();

	FString Head = FString::Printf(TEXT("SAISON  %s\n"), *Career.SeasonName());
	if (Next > 0)
	{
		Head += FString::Printf(TEXT("Rennen gefahren: %d von %d     Deine Saisonwertung: %.1f von 100\n"), Next, Calendar.Num(),
			Career.Rating());
	}
	else
	{
		Head += FString::Printf(TEXT("Rennen gefahren: 0 von %d     Noch keine Wertung\n"), Calendar.Num());
	}
	if (Calendar.IsValidIndex(Next))
	{
		const FUISimSeasonRace& Race = Calendar[Next];
		const FString Where = Race.Name == Race.Circuit ? Race.Name : Race.Name + TEXT(", ") + Race.Circuit;
		Head += FString::Printf(TEXT("Naechstes Rennen: %s, %d Runden%s"), *Where, Race.Laps, Race.bRain ? TEXT(", Regen moeglich") : TEXT(""));
		FString Suspended;
		for (const FUISimStanding& Driver : Drivers)
		{
			if (Driver.bSuspended)
			{
				Suspended += (Suspended.IsEmpty() ? TEXT("") : TEXT(", ")) + Driver.Name;
			}
		}
		if (!Suspended.IsEmpty())
		{
			Head += TEXT("\nGesperrt in diesem Rennen: ") + Suspended;
		}
	}
	else if (Drivers.Num() > 0 && Teams.Num() > 0)
	{
		Head += FString::Printf(TEXT("Die Saison ist zu Ende. Weltmeister: %s, Konstrukteurs-Weltmeister: %s"), *Drivers[0].Name,
			*Teams[0].Name);
	}
	Header->SetText(FText::FromString(Head));

	FString Races = TEXT("KALENDER                                      Runden        Wertung  Sieger\n");
	for (int32 Index = 0; Index < Calendar.Num(); ++Index)
	{
		const FUISimSeasonRace& Race = Calendar[Index];
		Races += FString::Printf(TEXT("%s%2d  %-38s %4d  %-5s"), Index == Next ? TEXT(">") : TEXT(" "), Index + 1, *Race.Name.Left(38),
			Race.Laps, Race.bRain ? TEXT("REGEN") : TEXT(""));
		if (Race.bDone)
		{
			Races += FString::Printf(TEXT("  %5.1f    %s"), Race.Rating, *Race.Winner);
			if (Race.SatOut.Num() > 0)
			{
				Races += TEXT("  (gesperrt: ") + FString::Join(Race.SatOut, TEXT(", ")) + TEXT(")");
			}
		}
		Races += TEXT("\n");
	}
	CalendarText->SetText(FText::FromString(Races));

	FString DriverLines = TEXT("FAHRER-WM                                   Punkte Siege  Strafpunkte\n");
	for (int32 Index = 0; Index < Drivers.Num(); ++Index)
	{
		const FUISimStanding& Driver = Drivers[Index];
		DriverLines += FString::Printf(TEXT("%2d. %-18s %-20s %5d %5d  %5d%s\n"), Index + 1, *Driver.Name.Left(18), *Driver.Team.Left(20),
			Driver.Points, Driver.Wins, Driver.PenaltyPoints, Driver.bSuspended ? TEXT("  GESPERRT") : TEXT(""));
	}
	DriversText->SetText(FText::FromString(DriverLines));

	FString TeamLines = TEXT("TEAM-WM                    Punkte Siege\n");
	for (int32 Index = 0; Index < Teams.Num(); ++Index)
	{
		TeamLines += FString::Printf(TEXT("%2d. %-22s %5d %5d\n"), Index + 1, *Teams[Index].Name.Left(22), Teams[Index].Points,
			Teams[Index].Wins);
	}
	TeamsText->SetText(FText::FromString(TeamLines));
}

int32 UUISeasonWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (!ButtonArea || ButtonArea->GetCachedGeometry().GetLocalSize().X <= 0.0)
	{
		return Layer;
	}
	const FGeometry& Area = ButtonArea->GetCachedGeometry();
	const bool bFinished = FUISimCareer::Get().IsFinished();
	const TCHAR* Label = bFinished ? TEXT("NEUE SAISON") : TEXT("RENNEN STARTEN");
	++Layer;
	FSlateDrawElement::MakeBox(OutDrawElements, Layer, Area.ToPaintGeometry(StartButton.GetSize(), FSlateLayoutTransform(1.0f, StartButton.Min)),
		FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, FLinearColor(0.1f, 0.42f, 0.16f, 0.95f));
	FSlateDrawElement::MakeText(OutDrawElements, Layer + 1, Area.ToPaintGeometry(StartButton.GetSize(),
		FSlateLayoutTransform(1.0f, StartButton.Min + FVector2D(14.0, 6.0))), Label, FCoreStyle::GetDefaultFontStyle("Mono", 13),
		ESlateDrawEffect::None, FLinearColor::White);
	Layer += 2;

	if (FVector2D(Area.GetAbsolutePosition()) != LoggedAt)
	{
		// Viewport pixel of the button, for -UISimScript checks (click x y); logged again when it moves.
		LoggedAt = FVector2D(Area.GetAbsolutePosition());
		const UWorld* World = GetWorld();
		UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
		const TSharedPtr<SViewport> ViewportWidget = Viewport ? Viewport->GetGameViewportWidget() : nullptr;
		if (ViewportWidget.IsValid())
		{
			const FVector2D At = FVector2D(Area.LocalToAbsolute(StartButton.GetCenter())) -
				FVector2D(ViewportWidget->GetCachedGeometry().GetAbsolutePosition());
			UE_LOG(LogUISim, Display, TEXT("Season: '%s' at %.0f %.0f"), Label, At.X, At.Y);
		}
	}
	return Layer;
}

FReply UUISeasonWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	AUIRaceDirector* Race = Director.Get();
	if (!ButtonArea || !Race || InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
	}
	const FVector2D Local = FVector2D(ButtonArea->GetCachedGeometry().AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition()));
	if (StartButton.IsInside(Local))
	{
		FUICommand Command;
		Command.Type = FUISimCareer::Get().IsFinished() ? EUICommand::NewSeason : EUICommand::StartSeasonRace;
		Race->Execute(Command);
	}
	return FReply::Handled();
}
