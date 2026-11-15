#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UITrackMapWidget.generated.h"

class AUIRaceDirector;
class UBorder;

// Track map card (M12.5, after the card "Track map" of the user's design): marshal sectors with
// their numbers, yellow when flagged (dashed for double yellow), start/finish, pit entry, Safety
// Car, accident sites and the cars in team colours. A click on a sector switches its flag on
// (clear, yellow, double yellow), a click on a car follows it with the TV camera. All actions go
// through AUIRaceDirector::Execute(). While an incident is reviewed, the review card takes its place.
UCLASS()
class UUITrackMapWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetRaceDirector(AUIRaceDirector* InDirector) { Director = InDirector; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	// Track point (metres along the lap) in map coordinates (local to MapArea).
	FVector2D ToMap(double Distance) const;
	// Unit vector across the track at that point, in map coordinates (as the design's normal).
	FVector2D Across(double Distance) const;
	bool PrepareProjection(const FVector2D& Size) const;
	void LogClickTargets(const FGeometry& Map) const;

	TWeakObjectPtr<AUIRaceDirector> Director;

	UPROPERTY()
	TObjectPtr<UBorder> MapArea;

	// Projection, rebuilt when the map size changes.
	mutable FVector2D ProjectedSize = FVector2D::ZeroVector;
	mutable FVector2D WorldMin = FVector2D::ZeroVector; // world X, Y in cm
	mutable double Scale = 0.0;
	mutable FVector2D Offset = FVector2D::ZeroVector;
	mutable bool bTargetsLogged = false;
};
