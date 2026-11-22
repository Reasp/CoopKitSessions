// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "CKSessionsDiagnostics.h"
#include "CKSessionsTypes.h"
#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CKSessionsSubsystem.generated.h"

class AController;
class IOnlineSubsystem;
class AGameModeBase;
class APlayerController;
class APlayerState;
class UCKSessionsProfileData;
class UNetDriver;
class UWorld;
struct FUniqueNetIdRepl;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCKSessionsOnStateChanged, ECKSessionsState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCKSessionsOnRequestComplete, bool, bSuccess, ECKSessionsFailure, Failure);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCKSessionsOnFindComplete, bool, bSuccess, const TArray<FCKSessionsSearchResult>&, Results);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FCKSessionsOnBackgroundFindComplete, bool, bSuccess, ECKSessionsFailure, Failure, const TArray<FCKSessionsSearchResult>&, Results);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCKSessionsOnSessionFailure, ECKSessionsFailure, Failure, const FString&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCKSessionsOnPlayerEvent, APlayerState*, PlayerState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FCKSessionsOnQuickPlayComplete, bool, bSuccess, bool, bHosted, ECKSessionsFailure, Failure);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCKSessionsOnHostLost, const FCKSessionsHostLoss&, HostLoss);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCKSessionsOnInviteAccepted, const FCKSessionsInvite&, Invite);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FCKSessionsOnReadFriendsComplete, bool, bSuccess, ECKSessionsFailure, Failure, const TArray<FCKSessionsFriend>&, Friends);

/**
 * The online session of one game instance: host, find, join, leave, start and end the match, reconnect after a lost
 * connection, and the host's record of every player. The only place of CoopKit Sessions that talks to the online
 * subsystem (IOnlineSession of OnlineSubsystem v1; Null, Steam or EOS by the project's DefaultPlatformService).
 *
 * Lives in the game instance because a session outlives maps: it is created in a menu, survives seamless travel,
 * and every Play In Editor instance has its own. Works without changing the project's game mode or game state:
 * players are tracked through the engine's game mode events.
 */
UCLASS()
class COOPKITSESSIONS_API UCKSessionsSubsystem final : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Subsystem of the game instance the object belongs to; null without a game instance. */
	static UCKSessionsSubsystem* Get(const UObject* WorldContextObject);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * Hosts a session. A standalone game first opens Map as a listen server, then creates the session; a server
	 * (listen or dedicated) creates the session on its current map. Completes with On Host Complete.
	 * @param Profile  Session profile; null uses the project's default profile.
	 * @param Map      Map to host on when the game is not a server yet; empty keeps the current map.
	 * @return False when the request was refused at once (busy, no online subsystem); On Host Complete still fires.
	 */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions")
	bool HostSession(const UCKSessionsProfileData* Profile, TSoftObjectPtr<UWorld> Map);

	/**
	 * Hosts a session like Host Session, advertising Attributes on top of the profile's attributes (a key in both takes
	 * the value given here), e.g. MatchId from a matchmaker or the Mode of a quick play. Replaces the attributes set
	 * earlier with Set Session Attribute.
	 * @param Attributes  Key and string value of each attribute; empty and reserved keys fail with Invalid Attribute.
	 * @return False when the request was refused at once; On Host Complete still fires.
	 */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions")
	bool HostSessionWithAttributes(const UCKSessionsProfileData* Profile, TSoftObjectPtr<UWorld> Map, const TMap<FName, FString>& Attributes);

	/**
	 * Sets one attribute of the session this machine hosts, on top of the profile's attributes. While hosting it is
	 * advertised at once (the online subsystem's UpdateSession); otherwise it is kept for the next Host Session.
	 * Leave Session clears the attributes set at runtime.
	 * @return False for an empty or reserved key, or on a client of someone else's session.
	 */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions")
	bool SetSessionAttribute(FName Key, const FString& Value);

	/** Attributes the hosted (or next hosted) session advertises: the active profile's attributes with the runtime ones on top. */
	UFUNCTION(BlueprintPure, Category = "CoopKit|Sessions")
	TMap<FName, FString> GetSessionAttributes() const;

	/**
	 * Searches for joinable sessions of the profile: same Session Type, a free slot, not a running match that refuses
	 * newcomers. Only while idle. Completes with On Find Complete; the results stay in Get Search Results.
	 * @param Profile  Session profile; null uses the project's default profile.
	 */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions")
	bool FindSessions(const UCKSessionsProfileData* Profile);

	/**
	 * Searches like Find Sessions, keeping only sessions that advertise every attribute of Filter with exactly the same
	 * value (case-sensitive). Steam and EOS filter on their side; the plugin repeats the check, so Null behaves the same.
	 * @param Filter  Key and string value each session must advertise; empty and reserved keys fail the search.
	 */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions")
	bool FindSessionsWithAttributes(const UCKSessionsProfileData* Profile, const TMap<FName, FString>& Filter);

	/**
	 * Searches for sessions in the background, to watch them rather than to join one: in any state, also while hosting or
	 * playing in a session, and without changing the state, Get Search Results or Get Last Failure. Keeps every session of
	 * the profile's Session Type that advertises every attribute of Filter, full sessions and running matches included.
	 * The online subsystem runs one search at a time: a background search waits for a Find Sessions in flight, and a Find
	 * Sessions waits for the background search. On a LAN (Null) a host is not visible to others while it searches.
	 * Completes with On Background Find Complete.
	 * @param Profile  Session profile; null uses the project's default profile.
	 * @param Filter   Key and string value each session must advertise; may be empty; empty and reserved keys fail.
	 * @return False when refused at once: a background search in flight or an invalid key (On Background Find Complete does
	 *         not fire, so the search in flight keeps its listeners), or no online subsystem (the event fires with the failure).
	 */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions")
	bool FindSessionsInBackground(const UCKSessionsProfileData* Profile, const TMap<FName, FString>& Filter);

	/** A background search is running or waiting for its turn. */
	UFUNCTION(BlueprintPure, Category = "CoopKit|Sessions")
	bool IsBackgroundSearchInFlight() const
	{
		return BackgroundFindCompleteHandle.IsValid() || bBackgroundSearchWaiting;
	}

	/**
	 * Quick play: searches for a session of the profile with the given attributes and joins the fullest one; when there
	 * is none, hosts a session with these attributes. If the chosen session fills up or closes before the join, hosts
	 * instead. Only while idle. Completes with On Quick Play Complete; On Find, On Join and On Host Complete fire on the way.
	 * @param Profile     Session profile; null uses the project's default profile.
	 * @param Attributes  Attributes to search for, and to advertise when hosting; may be empty.
	 * @param Map         Map to host on when no session is found; empty keeps the current map.
	 * @return False when the request was refused at once; On Quick Play Complete still fires.
	 */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions")
	bool QuickPlay(const UCKSessionsProfileData* Profile, const TMap<FName, FString>& Attributes, TSoftObjectPtr<UWorld> Map);

	/**
	 * Searches like Find Sessions With Attributes, keeping only sessions with at least GroupSize open slots, so a party of
	 * that size fits in whole. With none, the result list is empty. Completes with On Find Complete.
	 * @param Filter     Key and string value each session must advertise; may be empty; empty and reserved keys fail.
	 * @param GroupSize  Players who join together; below 1 counts as 1.
	 */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions")
	bool FindSessionsForGroup(const UCKSessionsProfileData* Profile, const TMap<FName, FString>& Filter, int32 GroupSize);

	/**
	 * Quick play for a party: like Quick Play, but only a session with at least GroupSize open slots is joined (the fullest
	 * of them); when none has room, hosts a session. Only while idle. Completes with On Quick Play Complete.
	 * @param GroupSize  Players who play together, this one included; below 1 counts as 1.
	 * @return False when the request was refused at once; On Quick Play Complete still fires.
	 */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions")
	bool QuickPlayForGroup(const UCKSessionsProfileData* Profile, const TMap<FName, FString>& Attributes, TSoftObjectPtr<UWorld> Map, int32 GroupSize);

	/**
	 * Host only: removes a remote player from the session. The player gets On Session Failure (Kicked) with the reason, does
	 * not reconnect and may join again by searching; use Ban Player to keep them out. Their record is dropped at once.
	 * @param Reason  Shown to the player; empty shows a plain default.
	 * @return False on a client, for the host's own player, or without a connection.
	 */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions|Moderation")
	bool KickPlayer(APlayerState* PlayerState, const FString& Reason);

	/**
	 * Host only: kicks the player and bans them for the rest of the session. A ban refuses a search and join, a reconnect and
	 * a direct address alike (the check runs when the connection is made), and the player is shown the reason (Failure Banned).
	 * The ban ends with the session (Leave Session, new Host Session) or with Unban Player.
	 * @return False when the kick is refused (see Kick Player); nothing is banned then.
	 */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions|Moderation")
	bool BanPlayer(APlayerState* PlayerState, const FString& Reason);

	/** Host only: lets a banned player in again. PlayerId is the id of the ban list (Get Banned Players). False when not banned. */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions|Moderation")
	bool UnbanPlayer(const FString& PlayerId);

	/** Host only: true while the player id is banned from the session. */
	UFUNCTION(BlueprintPure, Category = "CoopKit|Sessions|Moderation")
	bool IsPlayerBanned(const FString& PlayerId) const;

	/** Host only: players banned from this session. */
	UFUNCTION(BlueprintPure, Category = "CoopKit|Sessions|Moderation")
	TArray<FCKSessionsBanRecord> GetBannedPlayers() const;

	/** On a removed player: the host's reason of the last kick or ban; empty before. */
	UFUNCTION(BlueprintPure, Category = "CoopKit|Sessions|Moderation")
	const FString& GetLastRemovalReason() const
	{
		return LastRemovalReason;
	}

	/**
	 * Joins a session from a search and travels to its host. Only while idle. Completes with On Join Complete once
	 * the player is connected to the host.
	 */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions")
	bool JoinSession(const FCKSessionsSearchResult& Result);

	/** Destroys the session and returns to the menu map (Project Settings > CoopKit Sessions). A dedicated server stays on its map. */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions")
	void LeaveSession();

	/**
	 * Leaves like Leave Session, then opens Map instead of the menu map, e.g. the lobby of a game mode. A player who only
	 * searched opens Map too. A dedicated server stays on its map.
	 * @param Map  Map to open after leaving; empty opens the menu map, as Leave Session does.
	 */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions")
	void LeaveSessionToMap(TSoftObjectPtr<UWorld> Map);

	/**
	 * Server only: marks the match as running. Searches show it as in progress, and it stays open to newcomers only
	 * when the profile allows join in progress. The lobby component calls it when it travels to the match.
	 */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions")
	bool StartMatch();

	/** Server only: the match is over; the session is open to newcomers again. */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions")
	bool EndMatch();

	UFUNCTION(BlueprintPure, Category = "CoopKit|Sessions")
	ECKSessionsState GetState() const
	{
		return State;
	}

	/** True on the machine that created the session (listen or dedicated server). */
	UFUNCTION(BlueprintPure, Category = "CoopKit|Sessions")
	bool IsHost() const
	{
		return bIsHost;
	}

	/** True on the host between Start Match and End Match. */
	UFUNCTION(BlueprintPure, Category = "CoopKit|Sessions")
	bool IsMatchInProgress() const
	{
		return bMatchInProgress;
	}

	/** Results of the last finished search. */
	UFUNCTION(BlueprintPure, Category = "CoopKit|Sessions")
	const TArray<FCKSessionsSearchResult>& GetSearchResults() const
	{
		return SearchResults;
	}

	/** Profile of the current or last session request. */
	UFUNCTION(BlueprintPure, Category = "CoopKit|Sessions")
	const UCKSessionsProfileData* GetActiveProfile() const;

	/** Reason of the last failure; None after a success. */
	UFUNCTION(BlueprintPure, Category = "CoopKit|Sessions")
	ECKSessionsFailure GetLastFailure() const
	{
		return LastFailure;
	}

	/** Host only: every player of the session, connected or away within the reconnect grace time. */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions")
	TArray<FCKSessionsPlayerRecord> GetPlayerRecords() const;

	/**
	 * Hands a lost host over to the project's own code, e.g. host migration. While on, a client that loses the
	 * connection to its host (the host left, crashed or the link broke) does not reconnect, does not open the menu map
	 * and gets no On Session Failure. Instead, once the engine is back on its default map, the subsystem is Idle and
	 * On Host Lost fires: Host Session (with a map) and Find Sessions With Attributes work from that event on.
	 * Off by default; stays as set until changed, also across sessions. Does nothing on the host.
	 */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions")
	void SetHandleHostLossExternally(bool bEnabled);

	/**
	 * Checks the online setup of this game: the service named by DefaultPlatformService against the online subsystem the engine
	 * loaded, the GameNetDriver class, the enabled plugins, steam_appid.txt, the session flags, the EOS product settings and the
	 * maps the plugin travels to. Writes every finding to the log (LogCKSessions) and keeps the list for Get Setup Findings.
	 * The plugin runs it once a moment after the game starts; call it again after changing the setup at runtime.
	 * @return The findings, most serious first; empty when the setup is fine.
	 */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions")
	TArray<FCKSessionsFinding> CheckSetup();

	/** Findings of the last Check Setup, for a settings screen or a log line of your own. */
	UFUNCTION(BlueprintPure, Category = "CoopKit|Sessions")
	const TArray<FCKSessionsFinding>& GetSetupFindings() const
	{
		return SetupFindings;
	}

	/** True while a lost host is handed over to On Host Lost instead of reconnection and the menu map. */
	UFUNCTION(BlueprintPure, Category = "CoopKit|Sessions")
	bool IsHostLossHandledExternally() const
	{
		return bHandleHostLossExternally;
	}

	/**
	 * Changes what an accepted invite does (Project Settings > CoopKit Sessions > Invite Accept Policy) until the game
	 * instance ends. While Handle Host Loss Externally is on, an invite only fires On Invite Accepted whatever the policy.
	 */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions|Invites")
	void SetInviteAcceptPolicy(ECKSessionsInvitePolicy Policy);

	UFUNCTION(BlueprintPure, Category = "CoopKit|Sessions|Invites")
	ECKSessionsInvitePolicy GetInviteAcceptPolicy() const
	{
		return InvitePolicy;
	}

	/**
	 * Leaves the current session or request, if any, and joins Session, e.g. the session of On Invite Accepted with policy
	 * Event Only. A host, search or join in flight finishes first; a reconnection stops. Completes with On Join Complete.
	 * @return False when refused at once (empty result, dedicated server); On Join Complete still fires.
	 */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions|Invites")
	bool JoinInvitedSession(const FCKSessionsSearchResult& Session);

	/**
	 * Opens the platform's overlay to invite friends to the session this machine is in (Steam). Needs a session.
	 * @return None when the overlay opened; Not Supported on platforms without an invite overlay (Null, EOS: use Read Friends
	 *         and Invite Friend instead); Busy without a session.
	 */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions|Invites")
	ECKSessionsFailure ShowInviteUI();

	/**
	 * Reads the local player's friends list with presence from the platform (Steam friends, Epic account friends on EOS).
	 * Completes with On Read Friends Complete; the list stays in Get Friends. Not Supported with Null.
	 * @return False when refused at once; On Read Friends Complete still fires.
	 */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions|Invites")
	bool ReadFriends();

	/** Friends from the last successful Read Friends: joinable first, then playing this game, then online, then by name. */
	UFUNCTION(BlueprintPure, Category = "CoopKit|Sessions|Invites")
	const TArray<FCKSessionsFriend>& GetFriends() const
	{
		return Friends;
	}

	/**
	 * Invites a friend from Read Friends to the session this machine is in. The friend gets the platform's invite.
	 * @return None when sent; Not Supported with Null; Busy without a session; Invite Failed when the platform refused.
	 */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions|Invites")
	ECKSessionsFailure InviteFriend(const FCKSessionsFriend& Friend);

	/**
	 * Finds the game session a friend from Read Friends plays in (Steam, EOS) and joins it. Only while idle. Completes with
	 * On Join Complete: Session Gone when the friend is not in a game session of this game, Not Supported with Null.
	 * @return False when refused at once; On Join Complete still fires.
	 */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions|Invites")
	bool JoinFriend(const FCKSessionsFriend& Friend);

	/** Fires on this machine whenever the state changes. */
	UPROPERTY(BlueprintAssignable, Category = "CoopKit|Sessions")
	FCKSessionsOnStateChanged OnStateChanged;

	/** Fires on this machine when a Host Session request ends. */
	UPROPERTY(BlueprintAssignable, Category = "CoopKit|Sessions")
	FCKSessionsOnRequestComplete OnHostComplete;

	/** Fires on this machine when a Find Sessions request ends. */
	UPROPERTY(BlueprintAssignable, Category = "CoopKit|Sessions")
	FCKSessionsOnFindComplete OnFindComplete;

	/** Fires on this machine when a Find Sessions In Background request ends; Failure is None on success. */
	UPROPERTY(BlueprintAssignable, Category = "CoopKit|Sessions")
	FCKSessionsOnBackgroundFindComplete OnBackgroundFindComplete;

	/** Fires on this machine when a Join Session request ends: connected to the host, or failed. */
	UPROPERTY(BlueprintAssignable, Category = "CoopKit|Sessions")
	FCKSessionsOnRequestComplete OnJoinComplete;

	/** Fires on this machine when a Quick Play request ends: joined a session (Hosted false), hosted one (Hosted true), or failed. */
	UPROPERTY(BlueprintAssignable, Category = "CoopKit|Sessions")
	FCKSessionsOnQuickPlayComplete OnQuickPlayComplete;

	/**
	 * Fires on a client when the session is lost for good: connection lost without reconnection, or every reconnect
	 * attempt failed. Not while Handle Host Loss Externally is on: a lost host fires On Host Lost instead.
	 */
	UPROPERTY(BlueprintAssignable, Category = "CoopKit|Sessions")
	FCKSessionsOnSessionFailure OnSessionFailure;

	/**
	 * Fires on a client with Handle Host Loss Externally on, when the host of its session is lost: why, and the lost
	 * session with its attributes. The subsystem is Idle and the engine is on its default map: host or find the game
	 * that takes the session's place here, or open a menu with Leave Session To Map.
	 */
	UPROPERTY(BlueprintAssignable, Category = "CoopKit|Sessions")
	FCKSessionsOnHostLost OnHostLost;

	/**
	 * Fires on this machine when the local player accepts an invite to a game session of this game: in the Steam overlay or
	 * friends list (Join Game), by starting the game from an invite, or on EOS. Invites to sessions of another Session Type,
	 * e.g. a party, are ignored. Invite.bJoining says whether the subsystem joins by itself (Invite Accept Policy).
	 */
	UPROPERTY(BlueprintAssignable, Category = "CoopKit|Sessions|Invites")
	FCKSessionsOnInviteAccepted OnInviteAccepted;

	/** Fires on this machine when a Read Friends request ends. */
	UPROPERTY(BlueprintAssignable, Category = "CoopKit|Sessions|Invites")
	FCKSessionsOnReadFriendsComplete OnReadFriendsComplete;

	/** Fires on the host when a new player logs in (not on seamless travel, not on a reconnect). */
	UPROPERTY(BlueprintAssignable, Category = "CoopKit|Sessions")
	FCKSessionsOnPlayerEvent OnPlayerJoined;

	/** Fires on the host when a player leaves or loses the connection. The record stays for the reconnect grace time. */
	UPROPERTY(BlueprintAssignable, Category = "CoopKit|Sessions")
	FCKSessionsOnPlayerEvent OnPlayerLeft;

	/** Fires on the host when a player who lost the connection comes back within the grace time. Restore their data here. */
	UPROPERTY(BlueprintAssignable, Category = "CoopKit|Sessions")
	FCKSessionsOnPlayerEvent OnPlayerReconnected;

private:
	enum class EReconnectStep : uint8
	{
		Waiting,
		Searching,
		Joining
	};

	IOnlineSessionPtr GetSessions() const;
	IOnlineSubsystem* GetOnlineSubsystem() const;
	FName GetOnlineSubsystemName() const;
	const UCKSessionsProfileData* ResolveProfile(const UCKSessionsProfileData* Profile) const;
	void SetState(ECKSessionsState NewState);
	bool IsOwnWorld(const UWorld* World) const;
	bool Tick(float DeltaSeconds);

	void CreateHostedSession();
	void FinishHost(bool bSuccess, ECKSessionsFailure Failure);
	void OnCreateSessionComplete(FName SessionName, bool bSuccess);

	bool StartFind(const UCKSessionsProfileData* Profile, const TMap<FName, FString>& Filter, int32 GroupSize);
	bool StartQuickPlay(const UCKSessionsProfileData* Profile, const TMap<FName, FString>& Attributes, TSoftObjectPtr<UWorld> Map, int32 GroupSize);
	bool RemovePlayer(APlayerState* PlayerState, const FString& Reason, bool bBan);
	/** Client: the host kicked or banned this player. */
	void HandleRemoval(bool bBanned, const FString& Reason);
	bool StartSearch();
	void FinishFind(bool bSuccess);
	void OnFindSessionsComplete(bool bSuccess);

	bool StartBackgroundSearch();
	void OnBackgroundFindSessionsComplete(bool bSuccess);
	void FinishBackgroundFind(bool bSuccess, ECKSessionsFailure Failure, const TArray<FCKSessionsSearchResult>& Results);
	/** The main search that waited for the background search: starts it, or ends it when its request is gone. */
	void ResumeWaitingSearch();
	void UpdateAdvertisedAttributes();
	void ContinueQuickPlay(bool bSearchSucceeded);
	void FinishQuickPlay(bool bSuccess, bool bHosted, ECKSessionsFailure Failure);

	void StartJoin();
	void FinishJoin(bool bSuccess, ECKSessionsFailure Failure);
	void OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
	bool TravelToHost(const FString& ConnectString);
	bool IsConnectedToHost() const;

	void DestroySessionThen(TFunction<void()> Next);
	void OnDestroySessionComplete(FName SessionName, bool bSuccess);
	/** Opens Map, or the menu map (project settings, else the game's default map) when Map is empty. */
	void ReturnToMenu(const TSoftObjectPtr<UWorld>& Map = TSoftObjectPtr<UWorld>());
	void UpdateAdvertisedMatchState();

	void BeginReconnect();
	void TickReconnect(double Now);
	void FailReconnectAttempt();
	void LoseSession(ECKSessionsFailure Failure, const FString& Message);

	void BeginHostLoss(ENetworkFailure::Type FailureType, const FString& Message);
	void TickHostLoss(double Now);

	void EnsureInviteSubscription();
	void ClearInviteSubscription();
	void OnSessionUserInviteAccepted(bool bWasSuccessful, int32 ControllerId, FUniqueNetIdPtr UserId, const FOnlineSessionSearchResult& InviteResult);
	bool IsOwnSessionType(const FOnlineSessionSearchResult& Result) const;
	void QueueInviteJoin(const FCKSessionsSearchResult& Session);
	/** Starts the queued invite join once the subsystem and the engine are ready for it; called on accept and every tick. */
	void TryStartInviteJoin(double Now);
	void FinishReadFriends(bool bSuccess, ECKSessionsFailure Failure);
	void OnReadFriendsListComplete(int32 LocalUserNum, bool bWasSuccessful, const FString& ListName, const FString& ErrorStr);
	void OnFindFriendSessionComplete(int32 LocalUserNum, bool bWasSuccessful, const TArray<FOnlineSessionSearchResult>& Results);

	void OnNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);
	void OnTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);
	void OnGameModePreLogin(AGameModeBase* GameMode, const FUniqueNetIdRepl& NewPlayer, FString& ErrorMessage);
	void OnGameModePostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer);
	void OnGameModeLogout(AGameModeBase* GameMode, AController* Exiting);
	void PurgeExpiredRecords(double Now);

	ECKSessionsState State = ECKSessionsState::Idle;
	ECKSessionsFailure LastFailure = ECKSessionsFailure::None;
	bool bIsHost = false;
	bool bMatchInProgress = false;
	bool bWaitingForListenServer = false;
	bool bWaitingForConnection = false;
	bool bDedicatedServerHostChecked = false;
	double WaitStartedSeconds = 0.0;

	UPROPERTY()
	TObjectPtr<const UCKSessionsProfileData> ActiveProfile;

	/** Attributes set at runtime (Host Session With Attributes, Set Session Attribute), on top of the profile's. */
	TMap<FName, FString> RuntimeAttributes;
	/** Set Session Attribute ran while the session was being created: advertise once it exists. */
	bool bAttributesChangedWhileHosting = false;

	/** Attribute filter of the current search; empty for Find Sessions and reconnection. */
	TMap<FName, FString> SearchFilter;
	/** Open slots the sessions of the current search need (Find Sessions For Group); 1 otherwise. */
	int32 SearchGroupSize = 1;

	/** Quick play in flight: the search, then the join or the host. */
	bool bQuickPlay = false;
	/** The quick play already fell back from a failed join to hosting. */
	bool bQuickPlayFellBack = false;
	TMap<FName, FString> QuickPlayAttributes;
	TSoftObjectPtr<UWorld> QuickPlayMap;
	int32 QuickPlayGroupSize = 1;

	TArray<FCKSessionsSearchResult> SearchResults;
	TSharedPtr<FOnlineSessionSearch> ActiveSearch;
	FCKSessionsSearchResult PendingJoin;

	// One search at a time on the online subsystem: its completion event does not say whose search ended.
	/** Find Sessions (or a reconnect search) waits for the background search in flight. */
	bool bMainSearchWaiting = false;
	/** Find Sessions In Background waits for the main search in flight. */
	bool bBackgroundSearchWaiting = false;
	UPROPERTY()
	TObjectPtr<const UCKSessionsProfileData> BackgroundProfile;
	TMap<FName, FString> BackgroundFilter;
	TSharedPtr<FOnlineSessionSearch> BackgroundSearch;

	/** Session id and host address of the last joined session, for reconnection. */
	FString LastSessionId;
	FString LastConnectString;
	int32 ReconnectAttemptsLeft = 0;
	EReconnectStep ReconnectStep = EReconnectStep::Waiting;
	double NextReconnectSeconds = 0.0;

	/** A lost host goes to On Host Lost instead of reconnection and the menu map. */
	bool bHandleHostLossExternally = false;
	/** The host is lost; On Host Lost fires once the engine is back on its default map. */
	bool bHostLossPending = false;
	double HostLossStartedSeconds = 0.0;
	FCKSessionsHostLoss PendingHostLoss;

	TMap<FString, FCKSessionsPlayerRecord> PlayerRecords;
	/** Host: banned players by player id, for the rest of the session. */
	TMap<FString, FCKSessionsBanRecord> BannedPlayers;
	/** Client: the reason of the last kick or ban. */
	FString LastRemovalReason;

	/** What an accepted invite does; starts as the project setting. */
	ECKSessionsInvitePolicy InvitePolicy = ECKSessionsInvitePolicy::EventOnly;
	/** An invited session waits until the current host, search, join or leave is over (Join Invited Session). */
	bool bInviteJoinPending = false;
	FCKSessionsSearchResult PendingInvite;
	double InviteQueuedSeconds = 0.0;
	/** Session interface the invite delegate is bound to (the one of this game instance's world). */
	TWeakPtr<IOnlineSession, ESPMode::ThreadSafe> InviteSessions;
	FDelegateHandle InviteAcceptedHandle;

	/** Friends of the last successful Read Friends. */
	TArray<FCKSessionsFriend> Friends;
	bool bReadingFriends = false;
	FDelegateHandle FindFriendCompleteHandle;

	/** Result of the last Check Setup. */
	TArray<FCKSessionsFinding> SetupFindings;

	TFunction<void()> AfterDestroy;
	mutable TWeakPtr<IOnlineSession, ESPMode::ThreadSafe> CachedSessions;

	FTSTicker::FDelegateHandle TickHandle;
	FTSTicker::FDelegateHandle SetupCheckHandle;
	FDelegateHandle CreateCompleteHandle;
	FDelegateHandle FindCompleteHandle;
	FDelegateHandle BackgroundFindCompleteHandle;
	FDelegateHandle JoinCompleteHandle;
	FDelegateHandle DestroyCompleteHandle;
	FDelegateHandle NetworkFailureHandle;
	FDelegateHandle TravelFailureHandle;
	FDelegateHandle PreLoginHandle;
	FDelegateHandle PostLoginHandle;
	FDelegateHandle LogoutHandle;
};
