// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "CKSessionsSubsystem.h"
#include "CKSessionsTypes.h"
#include "CoreMinimal.h"
#include "Engine/World.h"
#include "Templates/Function.h"
#include "UObject/Object.h"
#include "UObject/Package.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "CKSessionsTestHostLossRecorder.generated.h"

/**
 * Test-only listener of one sessions subsystem: records On Host Lost, On Session Failure and every state change,
 * and runs the test's reaction inside On Host Lost, the way a host migration would.
 */
UCLASS(NotBlueprintable, HideDropdown)
class UCKSessionsTestHostLossRecorder final : public UObject
{
	GENERATED_BODY()

public:
	/** Starts listening to the subsystem. OnLost runs inside On Host Lost with the subsystem that fired it; may be empty. */
	void Watch(UCKSessionsSubsystem& InSessions, TFunction<void(UCKSessionsSubsystem&, const FCKSessionsHostLoss&)> InOnLost)
	{
		Sessions = &InSessions;
		OnLost = MoveTemp(InOnLost);
		InSessions.OnHostLost.AddDynamic(this, &UCKSessionsTestHostLossRecorder::HandleHostLost);
		InSessions.OnSessionFailure.AddDynamic(this, &UCKSessionsTestHostLossRecorder::HandleSessionFailure);
		InSessions.OnStateChanged.AddDynamic(this, &UCKSessionsTestHostLossRecorder::HandleStateChanged);
	}

	int32 GetHostLostCount() const
	{
		return HostLostCount;
	}

	const FCKSessionsHostLoss& GetHostLoss() const
	{
		return HostLoss;
	}

	int32 GetSessionFailureCount() const
	{
		return SessionFailureCount;
	}

	/** Every state the subsystem entered since Watch, in order. */
	const TArray<ECKSessionsState>& GetStates() const
	{
		return States;
	}

	/** How many states were entered before On Host Lost fired; INDEX_NONE before it fired. */
	int32 GetStatesBeforeHostLost() const
	{
		return StatesBeforeHostLost;
	}

	/** State, net mode and map (without the Play In Editor prefix) of the game instance when On Host Lost fired. */
	ECKSessionsState GetStateAtHostLost() const
	{
		return StateAtHostLost;
	}

	ENetMode GetNetModeAtHostLost() const
	{
		return NetModeAtHostLost;
	}

	const FString& GetMapAtHostLost() const
	{
		return MapAtHostLost;
	}

private:
	UFUNCTION()
	void HandleHostLost(const FCKSessionsHostLoss& InHostLoss)
	{
		++HostLostCount;
		HostLoss = InHostLoss;
		StatesBeforeHostLost = States.Num();
		UCKSessionsSubsystem* Subsystem = Sessions.Get();
		if (Subsystem == nullptr)
		{
			return;
		}
		StateAtHostLost = Subsystem->GetState();
		const UWorld* World = Subsystem->GetWorld();
		NetModeAtHostLost = World != nullptr ? World->GetNetMode() : NM_MAX;
		MapAtHostLost = World != nullptr ? UWorld::RemovePIEPrefix(World->GetPackage()->GetName()) : FString();
		if (OnLost)
		{
			OnLost(*Subsystem, InHostLoss);
		}
	}

	UFUNCTION()
	void HandleSessionFailure(ECKSessionsFailure Failure, const FString& Message)
	{
		++SessionFailureCount;
	}

	UFUNCTION()
	void HandleStateChanged(ECKSessionsState NewState)
	{
		States.Add(NewState);
	}

	/** Weak: the recorder outlives Play In Editor and must not keep its game instance alive. */
	TWeakObjectPtr<UCKSessionsSubsystem> Sessions;

	TFunction<void(UCKSessionsSubsystem&, const FCKSessionsHostLoss&)> OnLost;
	int32 HostLostCount = 0;
	FCKSessionsHostLoss HostLoss;
	int32 SessionFailureCount = 0;
	TArray<ECKSessionsState> States;
	int32 StatesBeforeHostLost = INDEX_NONE;
	ECKSessionsState StateAtHostLost = ECKSessionsState::Idle;
	ENetMode NetModeAtHostLost = NM_MAX;
	FString MapAtHostLost;
};
