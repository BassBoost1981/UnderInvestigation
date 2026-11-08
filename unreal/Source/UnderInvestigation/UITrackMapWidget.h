#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UITrackMapWidget.generated.h"

class AUIRaceDirector;
class UBorder;

// Track map (bottom right, M8.5): marshal sectors in their flag colour, cars, Safety Car and
// accident sites. Click a sector for the flag menu (green, yellow, double yellow), click a car
// to follow it with the TV camera. All actions go through AUIRaceDirector::Execute().
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
	bool PrepareProjection(const FVector2D& Size) const;
	FBox2D MenuRow(int32 Row) const; // 0 green, 1 yellow, 2 double yellow
	void LogClickTargets(const FGeometry& Map) const;

	TWeakObjectPtr<AUIRaceDirector> Director;

	UPROPERTY()
	TObjectPtr<UBorder> MapArea;

	int32 MenuSector = -1; // flag menu open for this marshal sector

	// Projection, rebuilt when the map size changes.
	mutable FVector2D ProjectedSize = FVector2D::ZeroVector;
	mutable FVector2D WorldMin = FVector2D::ZeroVector; // world X, Y in cm
	mutable double Scale = 0.0;
	mutable FVector2D Offset = FVector2D::ZeroVector;
	mutable bool bTargetsLogged = false;
};
