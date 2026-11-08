#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UITimingTowerWidget.generated.h"

class AUIRaceDirector;
class UTextBlock;

// Timing tower built in C++ (no widget asset yet). Only formats what the
// snapshot already contains; ordering and gaps come from the core.
UCLASS()
class UUITimingTowerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetRaceDirector(AUIRaceDirector* InDirector) { Director = InDirector; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UTextBlock* MakeText(const FLinearColor& Color);

	TWeakObjectPtr<AUIRaceDirector> Director;

	UPROPERTY()
	TObjectPtr<UTextBlock> Header;
	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> Rows;
	UPROPERTY()
	TObjectPtr<UTextBlock> Footer;
};
