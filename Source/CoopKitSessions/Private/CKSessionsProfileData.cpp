// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsProfileData.h"
#include "CKSessionsOnlineSettings.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "CKSessionsProfileData"

bool UCKSessionsProfileData::IsReservedAttributeKey(const FName Key)
{
	return CKSessions::Online::IsReservedKey(Key);
}

#if WITH_EDITOR
EDataValidationResult UCKSessionsProfileData::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (SessionType.IsNone())
	{
		Context.AddError(LOCTEXT("NoSessionType", "Session Type is empty: searches could not tell this kind of session from others."));
		Result = EDataValidationResult::Invalid;
	}
	if (MaxPlayers < 2 || MaxPlayers > 64)
	{
		Context.AddError(FText::Format(LOCTEXT("MaxPlayers", "Max Players is {0}: a co-op session needs 2 to 64 players."), FText::AsNumber(MaxPlayers)));
		Result = EDataValidationResult::Invalid;
	}
	if (ReconnectAttempts < 0)
	{
		Context.AddError(FText::Format(LOCTEXT("ReconnectAttempts", "Reconnect Attempts is {0}: it must not be negative (0 turns reconnection off)."), FText::AsNumber(ReconnectAttempts)));
		Result = EDataValidationResult::Invalid;
	}
	if (ReconnectAttempts > 0 && ReconnectIntervalSeconds <= 0.0f)
	{
		Context.AddError(FText::Format(LOCTEXT("ReconnectInterval", "Reconnect Interval Seconds is {0}: reconnect attempts need a positive pause."), FText::AsNumber(ReconnectIntervalSeconds)));
		Result = EDataValidationResult::Invalid;
	}
	if (ReconnectGraceSeconds < 0.0f)
	{
		Context.AddError(FText::Format(LOCTEXT("ReconnectGrace", "Reconnect Grace Seconds is {0}: it must not be negative."), FText::AsNumber(ReconnectGraceSeconds)));
		Result = EDataValidationResult::Invalid;
	}
	if (ReconnectAttempts > 0 && ReconnectGraceSeconds <= 0.0f)
	{
		Context.AddWarning(LOCTEXT("ReconnectNoGrace", "Reconnect Grace Seconds is 0: clients try to reconnect, but the host forgets them at once, so a returning player counts as a new one."));
	}
	if (MaxSearchResults < 1)
	{
		Context.AddError(FText::Format(LOCTEXT("MaxSearchResults", "Max Search Results is {0}: it must be at least 1."), FText::AsNumber(MaxSearchResults)));
		Result = EDataValidationResult::Invalid;
	}
	if (bPrivate && ReconnectAttempts > 0)
	{
		Context.AddWarning(LOCTEXT("PrivateReconnect", "Private sessions are not searchable: a reconnecting client travels straight to the last host address, which works on LAN and IP connections but not through every service relay."));
	}
	for (const TPair<FName, FString>& Attribute : Attributes)
	{
		if (Attribute.Key.IsNone())
		{
			Context.AddError(LOCTEXT("EmptyAttributeKey", "An attribute has an empty key."));
			Result = EDataValidationResult::Invalid;
		}
		else if (IsReservedAttributeKey(Attribute.Key))
		{
			Context.AddError(FText::Format(LOCTEXT("ReservedAttributeKey", "Attribute {0} reuses a key of the plugin or the engine; rename it."), FText::FromName(Attribute.Key)));
			Result = EDataValidationResult::Invalid;
		}
	}

	return Result == EDataValidationResult::NotValidated ? EDataValidationResult::Valid : Result;
}
#endif

#undef LOCTEXT_NAMESPACE
