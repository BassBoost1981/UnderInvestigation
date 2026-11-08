#include "UITimingTowerWidget.h"

#include "UIRaceDirector.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Styling/CoreStyle.h"

namespace
{
	constexpr int32 MaxRows = 26;
}

UTextBlock* UUITimingTowerWidget::MakeText(const FLinearColor& Color)
{
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>();
	Text->SetFont(FCoreStyle::GetDefaultFontStyle("Mono", 14));
	Text->SetColorAndOpacity(FSlateColor(Color));
	return Text;
}

TSharedRef<SWidget> UUITimingTowerWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		WidgetTree->RootWidget = Canvas;

		UBorder* Panel = WidgetTree->ConstructWidget<UBorder>();
		Panel->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.65f));
		Panel->SetPadding(FMargin(12.0f));
		UCanvasPanelSlot* PanelSlot = Canvas->AddChildToCanvas(Panel);
		PanelSlot->SetAutoSize(true);
		PanelSlot->SetPosition(FVector2D(24.0, 24.0));

		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>();
		Panel->SetContent(Column);

		Header = MakeText(FLinearColor(1.0f, 0.85f, 0.2f));
		Column->AddChildToVerticalBox(Header);
		for (int32 Index = 0; Index < MaxRows; ++Index)
		{
			UTextBlock* Row = MakeText(FLinearColor::White);
			Row->SetVisibility(ESlateVisibility::Collapsed);
			Column->AddChildToVerticalBox(Row);
			Rows.Add(Row);
		}
		Footer = MakeText(FLinearColor(0.6f, 0.6f, 0.6f));
		Column->AddChildToVerticalBox(Footer);
	}
	return Super::RebuildWidget();
}

void UUITimingTowerWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const AUIRaceDirector* Race = Director.Get();
	if (!Race || !Race->Session().IsStarted())
	{
		return;
	}
	const FUISimSnapshot& Snapshot = Race->Session().Snapshot();
	const TArray<FUISimEntry>& Entries = Race->Session().Entries();

	FString State;
	switch (Snapshot.State)
	{
	case EUISimRaceState::VirtualSafetyCar: State = TEXT("VSC"); break;
	case EUISimRaceState::SafetyCar: State = Snapshot.bSafetyCarInThisLap ? TEXT("SC IN THIS LAP") : TEXT("SAFETY CAR"); break;
	case EUISimRaceState::RedFlag: State = TEXT("RED FLAG"); break;
	case EUISimRaceState::Green: break;
	}
	if (Snapshot.bFinished)
	{
		State = TEXT("FINISHED");
	}
	else if (Snapshot.bChequered)
	{
		State = TEXT("CHEQUERED FLAG");
	}
	if (Snapshot.bLappedCarsReleased && Snapshot.State == EUISimRaceState::SafetyCar)
	{
		State += TEXT("  UEBERRUNDETE VORBEI");
	}
	if (Snapshot.bPitEntryClosed)
	{
		State += TEXT("  BOXENGASSE ZU");
	}
	Header->SetText(FText::FromString(FString::Printf(TEXT("LAP %2d/%d  %s  %s"),
		Snapshot.LeaderLap, Snapshot.TotalLaps, *AUIRaceDirector::FormatRaceTime(Snapshot.Time), *State)));

	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		UTextBlock* Row = Rows[Index];
		if (!Snapshot.Cars.IsValidIndex(Index))
		{
			Row->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}
		const FUISimCar& Car = Snapshot.Cars[Index];
		FString Gap;
		if (Car.bDisqualified)
		{
			Gap = TEXT("DSQ");
		}
		else if (Car.bRetired)
		{
			Gap = TEXT("OUT");
		}
		else if (Index == 0)
		{
			Gap = TEXT("Leader");
		}
		else if (Car.LapsDown > 0)
		{
			Gap = FString::Printf(TEXT("+%d L"), Car.LapsDown);
		}
		else
		{
			Gap = FString::Printf(TEXT("+%.3f"), Car.GapToLeader);
		}
		const FString Last = Car.LastLapTime > 0.0 ? AUIRaceDirector::FormatRaceTime(Car.LastLapTime) : FString(TEXT("-"));
		FString Penalty;
		if (Car.bInPitLane)
		{
			Penalty = TEXT("PIT");
		}
		else if (Car.bUnlapping)
		{
			Penalty = TEXT("UNLAP"); // lapped car passing the queue under the SC
		}
		else if (Car.bBlueFlag)
		{
			Penalty = TEXT("BLUE");
		}
		else if (Car.bOvertakeActive && !Car.bRetired)
		{
			Penalty = TEXT("OVT");
		}
		else if (Car.PendingPenalty == EUISimPenalty::DriveThrough)
		{
			Penalty = TEXT("DT");
		}
		else if (Car.PendingPenalty == EUISimPenalty::StopAndGo)
		{
			Penalty = TEXT("SG");
		}
		if (Car.TimePenalty > 0.0)
		{
			Penalty += FString::Printf(TEXT("+%.0fs"), Car.TimePenalty);
		}
		static const TCHAR* const CompoundLetters[] = {TEXT("S"), TEXT("M"), TEXT("H")};
		const FString Tyre = FString::Printf(TEXT("%s%-2d %d"), CompoundLetters[FMath::Clamp(Car.Compound, 0, 2)], Car.TyreAge,
			Car.PitStops);
		if (Car.bBlackWhiteFlag)
		{
			Penalty += FString::Printf(TEXT(" SW%d"), Car.TrackLimitStrikes); // black-and-white flag, track limits
		}
		Row->SetText(FText::FromString(FString::Printf(TEXT("%2d %-3s %9s  %9s %s %s %s"),
			Car.Position, *Entries[Car.EntryIndex].Code, *Gap, *Last, *Tyre, Car.bFinished ? TEXT("F") : TEXT(" "), *Penalty)));
		Row->SetVisibility(ESlateVisibility::HitTestInvisible);
	}

	const int32 Sector = Race->SelectedSector();
	const TCHAR* FlagNames[] = {TEXT("gruen"), TEXT("GELB"), TEXT("DOPPELT GELB")};
	const TCHAR* FlagName = Snapshot.SectorFlags.IsValidIndex(Sector)
		? FlagNames[static_cast<uint8>(Snapshot.SectorFlags[Sector])] : TEXT("-");
	Footer->SetText(FText::FromString(FString::Printf(
		TEXT("Sektor %d/%d: %s [Q/E, Y]  Verstoesse: %d\n")
		TEXT("V VSC  S Safety Car  R Rot/Restart  L Ueberrundete  P Boxengasse%s\n")
		TEXT("Kamera: %s [1-6]  Zeit x%g [+/-]"),
		Sector + 1, Snapshot.SectorFlags.Num(), FlagName, Race->Session().FlagViolations(),
		Snapshot.bPitEntryClosed ? TEXT(" (GESPERRT)") : TEXT(""),
		*Race->ActiveCameraName(), Race->TimeScale())));
}
