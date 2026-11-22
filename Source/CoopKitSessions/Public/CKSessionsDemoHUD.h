// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "CKSessionsDemoHUD.generated.h"

class UUserWidget;
class UWorld;

/** Restarts Play In Editor with the given number of standalone game windows; returns false when it cannot. */
DECLARE_DELEGATE_RetVal_OneParam(bool, FCKSessionsDemoRestartPlay, int32 /*Windows*/);

/**
 * HUD of the CoopKit Sessions demo maps (L_SessionsDemo and L_SessionsMatch). It puts a panel of buttons on the local
 * player's screen and shows the mouse cursor, so the whole demo runs with the mouse: host a game, find games and join
 * one, get ready, play the match and go back to the lobby. The panel shows the live state of the session and the lobby
 * and tells the player what to do next, including when the demo needs a second game window.
 *
 * The demo game mode (BP_CKSessionsDemoGameMode) uses it as its HUD Class. Set it on the game mode of your own test
 * map to get the same panel there. The panel calls only the plugin's Blueprint API; it is a demo, not a menu to ship.
 */
UCLASS(Blueprintable)
class COOPKITSESSIONS_API ACKSessionsDemoHUD : public AHUD
{
	GENERATED_BODY()

public:
	ACKSessionsDemoHUD();

	/** The panel on the screen; null before Begin Play and on a machine without a local player. */
	UUserWidget* GetPanel() const
	{
		return Panel;
	}

	/** Map the demo returns to: Leave opens it, Back to lobby takes everyone there. */
	const TSoftObjectPtr<UWorld>& GetLobbyMap() const
	{
		return LobbyMap;
	}

	/** Game windows the demo needs to show a session; the panel offers to restart Play In Editor with this many. */
	int32 GetPlayWindows() const
	{
		return PlayWindows;
	}

	/**
	 * Editor only: restarts Play In Editor with more standalone windows, without changing the user's Play settings. The
	 * CoopKitSessionsEditor module binds it; in a game it stays unbound and the panel tells the player to start a second
	 * copy of the game instead.
	 */
	static FCKSessionsDemoRestartPlay& OnRestartPlayInEditor();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Map the demo returns to: Leave opens it, Back to lobby takes everyone there. */
	UPROPERTY(EditDefaultsOnly, Category = "CoopKit|Sessions", meta = (AllowPrivateAccess = "true"))
	TSoftObjectPtr<UWorld> LobbyMap;

	/** Game windows the demo needs; at least the lobby rules' Min Players To Start. */
	UPROPERTY(EditDefaultsOnly, Category = "CoopKit|Sessions", meta = (AllowPrivateAccess = "true", ClampMin = "2", ClampMax = "4"))
	int32 PlayWindows = 2;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> Panel;
};
