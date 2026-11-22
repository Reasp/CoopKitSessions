// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "GameplayTagAssetInterface.h"
#include "CKSessionsPlayerComponent.generated.h"

class APlayerState;
class UCKSessionsPlayerComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCKSessionsOnReadyChanged, UCKSessionsPlayerComponent*, Component, bool, bIsReady);

/**
 * Lobby state of one player: the ready flag. Lives on the player state, so the owning client may change it through a
 * server RPC and every machine sees it. The lobby component adds it to player states that lack it (Lobby Rules >
 * Add Player Components); add it to your player state Blueprint to bind events in the editor.
 * Ready is lobby-only: a new player state after travel starts not ready.
 */
UCLASS(ClassGroup = (CoopKit), meta = (BlueprintSpawnableComponent))
class COOPKITSESSIONS_API UCKSessionsPlayerComponent final : public UActorComponent, public IGameplayTagAssetInterface
{
	GENERATED_BODY()

public:
	UCKSessionsPlayerComponent();

	/** Component of a player state; null when it has none. */
	UFUNCTION(BlueprintPure, Category = "CoopKit|Sessions")
	static UCKSessionsPlayerComponent* FindPlayerComponent(const APlayerState* PlayerState);

	/** Component of the first local player's player state in the world of the object; null before the player state replicates. */
	UFUNCTION(BlueprintPure, Category = "CoopKit|Sessions", meta = (WorldContext = "WorldContextObject"))
	static UCKSessionsPlayerComponent* FindLocalPlayerComponent(const UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "CoopKit|Sessions")
	bool IsReady() const
	{
		return bReady;
	}

	/**
	 * Sets the ready flag. On the server it changes at once; on the owning client it is sent to the server and comes
	 * back through replication. Other clients cannot change it.
	 */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions")
	void SetReady(bool bInReady);

	/** Fires on every machine when the ready flag changes (server: at once; clients: on replication). */
	UPROPERTY(BlueprintAssignable, Category = "CoopKit|Sessions")
	FCKSessionsOnReadyChanged OnReadyChanged;

	// IGameplayTagAssetInterface: CK.Sessions.State.Ready while ready.
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	/** Client -> server. Reliable: a rare, explicit player action that must not be lost to packet loss. */
	UFUNCTION(Server, Reliable)
	void ServerSetReady(bool bInReady);

	UFUNCTION()
	void OnRep_Ready();

	UPROPERTY(ReplicatedUsing = OnRep_Ready)
	bool bReady = false;
};
