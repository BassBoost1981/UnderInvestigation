#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UIRatingWidget.generated.h"

class AUIRaceDirector;
class UBorder;
class UTextBlock;

// Rating screen after the flag: score per category and, for every incident,
// the verdict given next to what really happened (ground truth revealed).
// Buttons page through the incidents (M8.7).
UCLASS()
class UUIRatingWidget : public UUserWidget
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
	TWeakObjectPtr<AUIRaceDirector> Director;

	UPROPERTY()
	TObjectPtr<UBorder> Panel;
	UPROPERTY()
	TObjectPtr<UTextBlock> Body;
	UPROPERTY()
	TObjectPtr<UBorder> ButtonArea;

	int32 Page = 0;
	int32 PageCount = 1;
	mutable FVector2D LoggedAt = FVector2D(-1.0, -1.0); // button area position last logged
};
