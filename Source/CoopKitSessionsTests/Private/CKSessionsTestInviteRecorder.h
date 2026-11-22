// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "CKSessionsSubsystem.h"
#include "CKSessionsTypes.h"
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "CKSessionsTestInviteRecorder.generated.h"

/** Test-only listener that records the invites, join results and friends results of one sessions subsystem. */
UCLASS(NotBlueprintable, HideDropdown)
class UCKSessionsTestInviteRecorder final : public UObject
{
	GENERATED_BODY()

public:
	/** Starts listening to On Invite Accepted, On Join Complete and On Read Friends Complete of the subsystem. */
	void Watch(UCKSessionsSubsystem& Sessions)
	{
		Sessions.OnInviteAccepted.AddDynamic(this, &UCKSessionsTestInviteRecorder::HandleInviteAccepted);
		Sessions.OnJoinComplete.AddDynamic(this, &UCKSessionsTestInviteRecorder::HandleJoinComplete);
		Sessions.OnReadFriendsComplete.AddDynamic(this, &UCKSessionsTestInviteRecorder::HandleReadFriendsComplete);
	}

	const TArray<FCKSessionsInvite>& GetInvites() const
	{
		return Invites;
	}

	int32 GetJoinSuccessCount() const
	{
		return JoinSuccesses;
	}

	const TArray<ECKSessionsFailure>& GetJoinFailures() const
	{
		return JoinFailures;
	}

	int32 GetReadFriendsCount() const
	{
		return ReadFriendsCount;
	}

	bool GetLastReadFriendsSuccess() const
	{
		return bLastReadFriendsSuccess;
	}

	ECKSessionsFailure GetLastReadFriendsFailure() const
	{
		return LastReadFriendsFailure;
	}

private:
	UFUNCTION()
	void HandleInviteAccepted(const FCKSessionsInvite& Invite)
	{
		Invites.Add(Invite);
	}

	UFUNCTION()
	void HandleJoinComplete(bool bSuccess, ECKSessionsFailure Failure)
	{
		if (bSuccess)
		{
			++JoinSuccesses;
		}
		else
		{
			JoinFailures.Add(Failure);
		}
	}

	UFUNCTION()
	void HandleReadFriendsComplete(bool bSuccess, ECKSessionsFailure Failure, const TArray<FCKSessionsFriend>& Friends)
	{
		++ReadFriendsCount;
		bLastReadFriendsSuccess = bSuccess;
		LastReadFriendsFailure = Failure;
	}

	TArray<FCKSessionsInvite> Invites;
	int32 JoinSuccesses = 0;
	TArray<ECKSessionsFailure> JoinFailures;
	int32 ReadFriendsCount = 0;
	bool bLastReadFriendsSuccess = false;
	ECKSessionsFailure LastReadFriendsFailure = ECKSessionsFailure::None;
};
