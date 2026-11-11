#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UIRaceDirector.h"
#include "UIControlPanelWidget.generated.h"

class UBorder;

// Race control panel (bottom centre, M8.6): buttons for VSC, Safety Car, red flag / restart,
// lapped cars, pit entry, cameras and time scale. Each button shows its state (active, not
// allowed) and its key; the red flag asks for confirmation. Actions go through Execute().
UCLASS()
class UUIControlPanelWidget : public UUserWidget
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
	enum class EKind : uint8 { Command, AskRedFlag, ConfirmYes, ConfirmNo, ToggleWet, ToggleStanding };

	struct FButton
	{
		FBox2D Rect;
		FString Label;
		FUICommand Command;
		EKind Kind = EKind::Command;
		bool bEnabled = true;
		bool bActive = false;
		FLinearColor ActiveColor = FLinearColor(1.0f, 0.85f, 0.2f);
	};

	TArray<FButton> Buttons() const; // in ButtonArea coordinates, for the current state
	bool IsConfirming() const;
	void LogClickTargets(const FGeometry& Area) const;

	TWeakObjectPtr<AUIRaceDirector> Director;

	UPROPERTY()
	TObjectPtr<UBorder> ButtonArea;

	bool bWetMandate = false; // SC start and restart with wet tyres compulsory (B6.3.7)
	bool bStanding = false;   // standing start when the SC comes in after formation laps or a red flag (B5.10.8, B5.15.4)
	double ConfirmUntil = 0.0; // real time (FPlatformTime::Seconds) until the red flag question expires
	mutable bool bTargetsLogged = false;
};
