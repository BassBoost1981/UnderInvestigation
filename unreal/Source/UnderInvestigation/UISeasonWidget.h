#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UISeasonWidget.generated.h"

class AUIRaceDirector;
class UBorder;
class UTextBlock;

// Season screen (M10.6), shown between the races: calendar with the ratings of the races run,
// the next race, both championships with penalty points and suspensions, and the button that
// starts the next race. Reads FUISimCareer; the actions go through AUIRaceDirector::Execute().
UCLASS()
class UUISeasonWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetRaceDirector(AUIRaceDirector* InDirector) { Director = InDirector; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	UTextBlock* MakeText(int32 Size, const FLinearColor& Color);

	TWeakObjectPtr<AUIRaceDirector> Director;

	UPROPERTY()
	TObjectPtr<UTextBlock> Header;
	UPROPERTY()
	TObjectPtr<UTextBlock> CalendarText;
	UPROPERTY()
	TObjectPtr<UTextBlock> DriversText;
	UPROPERTY()
	TObjectPtr<UTextBlock> TeamsText;
	UPROPERTY()
	TObjectPtr<UBorder> ButtonArea;

	mutable FVector2D LoggedAt = FVector2D(-1.0, -1.0); // button position last logged
};
