// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsLobbyRulesData.h"
#include "Engine/World.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "CKSessionsLobbyRulesData"

#if WITH_EDITOR
EDataValidationResult UCKSessionsLobbyRulesData::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (MatchMap.IsNull())
	{
		Context.AddError(LOCTEXT("NoMatchMap", "Match Map is empty: the lobby would have nowhere to travel."));
		Result = EDataValidationResult::Invalid;
	}
	if (MinPlayersToStart < 1)
	{
		Context.AddError(FText::Format(LOCTEXT("MinPlayers", "Min Players To Start is {0}: it must be at least 1."), FText::AsNumber(MinPlayersToStart)));
		Result = EDataValidationResult::Invalid;
	}
	if (CountdownSeconds < 0.0f)
	{
		Context.AddError(FText::Format(LOCTEXT("Countdown", "Countdown Seconds is {0}: it must not be negative."), FText::AsNumber(CountdownSeconds)));
		Result = EDataValidationResult::Invalid;
	}
	if (!TravelOptions.IsEmpty() && !TravelOptions.StartsWith(TEXT("?")))
	{
		Context.AddError(FText::Format(LOCTEXT("TravelOptionsPrefix", "Travel Options \"{0}\" must start with '?', e.g. ?game=/Game/MyGameMode.MyGameMode_C."), FText::FromString(TravelOptions)));
		Result = EDataValidationResult::Invalid;
	}
	if (TravelOptions.Contains(TEXT(" ")))
	{
		Context.AddError(FText::Format(LOCTEXT("TravelOptionsSpace", "Travel Options \"{0}\" contain a space: travel URLs cannot carry spaces."), FText::FromString(TravelOptions)));
		Result = EDataValidationResult::Invalid;
	}
	if (TravelOptions.Contains(TEXT("listen"), ESearchCase::IgnoreCase))
	{
		Context.AddWarning(LOCTEXT("TravelOptionsListen", "Travel Options contain 'listen': the lobby keeps the current server mode by itself, the option is not needed."));
	}

	return Result == EDataValidationResult::NotValidated ? EDataValidationResult::Valid : Result;
}
#endif

#undef LOCTEXT_NAMESPACE
