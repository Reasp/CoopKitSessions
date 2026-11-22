// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "CKSessionsTypes.h"
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CKSessionsLibrary.generated.h"

/** Reads search results of CoopKit Sessions in Blueprint: host, players, ping, map and custom attributes. */
UCLASS(MinimalAPI)
class UCKSessionsLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** True when the result points at a real session. */
	UFUNCTION(BlueprintPure, Category = "CoopKit|Sessions")
	static COOPKITSESSIONS_API bool IsValidResult(const FCKSessionsSearchResult& Result);

	/** Name of the player or server that hosts the session. */
	UFUNCTION(BlueprintPure, Category = "CoopKit|Sessions")
	static COOPKITSESSIONS_API FString GetHostName(const FCKSessionsSearchResult& Result);

	/**
	 * Players in the session and its size.
	 * @param OutPlayers     Players already in the session as the online service counts them. Null counts the players who
	 *                       joined, without the listen server's own player; check other services by hand.
	 * @param OutMaxPlayers  Profile's Max Players.
	 */
	UFUNCTION(BlueprintPure, Category = "CoopKit|Sessions")
	static COOPKITSESSIONS_API void GetPlayerCounts(const FCKSessionsSearchResult& Result, int32& OutPlayers, int32& OutMaxPlayers);

	/** Round trip to the host in milliseconds as the search measured it; 9999 when unknown. */
	UFUNCTION(BlueprintPure, Category = "CoopKit|Sessions")
	static COOPKITSESSIONS_API int32 GetPingMs(const FCKSessionsSearchResult& Result);

	/** Short name of the host's map, e.g. L_Lobby. */
	UFUNCTION(BlueprintPure, Category = "CoopKit|Sessions")
	static COOPKITSESSIONS_API FString GetMapName(const FCKSessionsSearchResult& Result);

	/** True when the host already started the match; the session was found, so it takes players in progress. */
	UFUNCTION(BlueprintPure, Category = "CoopKit|Sessions")
	static COOPKITSESSIONS_API bool IsMatchInProgress(const FCKSessionsSearchResult& Result);

	/** True when the session is a dedicated server rather than a player's game. */
	UFUNCTION(BlueprintPure, Category = "CoopKit|Sessions")
	static COOPKITSESSIONS_API bool IsDedicatedServer(const FCKSessionsSearchResult& Result);

	/**
	 * Reads a custom attribute the session advertises: from the host's profile or set at runtime (Host Session With Attributes, Set Session Attribute).
	 * @return False when the session has no such attribute.
	 */
	UFUNCTION(BlueprintPure, Category = "CoopKit|Sessions")
	static COOPKITSESSIONS_API bool GetSessionAttribute(const FCKSessionsSearchResult& Result, FName Key, FString& OutValue);
};
