// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsLibrary.h"
#include "CKSessionsOnlineSettings.h"
#include "Misc/Paths.h"
#include "Online/OnlineSessionNames.h"

bool UCKSessionsLibrary::IsValidResult(const FCKSessionsSearchResult& Result)
{
	return Result.IsValid();
}

FString UCKSessionsLibrary::GetHostName(const FCKSessionsSearchResult& Result)
{
	return Result.IsValid() ? Result.GetNative().Session.OwningUserName : FString();
}

void UCKSessionsLibrary::GetPlayerCounts(const FCKSessionsSearchResult& Result, int32& OutPlayers, int32& OutMaxPlayers)
{
	OutPlayers = 0;
	OutMaxPlayers = 0;
	if (Result.IsValid())
	{
		const FOnlineSession& Session = Result.GetNative().Session;
		OutMaxPlayers = Session.SessionSettings.NumPublicConnections + Session.SessionSettings.NumPrivateConnections;
		OutPlayers = FMath::Max(0, OutMaxPlayers - Session.NumOpenPublicConnections - Session.NumOpenPrivateConnections);
	}
}

int32 UCKSessionsLibrary::GetPingMs(const FCKSessionsSearchResult& Result)
{
	return Result.IsValid() ? Result.GetNative().PingInMs : 9999;
}

FString UCKSessionsLibrary::GetMapName(const FCKSessionsSearchResult& Result)
{
	FString MapName;
	if (Result.IsValid())
	{
		Result.GetNative().Session.SessionSettings.Get(SETTING_MAPNAME, MapName);
	}
	return FPaths::GetBaseFilename(MapName);
}

bool UCKSessionsLibrary::IsMatchInProgress(const FCKSessionsSearchResult& Result)
{
	return Result.IsValid() && CKSessions::Online::IsMatchInProgress(Result.GetNative().Session.SessionSettings);
}

bool UCKSessionsLibrary::IsDedicatedServer(const FCKSessionsSearchResult& Result)
{
	return Result.IsValid() && Result.GetNative().Session.SessionSettings.bIsDedicated;
}

bool UCKSessionsLibrary::GetSessionAttribute(const FCKSessionsSearchResult& Result, const FName Key, FString& OutValue)
{
	OutValue.Reset();
	return Result.IsValid() && Result.GetNative().Session.SessionSettings.Get(Key, OutValue);
}
