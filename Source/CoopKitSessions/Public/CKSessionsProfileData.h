// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CKSessionsProfileData.generated.h"

class FDataValidationContext;

/** Where a session is advertised. */
UENUM(BlueprintType)
enum class ECKSessionsNetworkScope : uint8
{
	/** LAN when the online subsystem is Null (local play and tests), online with Steam, EOS and other services. */
	Auto,
	/** Local network only, with any online subsystem. */
	Lan,
	/** Through the online service. The Null subsystem has no online service and falls back to LAN. */
	Online
};

/**
 * Session profile: how a session of one kind is created, found, joined and reconnected to.
 * One asset per kind of session (co-op campaign, private game with friends, LAN party). Hosts and searches
 * use the same profile, and a search only returns sessions whose Session Type matches the profile.
 * Create one with New Sessions Profile in the CoopKit Hub (Tools > CoopKit Hub) or in Content Browser > Add > CoopKit >
 * Sessions Profile; it opens in its card: the data editor with the Preview and Parts tabs. It is the entity of the CoopKit
 * workspace (decision-63 of the CoopKit line); it has no parts: it refers to no other asset.
 */
UCLASS(BlueprintType, meta = (CoopKitEntity = "Sessions Profile", CoopKitEntityFolder = "SessionsProfiles"))
class COOPKITSESSIONS_API UCKSessionsProfileData final : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	FName GetSessionType() const
	{
		return SessionType;
	}

	int32 GetMaxPlayers() const
	{
		return MaxPlayers;
	}

	ECKSessionsNetworkScope GetNetworkScope() const
	{
		return NetworkScope;
	}

	bool UsesLobbies() const
	{
		return bUseLobbies;
	}

	bool IsPrivate() const
	{
		return bPrivate;
	}

	bool AllowsJoinInProgress() const
	{
		return bAllowJoinInProgress;
	}

	int32 GetReconnectAttempts() const
	{
		return ReconnectAttempts;
	}

	float GetReconnectIntervalSeconds() const
	{
		return ReconnectIntervalSeconds;
	}

	float GetReconnectGraceSeconds() const
	{
		return ReconnectGraceSeconds;
	}

	int32 GetMaxSearchResults() const
	{
		return MaxSearchResults;
	}

	const TMap<FName, FString>& GetAttributes() const
	{
		return Attributes;
	}

	/** Session setting keys the plugin uses itself; custom attributes must not reuse them. */
	static bool IsReservedAttributeKey(FName Key);

#if WITH_EDITOR
	/** Reports profiles the plugin cannot use: no session type, impossible player counts, broken reconnect settings, reserved attribute keys. */
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

private:
	/**
	 * Kind of session, advertised with the session. A search only returns sessions of the same type,
	 * so different game modes or game versions never meet. Required.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoopKit|Sessions", meta = (AllowPrivateAccess = "true"))
	FName SessionType = TEXT("Coop");

	/** Players in the session, the host included. 2 to 64. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoopKit|Sessions", meta = (AllowPrivateAccess = "true", ClampMin = "2", ClampMax = "64"))
	int32 MaxPlayers = 4;

	/** Where the session is advertised and searched for. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoopKit|Sessions", meta = (AllowPrivateAccess = "true"))
	ECKSessionsNetworkScope NetworkScope = ECKSessionsNetworkScope::Auto;

	/** Use the service's lobbies (Steam lobbies, EOS lobbies) instead of game server sessions. Ignored on LAN. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoopKit|Sessions", meta = (AllowPrivateAccess = "true"))
	bool bUseLobbies = true;

	/** Not advertised: searches never return it, players join through invites of the online service. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoopKit|Sessions", meta = (AllowPrivateAccess = "true"))
	bool bPrivate = false;

	/** Players may join after the match started (the host called Start Match). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoopKit|Sessions", meta = (AllowPrivateAccess = "true"))
	bool bAllowJoinInProgress = true;

	/** How many times a client tries to join the same session again after losing the connection. 0 turns reconnection off. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoopKit|Sessions|Reconnect", meta = (AllowPrivateAccess = "true", ClampMin = "0", ClampMax = "20"))
	int32 ReconnectAttempts = 3;

	/** Pause between reconnect attempts, in seconds. Each attempt also waits for a search (5 s with the Null subsystem). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoopKit|Sessions|Reconnect", meta = (AllowPrivateAccess = "true", ClampMin = "0.1", Units = "s"))
	float ReconnectIntervalSeconds = 2.0f;

	/** How long the host keeps the record of a player who lost the connection, in seconds. A player back in time counts as reconnected. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoopKit|Sessions|Reconnect", meta = (AllowPrivateAccess = "true", ClampMin = "0", Units = "s"))
	float ReconnectGraceSeconds = 120.0f;

	/** Most sessions one search returns. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoopKit|Sessions|Search", meta = (AllowPrivateAccess = "true", ClampMin = "1", ClampMax = "200"))
	int32 MaxSearchResults = 20;

	/**
	 * Extra values advertised with the session, e.g. Difficulty = Hard. Read them from a search result with Get Session Attribute;
	 * search for them with Find Sessions With Attributes or Quick Play. Host Session With Attributes and Set Session Attribute
	 * override them at runtime.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoopKit|Sessions|Search", meta = (AllowPrivateAccess = "true"))
	TMap<FName, FString> Attributes;
};
