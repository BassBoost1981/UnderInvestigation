#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UIIncidentWidget.generated.h"

class AUIRaceDirector;
class UImage;
class USizeBox;
class UTextBlock;
class UVerticalBox;

// Incident list (top right). While reviewing it also shows the second camera
// (picture-in-picture) and a speed trace of both cars around the incident.
UCLASS()
class UUIIncidentWidget : public UUserWidget
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

private:
	UTextBlock* MakeText(const FLinearColor& Color, int32 Size = 13);

	TWeakObjectPtr<AUIRaceDirector> Director;

	UPROPERTY()
	TObjectPtr<UTextBlock> Title;
	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> Rows;
	UPROPERTY()
	TObjectPtr<UVerticalBox> ReviewSection;
	UPROPERTY()
	TObjectPtr<UTextBlock> ReviewInfo;
	UPROPERTY()
	TObjectPtr<UImage> PipImage;
	UPROPERTY()
	TObjectPtr<USizeBox> PlotArea;
	UPROPERTY()
	TObjectPtr<UTextBlock> PlotLegend;
	UPROPERTY()
	TObjectPtr<UTextBlock> ReviewControls;
};
