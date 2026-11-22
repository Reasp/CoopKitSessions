// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "CKSessionsSubsystem.h"
#include "CKSessionsTypes.h"
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "CKSessionsTestFindRecorder.generated.h"

/**
 * Test-only listener of one sessions subsystem: every Find Sessions and Find Sessions In Background completion, and every
 * state change, so a test can tell the two searches apart and see that a background search leaves the state alone.
 */
UCLASS(NotBlueprintable, HideDropdown)
class UCKSessionsTestFindRecorder final : public UObject
{
	GENERATED_BODY()

public:
	/** Starts listening to the subsystem's find and state events. */
	void Watch(UCKSessionsSubsystem& Sessions)
	{
		Sessions.OnFindComplete.AddDynamic(this, &UCKSessionsTestFindRecorder::HandleFindComplete);
		Sessions.OnBackgroundFindComplete.AddDynamic(this, &UCKSessionsTestFindRecorder::HandleBackgroundFindComplete);
		Sessions.OnStateChanged.AddDynamic(this, &UCKSessionsTestFindRecorder::HandleStateChanged);
	}

	/** Forgets what was recorded; the listening goes on. */
	void Reset()
	{
		FindCount = 0;
		bFindSuccess = false;
		FindResults.Reset();
		BackgroundCount = 0;
		bBackgroundSuccess = false;
		BackgroundFailure = ECKSessionsFailure::None;
		BackgroundResults.Reset();
		States.Reset();
	}

	int32 GetFindCount() const
	{
		return FindCount;
	}

	bool FindSucceeded() const
	{
		return bFindSuccess;
	}

	const TArray<FCKSessionsSearchResult>& GetFindResults() const
	{
		return FindResults;
	}

	int32 GetBackgroundCount() const
	{
		return BackgroundCount;
	}

	bool BackgroundSucceeded() const
	{
		return bBackgroundSuccess;
	}

	ECKSessionsFailure GetBackgroundFailure() const
	{
		return BackgroundFailure;
	}

	const TArray<FCKSessionsSearchResult>& GetBackgroundResults() const
	{
		return BackgroundResults;
	}

	/** States the subsystem went through since the last Reset, in order. */
	const TArray<ECKSessionsState>& GetStates() const
	{
		return States;
	}

private:
	UFUNCTION()
	void HandleFindComplete(bool bSuccess, const TArray<FCKSessionsSearchResult>& Results)
	{
		++FindCount;
		bFindSuccess = bSuccess;
		FindResults = Results;
	}

	UFUNCTION()
	void HandleBackgroundFindComplete(bool bSuccess, ECKSessionsFailure Failure, const TArray<FCKSessionsSearchResult>& Results)
	{
		++BackgroundCount;
		bBackgroundSuccess = bSuccess;
		BackgroundFailure = Failure;
		BackgroundResults = Results;
	}

	UFUNCTION()
	void HandleStateChanged(ECKSessionsState NewState)
	{
		States.Add(NewState);
	}

	int32 FindCount = 0;
	bool bFindSuccess = false;
	TArray<FCKSessionsSearchResult> FindResults;
	int32 BackgroundCount = 0;
	bool bBackgroundSuccess = false;
	ECKSessionsFailure BackgroundFailure = ECKSessionsFailure::None;
	TArray<FCKSessionsSearchResult> BackgroundResults;
	TArray<ECKSessionsState> States;
};
