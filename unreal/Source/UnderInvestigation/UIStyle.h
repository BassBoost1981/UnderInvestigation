#pragma once

#include "CoreMinimal.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/SlateFontInfo.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"

// Shared look of the race director's desk (M12): layout of the cards, colours and fonts in one place.
// Positions and sizes follow the user's design "Race Control Simulator v3 Modern" on a 1920 x 1080
// screen; UMG scales them to the window. Font sizes are points (design pixels x 0.75).
namespace UIStyle
{
	inline const FVector2D Screen(1920.0, 1080.0);
	constexpr float CardRadius = 16.0f;

	// Top bar: title, status banner, lap / session / sim speed, weather.
	inline const FBox2D TopTitle(FVector2D(14.0, 14.0), FVector2D(388.0, 86.0));
	inline const FBox2D TopBanner(FVector2D(401.0, 14.0), FVector2D(951.0, 86.0));
	inline const FBox2D TopSession(FVector2D(964.0, 14.0), FVector2D(1478.0, 86.0));
	inline const FBox2D TopWeather(FVector2D(1491.0, 14.0), FVector2D(1906.0, 86.0));

	// Left: live timing, below it the place of the strategy card (until M12.7: marshal radio).
	inline const FBox2D TimingCard(FVector2D(14.0, 98.0), FVector2D(758.0, 778.0));
	inline const FBox2D LowerLeftCard(FVector2D(14.0, 790.0), FVector2D(758.0, 1066.0));
	// Middle: video matrix with the 3D picture and the camera tiles, below it the track map.
	inline const FBox2D VideoCard(FVector2D(771.0, 98.0), FVector2D(1406.0, 642.0));
	inline const FBox2D Feed(FVector2D(784.0, 141.0), FVector2D(1393.0, 484.0)); // the 3D picture, 16:9
	inline const FBox2D Tiles(FVector2D(784.0, 492.0), FVector2D(1393.0, 630.0)); // until M12.5: control panel
	inline const FBox2D MapCard(FVector2D(771.0, 655.0), FVector2D(1406.0, 1066.0));
	// Right: track status, incidents and messages; until M12.4 / M12.6 one card with the incident list.
	inline const FBox2D IncidentCard(FVector2D(1419.0, 98.0), FVector2D(1906.0, 1066.0));

	// Colour from sRGB bytes, as written in the design.
	inline FLinearColor Rgb(uint8 R, uint8 G, uint8 B, float Alpha = 1.0f)
	{
		FLinearColor Color(FColor(R, G, B));
		Color.A = Alpha;
		return Color;
	}
	inline const FLinearColor Background = Rgb(10, 11, 14);
	inline const FLinearColor Card = Rgb(21, 22, 26);       // white at 4.5 % on the background
	inline const FLinearColor CardBorder = Rgb(42, 43, 47); // white at 9 % on the card
	inline const FLinearColor Text = Rgb(230, 232, 235);
	inline const FLinearColor Mute = Rgb(138, 145, 156);
	inline const FLinearColor Ink = Rgb(11, 13, 16); // text on bright fills
	inline const FLinearColor Yellow = Rgb(245, 197, 24);
	inline const FLinearColor Red = Rgb(229, 56, 59);
	inline const FLinearColor Green = Rgb(47, 191, 113);

	// Lexend (OFL, Content/Fonts) for text; the engine font stays for "Mono" columns of numbers.
	// LetterSpacing is in 1/1000 em, as in the design (.04em = 40).
	inline FSlateFontInfo Font(const TCHAR* Weight, int32 Size, int32 LetterSpacing = 0)
	{
		const bool bBold = FCString::Stricmp(Weight, TEXT("Bold")) == 0;
		if (bBold || FCString::Stricmp(Weight, TEXT("Regular")) == 0)
		{
			static TSharedPtr<const FCompositeFont> Lexend[2]; // loaded once per weight
			if (!Lexend[bBold].IsValid())
			{
				const FString File = FPaths::ProjectContentDir() / (bBold ? TEXT("Fonts/Lexend-Bold.ttf") : TEXT("Fonts/Lexend-Regular.ttf"));
				Lexend[bBold] = MakeShared<FStandaloneCompositeFont>(NAME_None, File, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
			}
			FSlateFontInfo Info(Lexend[bBold], static_cast<float>(Size));
			Info.LetterSpacing = LetterSpacing;
			return Info;
		}
		return FCoreStyle::GetDefaultFontStyle(Weight, Size);
	}
}
