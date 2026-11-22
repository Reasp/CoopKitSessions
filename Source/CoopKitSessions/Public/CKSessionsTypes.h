// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/OnlineReplStructs.h"
#include "OnlineSessionSettings.h"
#include "CKSessionsTypes.generated.h"

/** What the sessions subsystem of one game instance is doing right now. */
UENUM(BlueprintType)
enum class ECKSessionsState : uint8
{
	/** No session and no request in flight. */
	Idle,
	/** Opening the map as a listen server or creating the session. */
	Hosting,
	/** Looking for sessions. */
	Searching,
	/** Joining a session and travelling to its host. */
	Joining,
	/** Hosting or playing in a session. */
	InSession,
	/** The connection to the host was lost; trying to join the same session again. */
	Reconnecting,
	/**
	 * Destroying the session and returning to the menu map. With Handle Host Loss Externally on, also the short wait
	 * after a lost host until the engine is back on its default map and On Host Lost fires.
	 */
	Leaving
};

/** Why a client lost the connection to the host of its session (On Host Lost). */
UENUM(BlueprintType)
enum class ECKSessionsHostLossReason : uint8
{
	/** The host closed the connection: it left the session, quit the game or stopped the server. */
	HostLeft,
	/**
	 * Nothing came from the host for the net driver's connection timeout: the host crashed or its network went down.
	 * Play In Editor turns connection timeouts off, so there this only happens when a test turns them back on.
	 */
	TimedOut,
	/** The connection broke for another reason, e.g. this machine lost its network or the host kicked the player. */
	ConnectionLost
};

/** Why a session request failed. */
UENUM(BlueprintType)
enum class ECKSessionsFailure : uint8
{
	None,
	/** No online subsystem or no session interface: check [OnlineSubsystem] DefaultPlatformService. */
	NoOnlineSubsystem,
	/** The request does not fit the current state, e.g. Join while already in a session. */
	Busy,
	/** The online service refused to create the session. */
	CreateFailed,
	/** The map to host on could not be opened as a listen server. */
	HostTravelFailed,
	/** The search failed (not "found nothing": an empty result list is a success). */
	SearchFailed,
	/** The search result or the friend is empty or belongs to another online subsystem. */
	InvalidResult,
	/** The session has no free slot. */
	SessionFull,
	/** The session no longer exists. */
	SessionGone,
	/** The online service refused the join or did not return the host address. */
	JoinFailed,
	/** The host refused the connection or the travel to the host failed. */
	ConnectFailed,
	/** The connection to the host was lost and reconnection is off or used up. */
	ConnectionLost,
	/** Every reconnect attempt failed. */
	ReconnectFailed,
	/** A session attribute has an empty key or a key the plugin or the engine reserves (see Is Reserved Attribute Key). */
	InvalidAttribute,
	/**
	 * The online subsystem cannot do this, e.g. friends and invites with Null, or the invite overlay with EOS. Nothing was
	 * sent; the log says what the platform lacks.
	 */
	NotSupported,
	/** The platform did not return the friends list, e.g. the player is not logged in to Steam or to an Epic account. */
	FriendsFailed,
	/** The platform refused to send the invite. */
	InviteFailed,
	/** The host removed this player from the session (Kick Player). Get Last Removal Reason has the host's reason. */
	Kicked,
	/** The host banned this player from the session (Ban Player): no way back until the session ends or the host unbans. */
	Banned
};

/** What the subsystem does when the player accepts an invite to a game session of this game (On Invite Accepted). */
UENUM(BlueprintType)
enum class ECKSessionsInvitePolicy : uint8
{
	/**
	 * Leave the current session or request, if any, without asking the player (also in a running match), and join the
	 * invited session. On Invite Accepted fires first.
	 */
	JoinAtOnce,
	/** Default. Only fire On Invite Accepted: your code decides, e.g. asks the player, then calls Join Invited Session. */
	EventOnly
};

/**
 * One session found by a search. Wraps the online subsystem result so Blueprint can pass it around;
 * read it with the CoopKit Sessions library functions (host name, players, ping, attributes).
 */
USTRUCT(BlueprintType)
struct COOPKITSESSIONS_API FCKSessionsSearchResult
{
	GENERATED_BODY()

	FCKSessionsSearchResult() = default;

	explicit FCKSessionsSearchResult(const FOnlineSessionSearchResult& InResult)
		: Result(InResult)
	{
	}

	/** True when the result points at a real session. */
	bool IsValid() const
	{
		return Result.IsValid();
	}

	/** Unique id of the session, stable for the session's lifetime; empty when invalid. */
	FString GetSessionId() const
	{
		return Result.IsValid() ? Result.GetSessionIdStr() : FString();
	}

	const FOnlineSessionSearchResult& GetNative() const
	{
		return Result;
	}

private:
	FOnlineSessionSearchResult Result;
};

/** An invite the local player accepted (On Invite Accepted): the invited session and whether the subsystem joins it. */
USTRUCT(BlueprintType)
struct COOPKITSESSIONS_API FCKSessionsInvite
{
	GENERATED_BODY()

	/** The invited session; pass it to Join Invited Session when the subsystem does not join by itself. */
	UPROPERTY(BlueprintReadOnly, Category = "CoopKit|Sessions")
	FCKSessionsSearchResult Session;

	/** Unique id of the invited session. */
	UPROPERTY(BlueprintReadOnly, Category = "CoopKit|Sessions")
	FString SessionId;

	/** Custom attributes the invited session advertises. */
	UPROPERTY(BlueprintReadOnly, Category = "CoopKit|Sessions")
	TMap<FName, FString> Attributes;

	/**
	 * True when the subsystem joins the session by itself (policy Join At Once): it leaves the current session first and
	 * fires On Join Complete when connected. False with policy Event Only (the default) and while Handle Host Loss
	 * Externally is on.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "CoopKit|Sessions")
	bool bJoining = false;
};

/** One friend of the local player from Read Friends, with the presence the platform reports. */
USTRUCT(BlueprintType)
struct COOPKITSESSIONS_API FCKSessionsFriend
{
	GENERATED_BODY()

	/** Platform id of the friend; pass the whole struct to Invite Friend or Join Friend. */
	UPROPERTY(BlueprintReadOnly, Category = "CoopKit|Sessions")
	FUniqueNetIdRepl UniqueId;

	/** Name the platform shows for the friend. */
	UPROPERTY(BlueprintReadOnly, Category = "CoopKit|Sessions")
	FString DisplayName;

	/** The friend is online on the platform. */
	UPROPERTY(BlueprintReadOnly, Category = "CoopKit|Sessions")
	bool bOnline = false;

	/** The friend plays this game right now. */
	UPROPERTY(BlueprintReadOnly, Category = "CoopKit|Sessions")
	bool bPlayingThisGame = false;

	/** The platform says the friend's game can be joined: Join Friend is worth a try. */
	UPROPERTY(BlueprintReadOnly, Category = "CoopKit|Sessions")
	bool bJoinable = false;
};

/** Server-side record of one player of the session. Survives seamless travel and a reconnect of the player. */
USTRUCT(BlueprintType)
struct COOPKITSESSIONS_API FCKSessionsPlayerRecord
{
	GENERATED_BODY()

	/** Online id of the player as a string (the player name when the online subsystem gives no id). */
	UPROPERTY(BlueprintReadOnly, Category = "CoopKit|Sessions")
	FString PlayerId;

	/** Player name at the last login. */
	UPROPERTY(BlueprintReadOnly, Category = "CoopKit|Sessions")
	FString PlayerName;

	/** False while the player is away; the record is kept for the profile's Reconnect Grace Seconds. */
	UPROPERTY(BlueprintReadOnly, Category = "CoopKit|Sessions")
	bool bConnected = false;

	/** How many times the player came back after losing the connection. */
	UPROPERTY(BlueprintReadOnly, Category = "CoopKit|Sessions")
	int32 ReconnectCount = 0;

	/** Platform time (seconds) of the disconnect; 0 while connected. */
	double DisconnectedAtSeconds = 0.0;
};

/** One player the host banned from the session (Ban Player). Lives until the session ends. */
USTRUCT(BlueprintType)
struct COOPKITSESSIONS_API FCKSessionsBanRecord
{
	GENERATED_BODY()

	/** Online id of the player as a string (the player name when the online subsystem gives no id), as in the player record. */
	UPROPERTY(BlueprintReadOnly, Category = "CoopKit|Sessions")
	FString PlayerId;

	/** Player name when the ban was given; empty when the ban was given by id alone. */
	UPROPERTY(BlueprintReadOnly, Category = "CoopKit|Sessions")
	FString PlayerName;

	/** Reason the banned player is shown. */
	UPROPERTY(BlueprintReadOnly, Category = "CoopKit|Sessions")
	FString Reason;
};

/**
 * A client lost the host of its session while Handle Host Loss Externally was on (On Host Lost): why, and which session.
 * The session is gone on this machine; use its attributes to find or host the game that takes its place.
 */
USTRUCT(BlueprintType)
struct COOPKITSESSIONS_API FCKSessionsHostLoss
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CoopKit|Sessions")
	ECKSessionsHostLossReason Reason = ECKSessionsHostLossReason::ConnectionLost;

	/** The engine's description of the network failure, for logs; not meant for players. */
	UPROPERTY(BlueprintReadOnly, Category = "CoopKit|Sessions")
	FString Message;

	/** Unique id of the lost session. */
	UPROPERTY(BlueprintReadOnly, Category = "CoopKit|Sessions")
	FString SessionId;

	/**
	 * Custom attributes the lost session advertised (from the host's profile or set at runtime), as this machine found
	 * them before it joined. Attributes the host changed after that are not included.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "CoopKit|Sessions")
	TMap<FName, FString> Attributes;

	/** The search result this machine joined, e.g. to read it with the CoopKit Sessions library. Its host is gone. */
	UPROPERTY(BlueprintReadOnly, Category = "CoopKit|Sessions")
	FCKSessionsSearchResult Session;
};
