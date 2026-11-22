// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "CKSessionsSubsystem.h"
#include "CKSessionsTypes.h"
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "CKSessionsTestRemovalRecorder.generated.h"

/**
 * Test-only listener that records what a client of one sessions subsystem is told when the host removes it (On Session
 * Failure), how a Join Session request ends (On Join Complete), and whether the subsystem ever tried to reconnect.
 */
UCLASS(NotBlueprintable, HideDropdown)
class UCKSessionsTestRemovalRecorder final : public UObject
{
	GENERATED_BODY()

public:
	/** Starts listening to the subsystem. Call before the events to record. */
	void Watch(UCKSessionsSubsystem& Sessions)
	{
		Sessions.OnSessionFailure.AddDynamic(this, &UCKSessionsTestRemovalRecorder::HandleSessionFailure);
		Sessions.OnJoinComplete.AddDynamic(this, &UCKSessionsTestRemovalRecorder::HandleJoinComplete);
		Sessions.OnStateChanged.AddDynamic(this, &UCKSessionsTestRemovalRecorder::HandleStateChanged);
	}

	/** Forgets the recorded events (not the reconnect flag), to record the next removal or join. */
	void Reset()
	{
		bSessionFailed = false;
		SessionFailure = ECKSessionsFailure::None;
		SessionMessage.Reset();
		bJoinCompleted = false;
		bJoinSucceeded = false;
		JoinFailure = ECKSessionsFailure::None;
	}

	bool HasSessionFailed() const
	{
		return bSessionFailed;
	}

	ECKSessionsFailure GetSessionFailure() const
	{
		return SessionFailure;
	}

	const FString& GetSessionMessage() const
	{
		return SessionMessage;
	}

	bool HasJoinCompleted() const
	{
		return bJoinCompleted;
	}

	bool DidJoinSucceed() const
	{
		return bJoinSucceeded;
	}

	ECKSessionsFailure GetJoinFailure() const
	{
		return JoinFailure;
	}

	bool WasReconnecting() const
	{
		return bSawReconnecting;
	}

private:
	UFUNCTION()
	void HandleSessionFailure(ECKSessionsFailure InFailure, const FString& InMessage)
	{
		bSessionFailed = true;
		SessionFailure = InFailure;
		SessionMessage = InMessage;
	}

	UFUNCTION()
	void HandleJoinComplete(bool bInSuccess, ECKSessionsFailure InFailure)
	{
		bJoinCompleted = true;
		bJoinSucceeded = bInSuccess;
		JoinFailure = InFailure;
	}

	UFUNCTION()
	void HandleStateChanged(ECKSessionsState NewState)
	{
		bSawReconnecting |= NewState == ECKSessionsState::Reconnecting;
	}

	bool bSessionFailed = false;
	ECKSessionsFailure SessionFailure = ECKSessionsFailure::None;
	FString SessionMessage;
	bool bJoinCompleted = false;
	bool bJoinSucceeded = false;
	ECKSessionsFailure JoinFailure = ECKSessionsFailure::None;
	bool bSawReconnecting = false;
};
