// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "CKSessionsSubsystem.h"
#include "CKSessionsTypes.h"
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "CKSessionsTestQuickPlayRecorder.generated.h"

/** Test-only listener that records how a Quick Play request of one sessions subsystem ended (joined, hosted or failed). */
UCLASS(NotBlueprintable, HideDropdown)
class UCKSessionsTestQuickPlayRecorder final : public UObject
{
	GENERATED_BODY()

public:
	/** Starts listening to the subsystem's On Quick Play Complete. Call before Quick Play. */
	void Watch(UCKSessionsSubsystem& Sessions)
	{
		Sessions.OnQuickPlayComplete.AddDynamic(this, &UCKSessionsTestQuickPlayRecorder::HandleQuickPlayComplete);
	}

	bool IsCompleted() const
	{
		return bCompleted;
	}

	bool Succeeded() const
	{
		return bSuccess;
	}

	bool Hosted() const
	{
		return bHosted;
	}

	ECKSessionsFailure GetFailure() const
	{
		return Failure;
	}

private:
	UFUNCTION()
	void HandleQuickPlayComplete(bool bInSuccess, bool bInHosted, ECKSessionsFailure InFailure)
	{
		bCompleted = true;
		bSuccess = bInSuccess;
		bHosted = bInHosted;
		Failure = InFailure;
	}

	bool bCompleted = false;
	bool bSuccess = false;
	bool bHosted = false;
	ECKSessionsFailure Failure = ECKSessionsFailure::None;
};
