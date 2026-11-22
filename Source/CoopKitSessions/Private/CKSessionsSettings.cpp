// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsSettings.h"
#include "CKSessionsLobbyRulesData.h"
#include "CKSessionsProfileData.h"
#include "Engine/World.h"

const UCKSessionsSettings& UCKSessionsSettings::Get()
{
	return *GetDefault<UCKSessionsSettings>();
}

FName UCKSessionsSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

const UCKSessionsProfileData* UCKSessionsSettings::LoadDefaultProfile() const
{
	if (const UCKSessionsProfileData* Profile = DefaultProfile.LoadSynchronous())
	{
		return Profile;
	}
	// The class defaults are a complete, valid profile: hosting works without creating any asset.
	return GetDefault<UCKSessionsProfileData>();
}

const UCKSessionsLobbyRulesData* UCKSessionsSettings::LoadDefaultLobbyRules() const
{
	return DefaultLobbyRules.LoadSynchronous();
}
