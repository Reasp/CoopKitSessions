// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsDataSummary.h"
#include "CKSessionsLobbyRulesData.h"
#include "CKSessionsOnlineSettings.h"
#include "CKSessionsProfileData.h"
#include "Engine/World.h"
#include "OnlineSessionSettings.h"

#define LOCTEXT_NAMESPACE "CKSessionsDataSummary"

namespace CKSessions::Editor
{
	namespace
	{
		FString DescribeScope(const ECKSessionsNetworkScope Scope)
		{
			switch (Scope)
			{
			case ECKSessionsNetworkScope::Lan:
				return TEXT("LAN only, with any online subsystem");
			case ECKSessionsNetworkScope::Online:
				return TEXT("online service (Null falls back to LAN)");
			case ECKSessionsNetworkScope::Auto:
			default:
				return TEXT("LAN with Null, online with Steam, EOS and other services");
			}
		}

		const TCHAR* BoolText(const bool bValue)
		{
			return bValue ? TEXT("true") : TEXT("false");
		}

		/** One line with the settings CreateSession receives for a listen server host on the given subsystem. */
		FString DescribeOnlineSettings(const UCKSessionsProfileData& Profile, const FName SubsystemName)
		{
			FOnlineSessionSettings Settings;
			const bool bLan = Online::ShouldUseLan(Profile.GetNetworkScope(), SubsystemName);
			Online::BuildSessionSettings(Profile, bLan, Online::EHostKind::ListenServer, TEXT("/Game/Maps/Map"), Settings);
			return FString::Printf(TEXT("bIsLANMatch=%s, bShouldAdvertise=%s, NumPublicConnections=%d, NumPrivateConnections=%d, bUsesPresence=%s, bUseLobbiesIfAvailable=%s"),
				BoolText(Settings.bIsLANMatch), BoolText(Settings.bShouldAdvertise), Settings.NumPublicConnections,
				Settings.NumPrivateConnections, BoolText(Settings.bUsesPresence), BoolText(Settings.bUseLobbiesIfAvailable));
		}
	}

	FText BuildProfileDataSummary(const UObject& Data)
	{
		const UCKSessionsProfileData* Profile = Cast<UCKSessionsProfileData>(&Data);
		if (Profile == nullptr)
		{
			return FText::Format(LOCTEXT("NotProfile", "{0} is not a session profile."), FText::FromString(Data.GetName()));
		}

		TArray<FString> Lines;
		Lines.Add(FString::Printf(TEXT("Session type: %s (searches only find sessions of the same type)"), *Profile->GetSessionType().ToString()));
		Lines.Add(FString::Printf(TEXT("Players: up to %d, the host included"), Profile->GetMaxPlayers()));
		Lines.Add(Profile->IsPrivate()
			? FString(TEXT("Visibility: private, not found by Find Sessions; players join through invites of the online service"))
			: FString(TEXT("Visibility: public, found by Find Sessions")));
		Lines.Add(FString::Printf(TEXT("Network: %s"), *DescribeScope(Profile->GetNetworkScope())));
		Lines.Add(Profile->UsesLobbies()
			? FString(TEXT("Lobbies: service lobbies when a player hosts online; game sessions on LAN and on dedicated servers"))
			: FString(TEXT("Lobbies: off, always game sessions")));
		Lines.Add(Profile->AllowsJoinInProgress()
			? FString(TEXT("Join in progress: allowed, the session stays open after Start Match"))
			: FString(TEXT("Join in progress: refused, only players of this session may come back after Start Match")));
		Lines.Add(Profile->GetReconnectAttempts() > 0
			? FString::Printf(TEXT("Reconnect: %d attempts, %.1f s apart; the host keeps a player's record for %.0f s"),
				Profile->GetReconnectAttempts(), Profile->GetReconnectIntervalSeconds(), Profile->GetReconnectGraceSeconds())
			: FString::Printf(TEXT("Reconnect: off; the host keeps a player's record for %.0f s"), Profile->GetReconnectGraceSeconds()));
		Lines.Add(FString::Printf(TEXT("Search: up to %d results"), Profile->GetMaxSearchResults()));

		if (Profile->GetAttributes().IsEmpty())
		{
			Lines.Add(TEXT("Attributes: none"));
		}
		else
		{
			TArray<FString> Pairs;
			for (const TPair<FName, FString>& Attribute : Profile->GetAttributes())
			{
				Pairs.Add(FString::Printf(TEXT("%s = %s"), *Attribute.Key.ToString(), *Attribute.Value));
			}
			Lines.Add(FString::Printf(TEXT("Attributes: %s"), *FString::Join(Pairs, TEXT(", "))));
		}

		Lines.Add(TEXT(""));
		Lines.Add(TEXT("Online subsystem settings for a listen server host:"));
		Lines.Add(FString::Printf(TEXT("  Null: %s"), *DescribeOnlineSettings(*Profile, Online::NullSubsystemName)));
		Lines.Add(FString::Printf(TEXT("  Steam, EOS: %s"), *DescribeOnlineSettings(*Profile, FName(TEXT("STEAM")))));
		Lines.Add(FString::Printf(TEXT("Primary asset id: %s"), *Profile->GetPrimaryAssetId().ToString()));
		return FText::FromString(FString::Join(Lines, TEXT("\n")));
	}

	FText BuildLobbyRulesDataSummary(const UObject& Data)
	{
		const UCKSessionsLobbyRulesData* Rules = Cast<UCKSessionsLobbyRulesData>(&Data);
		if (Rules == nullptr)
		{
			return FText::Format(LOCTEXT("NotLobbyRules", "{0} is not lobby rules."), FText::FromString(Data.GetName()));
		}

		const FString MapName = Rules->GetMatchMap().IsNull() ? FString(TEXT("(no match map)")) : Rules->GetMatchMap().GetLongPackageName();
		TArray<FString> Lines;
		Lines.Add(FString::Printf(TEXT("1. Waiting: at least %d %s; %s."), Rules->GetMinPlayersToStart(),
			Rules->GetMinPlayersToStart() == 1 ? TEXT("player") : TEXT("players"),
			Rules->RequiresAllReady() ? TEXT("every player must be ready") : TEXT("enough ready players start the countdown")));
		Lines.Add(Rules->GetCountdownSeconds() > 0.0f
			? FString::Printf(TEXT("2. Countdown: %.1f s; a player who stops being ready cancels it."), Rules->GetCountdownSeconds())
			: FString(TEXT("2. Countdown: none, the lobby travels at once.")));
		Lines.Add(FString::Printf(TEXT("3. Travel: %s%s, seamless when the game mode uses seamless travel; the session is marked as in progress."),
			*MapName, *Rules->GetTravelOptions()));
		Lines.Add(Rules->CanHostForceStart()
			? FString(TEXT("The host may start early, before everyone is ready."))
			: FString(TEXT("The host cannot start early.")));
		Lines.Add(Rules->AddsPlayerComponents()
			? FString(TEXT("Player states without the ready component get it from the lobby."))
			: FString(TEXT("Add the ready component to your player state yourself.")));
		Lines.Add(FString::Printf(TEXT("Primary asset id: %s"), *Rules->GetPrimaryAssetId().ToString()));
		return FText::FromString(FString::Join(Lines, TEXT("\n")));
	}
}

#undef LOCTEXT_NAMESPACE
