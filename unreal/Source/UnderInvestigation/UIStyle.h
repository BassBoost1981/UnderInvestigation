#pragma once

#include "CoreMinimal.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/SlateFontInfo.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"

// Shared look of the race director's desk (M12): layout of the cards, colours and fonts in one place.
// All positions are design units on a 1920 x 1080 screen; UMG scales them to the window.
namespace UIStyle
{
	inline const FVector2D Screen(1920.0, 1080.0);
	constexpr float CardRadius = 16.0f;
	constexpr double TitleHeight = 30.0; // title strip of the camera picture

	// Cards: top bar, timing and map on the left, camera picture and controls in the middle, incidents on the right.
	inline const FBox2D TopBar(FVector2D(14.0, 14.0), FVector2D(1906.0, 84.0));
	inline const FBox2D TimingCard(FVector2D(14.0, 96.0), FVector2D(392.0, 762.0));
	inline const FBox2D FeedCard(FVector2D(404.0, 96.0), FVector2D(1354.0, 660.0));
	inline const FBox2D Feed(FVector2D(404.0, 96.0 + TitleHeight), FVector2D(1354.0, 660.0)); // the 3D picture, 16:9
	inline const FBox2D MapCard(FVector2D(14.0, 774.0), FVector2D(392.0, 1066.0));
	inline const FBox2D ControlCard(FVector2D(404.0, 672.0), FVector2D(1354.0, 1066.0));
	inline const FBox2D IncidentCard(FVector2D(1366.0, 96.0), FVector2D(1906.0, 1066.0));

	// Colour from sRGB bytes, as written in the design.
	inline FLinearColor Rgb(uint8 R, uint8 G, uint8 B, float Alpha = 1.0f)
	{
		FLinearColor Color(FColor(R, G, B));
		Color.A = Alpha;
		return Color;
	}
	inline const FLinearColor Background = Rgb(10, 11, 14);
	inline const FLinearColor Card = Rgb(21, 22, 26);
	inline const FLinearColor Text = Rgb(230, 232, 235);
	inline const FLinearColor Mute = Rgb(138, 145, 156);
	inline const FLinearColor Yellow = Rgb(245, 197, 24);
	inline const FLinearColor Red = Rgb(229, 56, 59);
	inline const FLinearColor Green = Rgb(47, 191, 113);

	inline FSlateFontInfo Font(const TCHAR* Weight, int32 Size)
	{
		// Lexend (OFL, Content/Fonts) for text; the engine font stays for "Mono" columns of numbers.
		const bool bBold = FCString::Stricmp(Weight, TEXT("Bold")) == 0;
		if (bBold || FCString::Stricmp(Weight, TEXT("Regular")) == 0)
		{
			static TSharedPtr<const FCompositeFont> Lexend[2]; // loaded once per weight
			if (!Lexend[bBold].IsValid())
			{
				const FString File = FPaths::ProjectContentDir() / (bBold ? TEXT("Fonts/Lexend-Bold.ttf") : TEXT("Fonts/Lexend-Regular.ttf"));
				Lexend[bBold] = MakeShared<FStandaloneCompositeFont>(NAME_None, File, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
			}
			return FSlateFontInfo(Lexend[bBold], static_cast<float>(Size));
		}
		return FCoreStyle::GetDefaultFontStyle(Weight, Size);
	}
}
