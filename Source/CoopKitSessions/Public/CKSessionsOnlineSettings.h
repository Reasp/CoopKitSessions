// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FOnlineSessionSearch;
class FOnlineSessionSearchResult;
class FOnlineSessionSettings;
class UCKSessionsProfileData;
enum class ECKSessionsInvitePolicy : uint8;
enum class ECKSessionsNetworkScope : uint8;
struct FCKSessionsFriend;

/**
 * Pure translation between a session profile and the online subsystem's settings and searches.
 * No world, no online subsystem: covered by the CoopKit.Sessions.Online automation tests.
 */
namespace CKSessions::Online
{
	/** Session setting with the profile's Session Type; searches only accept sessions of their own type. */
	extern COOPKITSESSIONS_API const FName SessionTypeKey;

	/** Session setting that is true while the match runs (between Start Match and End Match). */
	extern COOPKITSESSIONS_API const FName MatchInProgressKey;

	/** Keys of the plugin (CKS_ prefix) and of the engine's session names that custom attributes must not reuse. */
	COOPKITSESSIONS_API bool IsReservedKey(FName Key);

	/** True when Key may name a custom session attribute: not empty and not reserved. */
	COOPKITSESSIONS_API bool IsValidAttributeKey(FName Key);

	/**
	 * Checks every key of Attributes.
	 * @param OutInvalidKey  The first key that is empty or reserved; untouched when all keys are valid.
	 * @return True when every key may name a custom session attribute.
	 */
	COOPKITSESSIONS_API bool AreValidAttributeKeys(const TMap<FName, FString>& Attributes, FName& OutInvalidKey);

	/** Attributes of the profile with Overrides on top: a key in both takes the value of Overrides. */
	COOPKITSESSIONS_API TMap<FName, FString> MergeAttributes(const UCKSessionsProfileData& Profile, const TMap<FName, FString>& Overrides);

	/** Advertises every attribute as a string session setting; existing settings with the same keys are replaced. */
	COOPKITSESSIONS_API void ApplyAttributes(FOnlineSessionSettings& Settings, const TMap<FName, FString>& Attributes);

	/** Custom attributes a session advertises: every string setting whose key may name an attribute (no reserved keys). */
	COOPKITSESSIONS_API TMap<FName, FString> ReadAttributes(const FOnlineSessionSettings& Settings);

	/**
	 * True when the session advertises every attribute of Filter with exactly the same string value (case-sensitive).
	 * An empty filter matches every session.
	 */
	COOPKITSESSIONS_API bool MatchesAttributes(const FOnlineSessionSettings& Settings, const TMap<FName, FString>& Filter);

	/**
	 * Index of the session Quick Play joins: the one with the fewest open slots, so players fill running sessions
	 * before empty ones; on a tie the lower ping, then the earlier result. INDEX_NONE for an empty list.
	 */
	COOPKITSESSIONS_API int32 PickQuickPlayResult(const TArray<FOnlineSessionSearchResult>& Results);

	/**
	 * Index of the session a quick play of a group of GroupSize players joins: like Pick Quick Play Result, but only among
	 * the sessions with at least GroupSize open slots, so the whole group fits. INDEX_NONE when none has room (the group
	 * hosts) or for an empty list. A group size below 1 counts as 1.
	 */
	COOPKITSESSIONS_API int32 PickQuickPlayResultForGroup(const TArray<FOnlineSessionSearchResult>& Results, int32 GroupSize);

	/** Open slots of a found session, public and private together. */
	COOPKITSESSIONS_API int32 GetOpenSlots(const FOnlineSessionSearchResult& Result);

	/**
	 * Text the host sends a player it removes (the engine shows it as the network error): a kick or a ban with the reason.
	 * An empty reason is replaced by a plain default.
	 */
	COOPKITSESSIONS_API FString MakeRemovalMessage(bool bBan, const FString& Reason);

	/** Reads a text made by Make Removal Message; false for any other network error. Reason is left as the host wrote it. */
	COOPKITSESSIONS_API bool ParseRemovalMessage(const FString& Message, bool& bOutBan, FString& OutReason);

	/** Name of the Null online subsystem (NULL_SUBSYSTEM). */
	extern COOPKITSESSIONS_API const FName NullSubsystemName;

	/** True when a session of the scope must be a LAN session with the given online subsystem. */
	COOPKITSESSIONS_API bool ShouldUseLan(ECKSessionsNetworkScope Scope, FName SubsystemName);

	/** Who hosts the session. */
	enum class EHostKind : uint8
	{
		/** A player hosts (NM_ListenServer): presence and lobbies are allowed. */
		ListenServer,
		/** A dedicated server hosts: no presence, no lobbies. */
		DedicatedServer
	};

	/**
	 * Settings for CreateSession.
	 * @param Profile   Source profile.
	 * @param bLan      Result of ShouldUseLan.
	 * @param Host      Listen or dedicated server.
	 * @param MapName   Long package name of the map the host plays on, advertised as SETTING_MAPNAME.
	 * @param OutSettings  Overwritten completely.
	 */
	COOPKITSESSIONS_API void BuildSessionSettings(const UCKSessionsProfileData& Profile, bool bLan, EHostKind Host, const FString& MapName, FOnlineSessionSettings& OutSettings);

	/**
	 * Settings for CreateSession with attributes set at runtime on top of the profile's attributes.
	 * @param Overrides  Attributes that replace or extend the profile's attributes (see MergeAttributes).
	 */
	COOPKITSESSIONS_API void BuildSessionSettings(const UCKSessionsProfileData& Profile, bool bLan, EHostKind Host, const FString& MapName,
		const TMap<FName, FString>& Overrides, FOnlineSessionSettings& OutSettings);

	/** Search for sessions of the profile; overwrites the search's settings. */
	COOPKITSESSIONS_API void BuildSearch(const UCKSessionsProfileData& Profile, bool bLan, FOnlineSessionSearch& OutSearch);

	/**
	 * Search for sessions of the profile that advertise every attribute of Filter with the same value. Each attribute
	 * becomes an Equals query setting, which Steam and EOS apply on their side; MatchesAttributes repeats the check on
	 * the client because Null does not filter.
	 */
	COOPKITSESSIONS_API void BuildSearch(const UCKSessionsProfileData& Profile, bool bLan, const TMap<FName, FString>& Filter, FOnlineSessionSearch& OutSearch);

	/** Marks the match as running or not; while it runs, the session is closed to new players unless the profile allows join in progress. */
	COOPKITSESSIONS_API void SetMatchInProgress(FOnlineSessionSettings& Settings, bool bInProgress, bool bAllowJoinInProgress);

	/** True while the advertised match runs. */
	COOPKITSESSIONS_API bool IsMatchInProgress(const FOnlineSessionSettings& Settings);

	/**
	 * True when a search result is a session the profile can join: the same Session Type, a free slot,
	 * and not a running match that refuses newcomers. Services filter on their side too; this check makes
	 * every subsystem behave the same (Null does not filter at all).
	 */
	COOPKITSESSIONS_API bool IsJoinable(const FOnlineSessionSearchResult& Result, const UCKSessionsProfileData& Profile);

	/** Session Type a session advertises (the Session Type of its host's profile); empty when it has none, e.g. a party. */
	COOPKITSESSIONS_API FString GetSessionType(const FOnlineSessionSettings& Settings);

	/** True when the session advertises one of Types as its Session Type: a game session of this game, not a party. */
	COOPKITSESSIONS_API bool IsSessionOfType(const FOnlineSessionSettings& Settings, const TArray<FName>& Types);

	/** What the subsystem does with an accepted invite. */
	enum class EInviteAction : uint8
	{
		/** Not ours or not usable: another plugin's session (a party), or the platform reported a failed accept. */
		Ignore,
		/** Fire On Invite Accepted only. */
		EventOnly,
		/** Fire On Invite Accepted, leave the current session and join the invited one. */
		Join
	};

	/**
	 * Decision for an accepted invite (decision-94 p. 4).
	 * @param bUsable            The platform reported success and the result points at a session.
	 * @param bOwnType           The session is of a Session Type of this game (IsSessionOfType).
	 * @param Policy             Invite Accept Policy of the subsystem.
	 * @param bHostLossExternal  Handle Host Loss Externally is on: the project's own code decides where to go.
	 * @param bDedicatedServer   This machine is a dedicated server, which has no player to join with.
	 */
	COOPKITSESSIONS_API EInviteAction DecideInviteAction(bool bUsable, bool bOwnType, ECKSessionsInvitePolicy Policy, bool bHostLossExternal, bool bDedicatedServer);

	/** Orders a friends list for a menu: joinable first, then playing this game, then online, then by name. */
	COOPKITSESSIONS_API void SortFriends(TArray<FCKSessionsFriend>& Friends);
}
