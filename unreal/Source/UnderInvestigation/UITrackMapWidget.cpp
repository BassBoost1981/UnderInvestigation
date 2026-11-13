#include "UITrackMapWidget.h"

#include "UIRaceDirector.h"
#include "UIStyle.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SViewport.h"

DEFINE_LOG_CATEGORY_STATIC(LogUISim, Log, All); // same category name as UIRaceDirector.cpp

namespace
{
	constexpr double MapWidth = 600.0;
	constexpr double MapHeight = 340.0;
	constexpr double MapMargin = 12.0;
	constexpr double CarHitRadius = 8.0;    // map units
	constexpr double TrackHitRadius = 12.0;
	constexpr int32 TrackSamples = 600;

	const TCHAR* const MenuLabels[] = {TEXT("Gruen"), TEXT("Gelb"), TEXT("Doppelt Gelb")};

	FLinearColor FlagColor(EUISimFlag Flag)
	{
		switch (Flag)
		{
		case EUISimFlag::Yellow: return FLinearColor(1.0f, 0.85f, 0.1f);
		case EUISimFlag::DoubleYellow: return FLinearColor(1.0f, 0.4f, 0.05f);
		case EUISimFlag::Green: break;
		}
		return FLinearColor(0.55f, 0.6f, 0.55f);
	}
}

TSharedRef<SWidget> UUITrackMapWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		WidgetTree->RootWidget = Canvas;

		UBorder* Panel = WidgetTree->ConstructWidget<UBorder>();
		Panel->SetBrushColor(FLinearColor::Transparent); // the card of the desk is the background
		Panel->SetPadding(FMargin(8.0f));
		UCanvasPanelSlot* PanelSlot = Canvas->AddChildToCanvas(Panel);
		PanelSlot->SetAutoSize(true);
		PanelSlot->SetPosition(UIStyle::MapCard.Min + FVector2D(10.0, 8.0));

		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>();
		Panel->SetContent(Column);
		UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>();
		Title->SetFont(FCoreStyle::GetDefaultFontStyle("Mono", 11));
		Title->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.85f, 0.2f)));
		Title->SetText(FText::FromString(TEXT("STRECKE  Klick: Flagge, Auto")));
		Title->SetVisibility(ESlateVisibility::HitTestInvisible);
		Column->AddChildToVerticalBox(Title);

		USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>();
		Box->SetWidthOverride(MapWidth);
		Box->SetHeightOverride(MapHeight);
		Column->AddChildToVerticalBox(Box);
		MapArea = WidgetTree->ConstructWidget<UBorder>();
		MapArea->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.2f)); // visible, so it takes the clicks
		Box->SetContent(MapArea);
	}
	return Super::RebuildWidget();
}

bool UUITrackMapWidget::PrepareProjection(const FVector2D& Size) const
{
	const AUIRaceDirector* Race = Director.Get();
	if (!Race || Size.X <= 0.0 || Size.Y <= 0.0)
	{
		return false;
	}
	if (Size == ProjectedSize)
	{
		return true;
	}
	// Top view like the overview camera: world +X is up on the map, +Y is right.
	const TArray<FVector> Line = Race->Session().CenterlineWorld();
	if (Line.Num() == 0)
	{
		return false;
	}
	FBox2D Bounds(ForceInit);
	for (const FVector& Point : Line)
	{
		Bounds += FVector2D(Point.Y, Point.X);
	}
	const FVector2D Extent = Bounds.GetSize();
	Scale = FMath::Min((Size.X - 2.0 * MapMargin) / FMath::Max(Extent.X, 1.0), (Size.Y - 2.0 * MapMargin) / FMath::Max(Extent.Y, 1.0));
	WorldMin = FVector2D(Bounds.Min.X, Bounds.Max.Y); // left = smallest Y, top = largest X
	Offset = (Size - Extent * Scale) / 2.0;
	ProjectedSize = Size;
	return true;
}

FVector2D UUITrackMapWidget::ToMap(double Distance) const
{
	const FVector World = Director->Session().TrackTransform(Distance, 0.0).GetLocation();
	return FVector2D((World.Y - WorldMin.X) * Scale + Offset.X, (WorldMin.Y - World.X) * Scale + Offset.Y);
}

FBox2D UUITrackMapWidget::MenuRow(int32 Row) const
{
	const double Top = 28.0 + Row * 22.0;
	return FBox2D(FVector2D(8.0, Top), FVector2D(128.0, Top + 20.0));
}

void UUITrackMapWidget::LogClickTargets(const FGeometry& Map) const
{
	// Viewport pixels of the click targets, for -UISimScript checks (click x y).
	const UWorld* World = GetWorld();
	UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
	const TSharedPtr<SViewport> ViewportWidget = Viewport ? Viewport->GetGameViewportWidget() : nullptr;
	if (!ViewportWidget.IsValid())
	{
		return;
	}
	const FVector2D Origin = FVector2D(ViewportWidget->GetCachedGeometry().GetAbsolutePosition());
	auto Pixel = [&](const FVector2D& Local) { return FVector2D(Map.LocalToAbsolute(Local)) - Origin; };
	const FUISimSession& Session = Director->Session();
	const int32 Sectors = Session.MarshalSectorCount();
	const double SectorLength = Session.TrackLength() / FMath::Max(1, Sectors);
	for (int32 Sector = 0; Sector < Sectors; ++Sector)
	{
		const FVector2D At = Pixel(ToMap((Sector + 0.5) * SectorLength));
		UE_LOG(LogUISim, Display, TEXT("Track map: sector %d at %.0f %.0f"), Sector + 1, At.X, At.Y);
	}
	for (int32 Row = 0; Row < UE_ARRAY_COUNT(MenuLabels); ++Row)
	{
		const FVector2D At = Pixel(MenuRow(Row).GetCenter());
		UE_LOG(LogUISim, Display, TEXT("Track map: menu '%s' at %.0f %.0f"), MenuLabels[Row], At.X, At.Y);
	}
	for (const FUISimCar& Car : Session.Snapshot().Cars)
	{
		const FVector2D At = Pixel(ToMap(Car.Distance));
		UE_LOG(LogUISim, Display, TEXT("Track map: car %s at %.0f %.0f (race time %.1f s)"), *Session.Entries()[Car.EntryIndex].Code,
			At.X, At.Y, Session.Snapshot().Time);
	}
}

int32 UUITrackMapWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const AUIRaceDirector* Race = Director.Get();
	if (!Race || !Race->Session().IsStarted() || !MapArea)
	{
		return Layer;
	}
	const FGeometry& Map = MapArea->GetCachedGeometry();
	if (!PrepareProjection(Map.GetLocalSize()))
	{
		return Layer;
	}
	const FUISimSession& Session = Race->Session();
	const FUISimSnapshot& Snapshot = Session.Snapshot();
	const double Length = Session.TrackLength();
	const int32 Sectors = Session.MarshalSectorCount();
	const double SectorLength = Length / FMath::Max(1, Sectors);
	const FPaintGeometry Paint = Map.ToPaintGeometry();
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
	const FSlateFontInfo Small = FCoreStyle::GetDefaultFontStyle("Mono", 8);
	auto Box = [&](const FVector2D& Centre, double Size, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, Layer, Map.ToPaintGeometry(FVector2D(Size, Size),
			FSlateLayoutTransform(1.0f, Centre - FVector2D(Size, Size) / 2.0)), White, ESlateDrawEffect::None, Color);
	};
	auto Text = [&](const FVector2D& TopLeft, const FString& String, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeText(OutDrawElements, Layer, Map.ToPaintGeometry(FVector2D(120.0, 14.0),
			FSlateLayoutTransform(1.0f, TopLeft)), String, Small, ESlateDrawEffect::None, Color);
	};

	// Track, one polyline per marshal sector in its flag colour; the selected sector drawn wider.
	++Layer;
	for (int32 Sector = 0; Sector < Sectors; ++Sector)
	{
		TArray<FVector2D> Points;
		const int32 Steps = FMath::Max(2, TrackSamples / FMath::Max(1, Sectors));
		for (int32 Step = 0; Step <= Steps; ++Step)
		{
			Points.Add(ToMap(SectorLength * (Sector + static_cast<double>(Step) / Steps)));
		}
		const bool bSelected = Sector == Race->SelectedSector() || Sector == MenuSector;
		const EUISimFlag Flag = Snapshot.SectorFlags.IsValidIndex(Sector) ? Snapshot.SectorFlags[Sector] : EUISimFlag::Green;
		FSlateDrawElement::MakeLines(OutDrawElements, Layer, Paint, Points, ESlateDrawEffect::None, FlagColor(Flag), true,
			bSelected ? 8.0f : 4.0f);
	}
	++Layer;
	const FVector2D Line = ToMap(0.0);
	Box(Line, 6.0, FLinearColor::White); // start/finish line

	// Accident sites, Safety Car, cars.
	for (const FUISimHazard& Hazard : Snapshot.Hazards)
	{
		Box(ToMap(Hazard.TrackPosition), 9.0, FLinearColor(0.9f, 0.1f, 0.1f));
	}
	if (Snapshot.bSafetyCarOnTrack)
	{
		Box(ToMap(Snapshot.SafetyCarDistance), 9.0, FLinearColor(1.0f, 0.45f, 0.0f));
	}
	++Layer;
	for (const FUISimCar& Car : Snapshot.Cars)
	{
		if (Car.bFinished || Car.bRetired || Car.bInPitLane)
		{
			continue;
		}
		const bool bFollowed = Car.EntryIndex == Race->FollowedCar();
		const FVector2D At = ToMap(Car.Distance);
		Box(At, bFollowed ? 9.0 : 6.0, bFollowed ? FLinearColor(1.0f, 0.85f, 0.2f) : FLinearColor::White);
		if (bFollowed || Car.Position == 1) // more labels would pile up in the pack
		{
			Text(At + FVector2D(5.0, -6.0), Session.Entries()[Car.EntryIndex].Code,
				bFollowed ? FLinearColor(1.0f, 0.85f, 0.2f) : FLinearColor(0.85f, 0.85f, 0.85f));
		}
	}

	// Flag menu for the clicked sector.
	if (MenuSector >= 0)
	{
		++Layer;
		FSlateDrawElement::MakeBox(OutDrawElements, Layer, Map.ToPaintGeometry(FVector2D(128.0, 96.0),
			FSlateLayoutTransform(1.0f, FVector2D(4.0, 4.0))), White, ESlateDrawEffect::None, FLinearColor(0.0f, 0.0f, 0.0f, 0.85f));
		++Layer;
		Text(FVector2D(10.0, 9.0), FString::Printf(TEXT("Sektor %d"), MenuSector + 1), FLinearColor(1.0f, 0.85f, 0.2f));
		for (int32 Row = 0; Row < UE_ARRAY_COUNT(MenuLabels); ++Row)
		{
			const FBox2D Rect = MenuRow(Row);
			FSlateDrawElement::MakeBox(OutDrawElements, Layer, Map.ToPaintGeometry(Rect.GetSize(), FSlateLayoutTransform(1.0f, Rect.Min)),
				White, ESlateDrawEffect::None, FlagColor(static_cast<EUISimFlag>(Row)) * FLinearColor(1.0f, 1.0f, 1.0f, 0.6f));
			Text(Rect.Min + FVector2D(6.0, 3.0), MenuLabels[Row], FLinearColor::White);
		}
	}

	if (!bTargetsLogged)
	{
		bTargetsLogged = true;
		LogClickTargets(Map);
	}
	return Layer;
}

FReply UUITrackMapWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	AUIRaceDirector* Race = Director.Get();
	if (!Race || !MapArea || InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton ||
		!PrepareProjection(MapArea->GetCachedGeometry().GetLocalSize()))
	{
		return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
	}
	const FVector2D Local = FVector2D(MapArea->GetCachedGeometry().AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition()));
	const FUISimSession& Session = Race->Session();

	// 1. A row of the open flag menu.
	if (MenuSector >= 0)
	{
		for (int32 Row = 0; Row < UE_ARRAY_COUNT(MenuLabels); ++Row)
		{
			if (MenuRow(Row).IsInside(Local))
			{
				FUICommand Command;
				Command.Type = EUICommand::SetSectorFlag;
				Command.Index = MenuSector;
				Command.Flag = static_cast<EUISimFlag>(Row);
				Race->Execute(Command);
				MenuSector = -1;
				return FReply::Handled();
			}
		}
	}

	// 2. The nearest car on track.
	int32 BestCar = -1;
	double BestDistance = CarHitRadius;
	for (const FUISimCar& Car : Session.Snapshot().Cars)
	{
		if (Car.bFinished || Car.bRetired || Car.bInPitLane)
		{
			continue;
		}
		const double Distance = FVector2D::Distance(ToMap(Car.Distance), Local);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			BestCar = Car.EntryIndex;
		}
	}
	if (BestCar >= 0)
	{
		FUICommand Command;
		Command.Type = EUICommand::FollowCar;
		Command.Index = BestCar;
		Race->Execute(Command);
		MenuSector = -1;
		return FReply::Handled();
	}

	// 3. The nearest point of the track: open the flag menu for its marshal sector.
	const int32 Sectors = Session.MarshalSectorCount();
	const double Length = Session.TrackLength();
	int32 BestSector = -1;
	BestDistance = TrackHitRadius;
	for (int32 Sample = 0; Sample < TrackSamples; ++Sample)
	{
		const double Along = Length * Sample / TrackSamples;
		const double Distance = FVector2D::Distance(ToMap(Along), Local);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			BestSector = FMath::Clamp(static_cast<int32>(Along / (Length / Sectors)), 0, Sectors - 1);
		}
	}
	if (BestSector >= 0)
	{
		FUICommand Command;
		Command.Type = EUICommand::SelectSector;
		Command.Index = BestSector;
		Race->Execute(Command);
		MenuSector = BestSector;
		return FReply::Handled();
	}
	MenuSector = -1; // a click beside the track closes the menu
	return FReply::Handled();
}
