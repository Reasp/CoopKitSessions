// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsOnlineSettings.h"
#include "CKSessionsCompat.h"
#include "CKSessionsProfileData.h"
#include "CKSessionsTypes.h"
#include "Online/OnlineSessionNames.h"
#include "OnlineSessionSettings.h"

namespace CKSessions::Online
{
	const FName SessionTypeKey(TEXT("CKS_TYPE"));
	const FName MatchInProgressKey(TEXT("CKS_INMATCH"));
	const FName NullSubsystemName(TEXT("NULL"));

	bool IsReservedKey(const FName Key)
	{
		if (Key.ToString().StartsWith(TEXT("CKS_")))
		{
			return true;
		}
		// Engine session names (Online/OnlineSessionNames.h) that services read themselves.
		static const FName EngineKeys[] = {
			SETTING_MAPNAME, SETTING_GAMEMODE, SETTING_NUMBOTS, SETTING_MATCHING_HOPPER, SETTING_MATCHING_TIMEOUT,
			SETTING_SESSION_TEMPLATE_NAME, SETTING_CUSTOMSEARCHINT1, SEARCH_KEYWORDS, SEARCH_DEDICATED_ONLY,
			SEARCH_EMPTY_SERVERS_ONLY, SEARCH_SECURE_SERVERS_ONLY, SEARCH_LOBBIES, Compat::GetLegacyPresenceSearchKey()
		};
		for (const FName& EngineKey : EngineKeys)
		{
			if (Key == EngineKey)
			{
				return true;
			}
		}
		return false;
	}

	bool IsValidAttributeKey(const FName Key)
	{
		return !Key.IsNone() && !IsReservedKey(Key);
	}

	bool AreValidAttributeKeys(const TMap<FName, FString>& Attributes, FName& OutInvalidKey)
	{
		for (const TPair<FName, FString>& Attribute : Attributes)
		{
			if (!IsValidAttributeKey(Attribute.Key))
			{
				OutInvalidKey = Attribute.Key;
				return false;
			}
		}
		return true;
	}

	TMap<FName, FString> MergeAttributes(const UCKSessionsProfileData& Profile, const TMap<FName, FString>& Overrides)
	{
		TMap<FName, FString> Merged = Profile.GetAttributes();
		Merged.Append(Overrides);
		return Merged;
	}

	void ApplyAttributes(FOnlineSessionSettings& Settings, const TMap<FName, FString>& Attributes)
	{
		for (const TPair<FName, FString>& Attribute : Attributes)
		{
			Settings.Set(Attribute.Key, Attribute.Value, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
		}
	}

	TMap<FName, FString> ReadAttributes(const FOnlineSessionSettings& Settings)
	{
		TMap<FName, FString> Attributes;
		for (const TPair<FName, FOnlineSessionSetting>& Setting : Settings.Settings)
		{
			if (IsValidAttributeKey(Setting.Key) && Setting.Value.Data.GetType() == EOnlineKeyValuePairDataType::String)
			{
				FString Value;
				Setting.Value.Data.GetValue(Value);
				Attributes.Add(Setting.Key, MoveTemp(Value));
			}
		}
		return Attributes;
	}

	bool MatchesAttributes(const FOnlineSessionSettings& Settings, const TMap<FName, FString>& Filter)
	{
		for (const TPair<FName, FString>& Wanted : Filter)
		{
			FString Value;
			if (!Settings.Get(Wanted.Key, Value) || !Value.Equals(Wanted.Value, ESearchCase::CaseSensitive))
			{
				return false;
			}
		}
		return true;
	}

	int32 PickQuickPlayResult(const TArray<FOnlineSessionSearchResult>& Results)
	{
		int32 Best = INDEX_NONE;
		int32 BestOpenSlots = MAX_int32;
		int32 BestPing = MAX_int32;
		for (int32 Index = 0; Index < Results.Num(); ++Index)
		{
			const FOnlineSessionSearchResult& Result = Results[Index];
			const int32 OpenSlots = GetOpenSlots(Result);
			if (OpenSlots < BestOpenSlots || (OpenSlots == BestOpenSlots && Result.PingInMs < BestPing))
			{
				Best = Index;
				BestOpenSlots = OpenSlots;
				BestPing = Result.PingInMs;
			}
		}
		return Best;
	}

	int32 GetOpenSlots(const FOnlineSessionSearchResult& Result)
	{
		return Result.Session.NumOpenPublicConnections + Result.Session.NumOpenPrivateConnections;
	}

	int32 PickQuickPlayResultForGroup(const TArray<FOnlineSessionSearchResult>& Results, const int32 GroupSize)
	{
		const int32 Needed = FMath::Max(GroupSize, 1);
		int32 Best = INDEX_NONE;
		int32 BestOpenSlots = MAX_int32;
		int32 BestPing = MAX_int32;
		for (int32 Index = 0; Index < Results.Num(); ++Index)
		{
			const int32 OpenSlots = GetOpenSlots(Results[Index]);
			if (OpenSlots < Needed)
			{
				continue;
			}
			if (OpenSlots < BestOpenSlots || (OpenSlots == BestOpenSlots && Results[Index].PingInMs < BestPing))
			{
				Best = Index;
				BestOpenSlots = OpenSlots;
				BestPing = Results[Index].PingInMs;
			}
		}
		return Best;
	}

	namespace
	{
		const TCHAR* const KickPrefix = TEXT("Kicked from the session: ");
		const TCHAR* const BanPrefix = TEXT("Banned from the session: ");
	}

	FString MakeRemovalMessage(const bool bBan, const FString& Reason)
	{
		const FString Shown = Reason.IsEmpty() ? FString(TEXT("No reason given.")) : Reason;
		return FString(bBan ? BanPrefix : KickPrefix) + Shown;
	}

	bool ParseRemovalMessage(const FString& Message, bool& bOutBan, FString& OutReason)
	{
		if (Message.StartsWith(KickPrefix))
		{
			bOutBan = false;
			OutReason = Message.RightChop(FCString::Strlen(KickPrefix));
			return true;
		}
		if (Message.StartsWith(BanPrefix))
		{
			bOutBan = true;
			OutReason = Message.RightChop(FCString::Strlen(BanPrefix));
			return true;
		}
		return false;
	}

	bool ShouldUseLan(const ECKSessionsNetworkScope Scope, const FName SubsystemName)
	{
		switch (Scope)
		{
		case ECKSessionsNetworkScope::Lan:
			return true;
		case ECKSessionsNetworkScope::Online:
		case ECKSessionsNetworkScope::Auto:
		default:
			// Null has no online service: its sessions are only visible on the LAN.
			return SubsystemName == NullSubsystemName;
		}
	}

	void BuildSessionSettings(const UCKSessionsProfileData& Profile, const bool bLan, const EHostKind Host, const FString& MapName, FOnlineSessionSettings& OutSettings)
	{
		BuildSessionSettings(Profile, bLan, Host, MapName, TMap<FName, FString>(), OutSettings);
	}

	void BuildSessionSettings(const UCKSessionsProfileData& Profile, const bool bLan, const EHostKind Host, const FString& MapName,
		const TMap<FName, FString>& Overrides, FOnlineSessionSettings& OutSettings)
	{
		OutSettings = FOnlineSessionSettings();

		const bool bDedicated = Host == EHostKind::DedicatedServer;
		// Presence and lobbies belong to a player host on an online service.
		const bool bPresence = Profile.UsesLobbies() && !bLan && !bDedicated;

		OutSettings.bIsLANMatch = bLan;
		OutSettings.bIsDedicated = bDedicated;
		OutSettings.bShouldAdvertise = !Profile.IsPrivate();
		OutSettings.NumPublicConnections = Profile.IsPrivate() ? 0 : Profile.GetMaxPlayers();
		OutSettings.NumPrivateConnections = Profile.IsPrivate() ? Profile.GetMaxPlayers() : 0;
		OutSettings.bAllowJoinInProgress = true;
		OutSettings.bAllowInvites = true;
		OutSettings.bUsesPresence = bPresence;
		OutSettings.bAllowJoinViaPresence = bPresence;
		OutSettings.bUseLobbiesIfAvailable = bPresence;
		OutSettings.bUsesStats = false;

		OutSettings.Set(SETTING_MAPNAME, MapName, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
		OutSettings.Set(SessionTypeKey, Profile.GetSessionType().ToString(), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
		ApplyAttributes(OutSettings, MergeAttributes(Profile, Overrides));
		SetMatchInProgress(OutSettings, false, Profile.AllowsJoinInProgress());
	}

	void BuildSearch(const UCKSessionsProfileData& Profile, const bool bLan, FOnlineSessionSearch& OutSearch)
	{
		BuildSearch(Profile, bLan, TMap<FName, FString>(), OutSearch);
	}

	void BuildSearch(const UCKSessionsProfileData& Profile, const bool bLan, const TMap<FName, FString>& Filter, FOnlineSessionSearch& OutSearch)
	{
		OutSearch.bIsLanQuery = bLan;
		OutSearch.MaxSearchResults = Profile.GetMaxSearchResults();
		OutSearch.QuerySettings = FOnlineSearchSettings();
		Compat::SetLobbySearch(OutSearch.QuerySettings, Profile.UsesLobbies() && !bLan);
		OutSearch.QuerySettings.Set(SessionTypeKey, Profile.GetSessionType().ToString(), EOnlineComparisonOp::Equals);
		for (const TPair<FName, FString>& Wanted : Filter)
		{
			OutSearch.QuerySettings.Set(Wanted.Key, Wanted.Value, EOnlineComparisonOp::Equals);
		}
	}

	void SetMatchInProgress(FOnlineSessionSettings& Settings, const bool bInProgress, const bool bAllowJoinInProgress)
	{
		Settings.Set(MatchInProgressKey, bInProgress, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
		Settings.bAllowJoinInProgress = !bInProgress || bAllowJoinInProgress;
	}

	bool IsMatchInProgress(const FOnlineSessionSettings& Settings)
	{
		bool bInProgress = false;
		Settings.Get(MatchInProgressKey, bInProgress);
		return bInProgress;
	}

	bool IsJoinable(const FOnlineSessionSearchResult& Result, const UCKSessionsProfileData& Profile)
	{
		if (!Result.IsValid())
		{
			return false;
		}
		const FOnlineSessionSettings& Settings = Result.Session.SessionSettings;

		FString SessionType;
		if (!Settings.Get(SessionTypeKey, SessionType) || SessionType != Profile.GetSessionType().ToString())
		{
			return false;
		}
		if (IsMatchInProgress(Settings) && !Settings.bAllowJoinInProgress)
		{
			return false;
		}
		return Result.Session.NumOpenPublicConnections > 0 || Result.Session.NumOpenPrivateConnections > 0;
	}

	FString GetSessionType(const FOnlineSessionSettings& Settings)
	{
		FString SessionType;
		Settings.Get(SessionTypeKey, SessionType);
		return SessionType;
	}

	bool IsSessionOfType(const FOnlineSessionSettings& Settings, const TArray<FName>& Types)
	{
		const FString SessionType = GetSessionType(Settings);
		if (SessionType.IsEmpty())
		{
			return false;
		}
		for (const FName& Type : Types)
		{
			if (!Type.IsNone() && SessionType == Type.ToString())
			{
				return true;
			}
		}
		return false;
	}

	EInviteAction DecideInviteAction(const bool bUsable, const bool bOwnType, const ECKSessionsInvitePolicy Policy, const bool bHostLossExternal,
		const bool bDedicatedServer)
	{
		if (!bUsable || !bOwnType || bDedicatedServer)
		{
			return EInviteAction::Ignore;
		}
		// While a lost host is handed over to the project, the project also decides where an invite leads.
		if (bHostLossExternal || Policy == ECKSessionsInvitePolicy::EventOnly)
		{
			return EInviteAction::EventOnly;
		}
		return EInviteAction::Join;
	}

	void SortFriends(TArray<FCKSessionsFriend>& Friends)
	{
		Friends.StableSort([](const FCKSessionsFriend& Left, const FCKSessionsFriend& Right)
		{
			if (Left.bJoinable != Right.bJoinable)
			{
				return Left.bJoinable;
			}
			if (Left.bPlayingThisGame != Right.bPlayingThisGame)
			{
				return Left.bPlayingThisGame;
			}
			if (Left.bOnline != Right.bOnline)
			{
				return Left.bOnline;
			}
			return Left.DisplayName.Compare(Right.DisplayName, ESearchCase::IgnoreCase) < 0;
		});
	}
}
