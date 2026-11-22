// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "CKSessionsTypes.h"
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CKSessionsSettings.generated.h"

class UCKSessionsLobbyRulesData;
class UCKSessionsProfileData;
class UWorld;

/**
 * Project settings of CoopKit Sessions: how the plugin plugs into the project. Gameplay data (session profiles,
 * lobby rules) lives in data assets; these settings only pick the defaults and the menu map.
 * Defaults ship in the plugin (Config/DefaultCoopKitSessions.ini); changes are saved to the project's
 * Config/DefaultCoopKitSessions.ini.
 */
UCLASS(Config = CoopKitSessions, DefaultConfig, meta = (DisplayName = "CoopKit Sessions"))
class COOPKITSESSIONS_API UCKSessionsSettings final : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Settings object with the current project values. */
	static const UCKSessionsSettings& Get();

	virtual FName GetCategoryName() const override;

	/** Profile used when a request passes none, and by a dedicated server that hosts on its own. Loads it synchronously if needed. */
	const UCKSessionsProfileData* LoadDefaultProfile() const;

	/** Lobby rules used by a lobby component that has none. Loads them synchronously if needed; may be null. */
	const UCKSessionsLobbyRulesData* LoadDefaultLobbyRules() const;

	const TSoftObjectPtr<UWorld>& GetMenuMap() const
	{
		return MenuMap;
	}

	bool ShouldDedicatedServerHost() const
	{
		return bDedicatedServerHosts;
	}

	ECKSessionsInvitePolicy GetInviteAcceptPolicy() const
	{
		return InviteAcceptPolicy;
	}

private:
	/** Profile for requests without one. Empty: the built-in defaults of a new Sessions Profile (4 players, LAN with Null). */
	UPROPERTY(Config, EditAnywhere, Category = "Defaults", meta = (AllowedClasses = "/Script/CoopKitSessions.CKSessionsProfileData"))
	TSoftObjectPtr<UCKSessionsProfileData> DefaultProfile;

	/** Lobby rules for lobby components without their own. */
	UPROPERTY(Config, EditAnywhere, Category = "Defaults", meta = (AllowedClasses = "/Script/CoopKitSessions.CKSessionsLobbyRulesData"))
	TSoftObjectPtr<UCKSessionsLobbyRulesData> DefaultLobbyRules;

	/** Map a player returns to after leaving a session or when the session fails. Empty: the game default map. */
	UPROPERTY(Config, EditAnywhere, Category = "Travel")
	TSoftObjectPtr<UWorld> MenuMap;

	/** A dedicated server creates the session with the default profile as soon as its map begins play. */
	UPROPERTY(Config, EditAnywhere, Category = "Dedicated Server")
	bool bDedicatedServerHosts = true;

	/**
	 * What happens when the player accepts an invite to a game session of this game (Steam overlay, Join Game, a game
	 * started by an invite, EOS). Event Only (default, as 1.0.0 plus the event): only On Invite Accepted fires. Join At
	 * Once: the player leaves the current session without being asked, also in a running match, and joins the invited one.
	 * The subsystem's Set Invite Accept Policy changes it at runtime. Invites to a party session are never taken.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Invites")
	ECKSessionsInvitePolicy InviteAcceptPolicy = ECKSessionsInvitePolicy::EventOnly;
};
