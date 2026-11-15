#include "UITrackMapWidget.h"

#include "UIRaceDirector.h"
#include "UIStyle.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SViewport.h"

DEFINE_LOG_CATEGORY_STATIC(LogUISim, Log, All); // same category name as UIRaceDirector.cpp

namespace
{
	// The map inside the card. Sizes below are the design's SVG units at the scale of this map (x 0.645).
	const FBox2D MapRect(UIStyle::MapCard.Min + FVector2D(8.0, 38.0), UIStyle::MapCard.Max - FVector2D(8.0, 8.0));
	constexpr double MapMargin = 26.0;       // room for the sector numbers beside the track
	constexpr double TrackWidth = 11.5;      // dark band under the sectors
	constexpr double SectorWidth = 2.6, FlaggedWidth = 4.5;
	constexpr double DashOn = 4.5, DashOff = 2.6; // double yellow
	constexpr double LabelOffset = 19.0;     // sector numbers beside the track
	constexpr double CarHitRadius = 9.0;     // map units
	constexpr double TrackHitRadius = 14.0;
	constexpr int32 TrackSamples = 600;

	const FLinearColor TrackBand = UIStyle::Rgb(35, 40, 48);
	const FLinearColor SectorIdle = UIStyle::Rgb(89, 97, 110);

	EUISimFlag NextFlag(EUISimFlag Flag)
	{
		return Flag == EUISimFlag::Green ? EUISimFlag::Yellow : Flag == EUISimFlag::Yellow ? EUISimFlag::DoubleYellow : EUISimFlag::Green;
	}

	int32 SectorAt(const FUISimSession& Session, double Distance)
	{
		const int32 Sectors = FMath::Max(1, Session.MarshalSectorCount());
		const double Lap = FMath::Fmod(FMath::Max(Distance, 0.0), Session.TrackLength());
		return FMath::Clamp(FMath::FloorToInt32(Lap / Session.TrackLength() * Sectors), 0, Sectors - 1);
	}
}

TSharedRef<SWidget> UUITrackMapWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		WidgetTree->RootWidget = Canvas;
		MapArea = WidgetTree->ConstructWidget<UBorder>();
		MapArea->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.01f)); // visible, so it takes the clicks
		UCanvasPanelSlot* AreaSlot = Canvas->AddChildToCanvas(MapArea);
		AreaSlot->SetPosition(MapRect.Min);
		AreaSlot->SetSize(MapRect.GetSize());
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

FVector2D UUITrackMapWidget::Across(double Distance) const
{
	const double Step = Director->Session().TrackLength() * 0.003;
	const FVector2D Along = ToMap(Distance + Step) - ToMap(Distance - Step);
	return FVector2D(-Along.Y, Along.X).GetSafeNormal();
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
	static const FSlateRoundedBoxBrush Circle(FLinearColor::White, 16.0f);
	static const FSlateRoundedBoxBrush Ring(FLinearColor::Transparent, 10.0f, FLinearColor::White, 2.0f);
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FUISimSession& Session = Race->Session();
	const FUISimSnapshot& Snapshot = Session.Snapshot();
	const int32 Sectors = Session.MarshalSectorCount();
	const double SectorLength = Session.TrackLength() / FMath::Max(1, Sectors);
	const FPaintGeometry Paint = Map.ToPaintGeometry();

	// Card title and hint, in desk coordinates.
	auto CardText = [&](const FVector2D& At, const FString& String, const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeText(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(FVector2D(600.0, 30.0),
			FSlateLayoutTransform(1.0f, At)), String, Font, ESlateDrawEffect::None, Color);
	};
	++Layer;
	CardText(UIStyle::MapCard.Min + FVector2D(16.0, 13.0), TEXT("Track map"), UIStyle::Font(TEXT("Bold"), 11), UIStyle::Text);
	const FString Hint = TEXT("CLICK SECTOR: CLEAR > YELLOW > DOUBLE");
	const FSlateFontInfo HintFont = UIStyle::Font(TEXT("Regular"), 7);
	CardText(FVector2D(UIStyle::MapCard.Max.X - 16.0 - Measure->Measure(Hint, HintFont).X, UIStyle::MapCard.Min.Y + 17.0), Hint, HintFont,
		UIStyle::Mute);

	auto Box = [&](const FVector2D& Centre, const FVector2D& Size, const FSlateBrush* Brush, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, Layer, Map.ToPaintGeometry(Size, FSlateLayoutTransform(1.0f, Centre - Size / 2.0)), Brush,
			ESlateDrawEffect::None, Color);
	};
	auto Text = [&](const FVector2D& Centre, const FString& String, const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		const FVector2D Size = Measure->Measure(String, Font);
		FSlateDrawElement::MakeText(OutDrawElements, Layer, Map.ToPaintGeometry(Size + FVector2D(4.0, 0.0),
			FSlateLayoutTransform(1.0f, Centre - Size / 2.0)), String, Font, ESlateDrawEffect::None, Color);
	};
	auto SectorPoints = [&](int32 Sector)
	{
		TArray<FVector2D> Points;
		const int32 Steps = FMath::Max(2, TrackSamples / FMath::Max(1, Sectors));
		for (int32 Step = 0; Step <= Steps; ++Step)
		{
			Points.Add(ToMap(SectorLength * (Sector + static_cast<double>(Step) / Steps)));
		}
		return Points;
	};
	auto Lines = [&](const TArray<FVector2D>& Points, const FLinearColor& Color, double Thickness)
	{
		FSlateDrawElement::MakeLines(OutDrawElements, Layer, Paint, Points, ESlateDrawEffect::None, Color, true, Thickness);
	};

	// Dark band under the whole lap.
	++Layer;
	for (int32 Sector = 0; Sector < Sectors; ++Sector)
	{
		Lines(SectorPoints(Sector), TrackBand, TrackWidth);
	}

	// Sectors: grey (every other one fainter), yellow when flagged, dashed for double yellow. Under SC and
	// VSC all of them yellow, under the red flag red, as in the design. The sector picked by key is brighter.
	const bool bAll = Snapshot.State != EUISimRaceState::Green;
	const FLinearColor All = Snapshot.State == EUISimRaceState::RedFlag ? UIStyle::Red : UIStyle::Yellow;
	++Layer;
	for (int32 Sector = 0; Sector < Sectors; ++Sector)
	{
		const EUISimFlag Flag = Snapshot.SectorFlags.IsValidIndex(Sector) ? Snapshot.SectorFlags[Sector] : EUISimFlag::Green;
		const bool bFlagged = Flag != EUISimFlag::Green;
		FLinearColor Color = bFlagged ? UIStyle::Yellow : bAll ? All : Sector == Race->SelectedSector() ? UIStyle::Text : SectorIdle;
		if (!bFlagged && !bAll && Sector != Race->SelectedSector() && Sector % 2 == 0)
		{
			Color.A = 0.65f;
		}
		const TArray<FVector2D> Points = SectorPoints(Sector);
		if (Flag != EUISimFlag::DoubleYellow)
		{
			Lines(Points, Color, bFlagged ? FlaggedWidth : SectorWidth);
		}
		else
		{
			// Dashes along the polyline.
			TArray<FVector2D> Dash = {Points[0]};
			double Done = 0.0; // length of the current dash or gap so far
			bool bOn = true;
			for (int32 Index = 1; Index < Points.Num(); ++Index)
			{
				FVector2D From = Points[Index - 1];
				const FVector2D Direction = (Points[Index] - From).GetSafeNormal();
				double Left = FVector2D::Distance(From, Points[Index]);
				while (Left > 1e-6)
				{
					const double Step = FMath::Min(Left, (bOn ? DashOn : DashOff) - Done);
					From += Direction * Step;
					Done += Step;
					Left -= Step;
					if (bOn)
					{
						Dash.Add(From);
					}
					if (Done >= (bOn ? DashOn : DashOff) - 1e-6)
					{
						if (bOn)
						{
							Lines(Dash, Color, FlaggedWidth);
						}
						Dash = {From};
						bOn = !bOn;
						Done = 0.0;
					}
				}
			}
			if (bOn && Dash.Num() > 1)
			{
				Lines(Dash, Color, FlaggedWidth);
			}
		}
		const double Middle = (Sector + 0.5) * SectorLength;
		Text(ToMap(Middle) + Across(Middle) * LabelOffset, FString::FromInt(Sector + 1),
			UIStyle::Font(bFlagged ? TEXT("Bold") : TEXT("Regular"), 8), bFlagged ? UIStyle::Yellow : UIStyle::Mute);
	}

	// Start/finish line and the pit entry.
	++Layer;
	const FVector2D Line = ToMap(0.0), LineAcross = Across(0.0);
	Lines({Line - LineAcross * 8.4, Line + LineAcross * 8.4}, UIStyle::Text, 2.0);
	Text(Line - LineAcross * 26.0, TEXT("S/F"), UIStyle::Font(TEXT("Bold"), 8), UIStyle::Text);
	const double PitAt = Session.TrackLength() * 0.985;
	Text(ToMap(PitAt) - Across(PitAt) * 37.0 + FVector2D(0.0, 20.0), Snapshot.bPitEntryClosed ? TEXT("PIT CLOSED") : TEXT("PIT"),
		UIStyle::Font(TEXT("Regular"), 8), Snapshot.bPitEntryClosed ? UIStyle::Red : UIStyle::Mute);

	// Accident sites and the Safety Car.
	for (const FUISimHazard& Hazard : Snapshot.Hazards)
	{
		Box(ToMap(Hazard.TrackPosition), FVector2D(9.0, 9.0), White, UIStyle::Red);
	}
	if (Snapshot.bSafetyCarOnTrack)
	{
		const FVector2D At = ToMap(Snapshot.SafetyCarDistance);
		Box(At, FVector2D(19.0, 13.0), White, UIStyle::Yellow);
		++Layer;
		Text(At, TEXT("SC"), UIStyle::Font(TEXT("Bold"), 7), UIStyle::Ink);
	}

	// Cars in team colours, the leader on top; the followed car gets a ring, it and the leader their code.
	++Layer;
	FSlateFontInfo CodeFont = UIStyle::Font(TEXT("Bold"), 9);
	CodeFont.OutlineSettings.OutlineSize = 2;
	CodeFont.OutlineSettings.OutlineColor = UIStyle::Ink;
	for (int32 Index = Snapshot.Cars.Num() - 1; Index >= 0; --Index)
	{
		const FUISimCar& Car = Snapshot.Cars[Index];
		if (Car.bFinished || Car.bRetired || Car.bInPitLane)
		{
			continue;
		}
		const bool bFollowed = Car.EntryIndex == Race->FollowedCar();
		const FVector2D At = ToMap(Car.Distance);
		if (bFollowed)
		{
			Box(At, FVector2D(19.0, 19.0), &Ring, UIStyle::Text);
		}
		Box(At, FVector2D(14.0, 14.0), &Circle, UIStyle::Ink);
		Box(At, FVector2D(11.5, 11.5), &Circle, UIStyle::TeamColor(Session.Entries(), Car.EntryIndex));
		if (bFollowed || Car.Position == 1) // more labels would pile up in the pack
		{
			Text(At + FVector2D(0.0, -14.0), Session.Entries()[Car.EntryIndex].Code.ToUpper(), CodeFont, UIStyle::Text);
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
	if (!Race || !MapArea || !Race->Session().IsStarted() || InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton ||
		!PrepareProjection(MapArea->GetCachedGeometry().GetLocalSize()))
	{
		return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
	}
	const FVector2D Local = FVector2D(MapArea->GetCachedGeometry().AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition()));
	const FUISimSession& Session = Race->Session();

	// 1. The nearest car on track.
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
		return FReply::Handled();
	}

	// 2. The nearest point of the track: its marshal sector gets the next flag.
	const double Length = Session.TrackLength();
	int32 BestSector = -1;
	BestDistance = TrackHitRadius;
	for (int32 Sample = 0; Sample < TrackSamples; ++Sample)
	{
		const double Along = Length * (Sample + 0.5) / TrackSamples;
		const double Distance = FVector2D::Distance(ToMap(Along), Local);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			BestSector = SectorAt(Session, Along);
		}
	}
	if (BestSector >= 0)
	{
		const TArray<EUISimFlag>& Flags = Session.Snapshot().SectorFlags;
		FUICommand Command;
		Command.Type = EUICommand::SetSectorFlag;
		Command.Index = BestSector;
		Command.Flag = NextFlag(Flags.IsValidIndex(BestSector) ? Flags[BestSector] : EUISimFlag::Green);
		Race->Execute(Command);
	}
	return FReply::Handled();
}
