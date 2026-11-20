#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UISeasonWidget.generated.h"

class AUIRaceDirector;
class UBorder;

// Season screen (M10.6), shown between the races: calendar with the ratings of the races run,
// the next race, both championships with penalty points and suspensions, and the button that
// starts the next race. Reads FUISimCareer; the actions go through AUIRaceDirector::Execute().
// Drawn as cards in the style of the desk (M12.8).
UCLASS()
class UUISeasonWidget : public UUserWidget
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
	TObjectPtr<UBorder> Panel; // covers the whole view and takes the clicks

	mutable FVector2D LoggedAt = FVector2D(-1.0, -1.0); // viewport pixels of the button last logged
};
