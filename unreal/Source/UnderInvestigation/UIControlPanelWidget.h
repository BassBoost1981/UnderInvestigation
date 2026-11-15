#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UIRaceDirector.h"
#include "UIControlPanelWidget.generated.h"

class UBorder;

// Race control (M12.4), after the card "Track status" of the user's design: the six big state
// buttons, OVERTAKE and PIT LANE, the marshal sectors, the start lights with the restart button,
// and below them the commands the design does not know. It also draws the camera tiles under
// the 3D picture (name, cars in the frame, flag dot). The red flag needs a second click.
// Actions go through Execute().
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
	enum class EKind : uint8 { Command, AskRedFlag, ClearTrack, ToggleWet, ToggleStanding, Display };
	enum class ELook : uint8 { Status, Toggle, Sector, Capsule, Small, Tile };

	struct FButton
	{
		FBox2D Rect;       // in the coordinates of its area
		FString Label;
		FString Sub;       // second line (Status, Tile) or the state chip (Toggle)
		FUICommand Command;
		EKind Kind = EKind::Command;
		ELook Look = ELook::Status;
		bool bTile = false;   // sits in the tiles area, not in the status card
		bool bEnabled = true;
		bool bActive = false;
		bool bDarkInk = true; // text colour on the active fill
		FLinearColor Color = FLinearColor::White; // fill when active, colour of the dot or chip
		EUISimFlag Flag = EUISimFlag::Green;      // Sector: the flag shown
	};

	TArray<FButton> Buttons() const; // for the current state
	bool IsConfirming() const;
	void LogClickTargets() const;

	TWeakObjectPtr<AUIRaceDirector> Director;

	UPROPERTY()
	TObjectPtr<UBorder> StatusArea; // the card "Track status"
	UPROPERTY()
	TObjectPtr<UBorder> TilesArea;  // camera tiles under the 3D picture

	bool bWetMandate = false; // SC start and restart with wet tyres compulsory (B6.3.7)
	bool bStanding = false;   // standing start when the SC comes in after formation laps (B5.10.8)
	double ConfirmUntil = 0.0; // real time (FPlatformTime::Seconds) until the red flag question expires
	mutable bool bTargetsLogged = false;
};
