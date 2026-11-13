#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UITimingTowerWidget.generated.h"

class AUIRaceDirector;
class UBorder;

// Live timing (M12.3), drawn in C++ after the user's design: one row per car with position, team
// colour, driver code, gap to the leader, interval, last and best lap, the three sector bars, tyre,
// pit stops and a penalty or status token. "IN PIT" stands in the gap column only while the car is
// in the pit lane. A click on a row puts the TV camera on that car.
// Only formats what the snapshot already contains; ordering, gaps and times come from the core.
UCLASS()
class UUITimingTowerWidget : public UUserWidget
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
	TWeakObjectPtr<AUIRaceDirector> Director;

	UPROPERTY()
	TObjectPtr<UBorder> TableArea; // the whole card; painted on, takes the clicks

	mutable FVector2D LoggedAt = FVector2D(-1.0, -1.0); // position of the card last logged
};
