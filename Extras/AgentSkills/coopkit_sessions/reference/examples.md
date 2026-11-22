# Examples

Replace `Sleepwalker`/`SLEEPWALKER_API` with the project's module name and API macro (module `Sleepwalker` gives `SLEEPWALKER_API`) and the copyright line with the project's. The plugin's classes are `final`: the examples extend it with components and a subsystem of the project that use the plugin's calls, events and attributes.

## 1. "A player may be ready only after choosing a hero"

The lobby counts the ready flags (*Require All Ready* of the lobby rules). The project adds a component to its player state that holds the choice and sets the plugin's ready flag only when a hero is chosen.

`Source/Sleepwalker/Public/SleepwalkerHeroPickComponent.h`

```cpp
// Copyright 2026 Example Games. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "SleepwalkerHeroPickComponent.generated.h"

namespace Sleepwalker::Lobby
{
	/** True when a player with this hero choice may be ready: a hero is chosen (NAME_None means none yet). */
	SLEEPWALKER_API bool CanBeReady(FName HeroId);
}

/**
 * Put it on the player state (Blueprint: Add Component). The player picks a hero with PickHero; the lobby ready flag of
 * CoopKit Sessions follows the choice: ready only while a hero is chosen.
 */
UCLASS(ClassGroup = (Sleepwalker), meta = (BlueprintSpawnableComponent))
class SLEEPWALKER_API USleepwalkerHeroPickComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** The hero chosen, or None. */
	UFUNCTION(BlueprintPure, Category = "Sleepwalker|Lobby")
	FName GetHeroId() const
	{
		return HeroId;
	}

	/** Chooses a hero (None clears the choice) and updates the ready flag: ready needs a hero, a cleared choice cancels ready. */
	UFUNCTION(BlueprintCallable, Category = "Sleepwalker|Lobby")
	void PickHero(FName NewHeroId);

private:
	FName HeroId;
};
```

`Source/Sleepwalker/Private/SleepwalkerHeroPickComponent.cpp`

```cpp
// Copyright 2026 Example Games. All Rights Reserved.

#include "SleepwalkerHeroPickComponent.h"
#include "CKSessionsPlayerComponent.h"
#include "GameFramework/PlayerState.h"

namespace Sleepwalker::Lobby
{
	bool CanBeReady(const FName HeroId)
	{
		return !HeroId.IsNone();
	}
}

void USleepwalkerHeroPickComponent::PickHero(const FName NewHeroId)
{
	HeroId = NewHeroId;
	const APlayerState* PlayerState = Cast<APlayerState>(GetOwner());
	// SetReady works on the server and on the owning client (the plugin sends it to the server); nobody else may call it.
	if (UCKSessionsPlayerComponent* Ready = UCKSessionsPlayerComponent::FindPlayerComponent(PlayerState))
	{
		Ready->SetReady(Sleepwalker::Lobby::CanBeReady(HeroId));
	}
}
```

`Build.cs` of the project module: `PublicDependencyModuleNames.Add("CoopKitSessions");` (the header of a project class may expose plugin types later; keep it public).

How the user uses it: open the project's player state Blueprint, add **Sleepwalker Hero Pick** and call **Pick Hero** from the hero buttons of the lobby UI. In the lobby rules asset (card of **Sessions Lobby Rules**) keep **Require All Ready** on: the countdown starts when every player has a hero.

## 2. A server list with a "Difficulty" value

The host advertises `Difficulty` with its session; the server list shows the host name, players, ping and difficulty, and searches for one difficulty. The pure formatting function is tested without a network.

`Source/Sleepwalker/Public/SleepwalkerServerList.h`

```cpp
// Copyright 2026 Example Games. All Rights Reserved.

#pragma once

#include "CKSessionsTypes.h"
#include "CoreMinimal.h"

class UCKSessionsProfileData;
class UObject;
class UWorld;

namespace Sleepwalker::Sessions
{
	/** Key of the advertised difficulty; not a key of the plugin or the engine (CKSessions profiles refuse those). */
	inline const FName DifficultyKey(TEXT("Difficulty"));

	/** One row of the server list: "Host (2/4) 35 ms, Hard". An empty difficulty is left out. */
	SLEEPWALKER_API FString FormatServerRow(const FString& HostName, int32 Players, int32 MaxPlayers, int32 PingMs, const FString& Difficulty);

	/** The row of a found session, read with the plugin's library; an empty string for an invalid result. */
	SLEEPWALKER_API FString DescribeResult(const FCKSessionsSearchResult& Result);

	/** The attributes that make a search return only sessions of this difficulty. */
	SLEEPWALKER_API TMap<FName, FString> MakeDifficultyFilter(const FString& Difficulty);

	/** Hosts a session of the profile on the map, advertising the difficulty. False when the request cannot start. */
	SLEEPWALKER_API bool HostWithDifficulty(const UObject* WorldContext, const UCKSessionsProfileData* Profile, TSoftObjectPtr<UWorld> Map, const FString& Difficulty);
}
```

`Source/Sleepwalker/Private/SleepwalkerServerList.cpp`

```cpp
// Copyright 2026 Example Games. All Rights Reserved.

#include "SleepwalkerServerList.h"
#include "CKSessionsLibrary.h"
#include "CKSessionsSubsystem.h"

namespace Sleepwalker::Sessions
{
	FString FormatServerRow(const FString& HostName, const int32 Players, const int32 MaxPlayers, const int32 PingMs, const FString& Difficulty)
	{
		FString Row = FString::Printf(TEXT("%s (%d/%d) %d ms"), *HostName, Players, MaxPlayers, PingMs);
		if (!Difficulty.IsEmpty())
		{
			Row += TEXT(", ") + Difficulty;
		}
		return Row;
	}

	FString DescribeResult(const FCKSessionsSearchResult& Result)
	{
		if (!UCKSessionsLibrary::IsValidResult(Result))
		{
			return FString();
		}
		int32 Players = 0;
		int32 MaxPlayers = 0;
		UCKSessionsLibrary::GetPlayerCounts(Result, Players, MaxPlayers);
		FString Difficulty;
		UCKSessionsLibrary::GetSessionAttribute(Result, DifficultyKey, Difficulty);
		return FormatServerRow(UCKSessionsLibrary::GetHostName(Result), Players, MaxPlayers, UCKSessionsLibrary::GetPingMs(Result), Difficulty);
	}

	TMap<FName, FString> MakeDifficultyFilter(const FString& Difficulty)
	{
		TMap<FName, FString> Filter;
		Filter.Add(DifficultyKey, Difficulty);
		return Filter;
	}

	bool HostWithDifficulty(const UObject* WorldContext, const UCKSessionsProfileData* Profile, TSoftObjectPtr<UWorld> Map, const FString& Difficulty)
	{
		UCKSessionsSubsystem* Subsystem = UCKSessionsSubsystem::Get(WorldContext);
		return Subsystem != nullptr && Subsystem->HostSessionWithAttributes(Profile, Map, MakeDifficultyFilter(Difficulty));
	}
}
```

The menu widget calls `Subsystem->FindSessionsWithAttributes(Profile, MakeDifficultyFilter(TEXT("Hard")))`, binds `OnFindComplete`, and fills its list with `DescribeResult` of each result. A failed search reports `bSuccess == false`; an empty list is a success: show "no games found". In Blueprint the same flow is the nodes **Host Session With Attributes**, **Find Sessions With Attributes** and **Get Session Attribute**.

How the user uses it: no asset change is needed. To advertise a fixed value on every session of a profile, add `Difficulty` to **Attributes** of that **Sessions Profile** in its card.

## 3. "A returning player gets their gear back"

The host keeps the record of a player who lost the connection for *Reconnect Grace Seconds* of the profile. When the player is back, the subsystem fires `OnPlayerReconnected` on the host; the project restores what it keeps for that player.

`Source/Sleepwalker/Public/SleepwalkerSessionWatcher.h`

```cpp
// Copyright 2026 Example Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SleepwalkerSessionWatcher.generated.h"

class APlayerState;

namespace Sleepwalker::Sessions
{
	/** The log line for a player who came back: the name, or "a player" when the player state has none. */
	SLEEPWALKER_API FString DescribeReturn(const FString& PlayerName);
}

/** Reacts to the host events of CoopKit Sessions. Created with the game instance; it only acts on the host. */
UCLASS()
class SLEEPWALKER_API USleepwalkerSessionWatcher : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	UFUNCTION()
	void HandlePlayerReconnected(APlayerState* PlayerState);
};
```

`Source/Sleepwalker/Private/SleepwalkerSessionWatcher.cpp`

```cpp
// Copyright 2026 Example Games. All Rights Reserved.

#include "SleepwalkerSessionWatcher.h"
#include "CKSessionsSubsystem.h"
#include "GameFramework/PlayerState.h"

namespace Sleepwalker::Sessions
{
	FString DescribeReturn(const FString& PlayerName)
	{
		return FString::Printf(TEXT("%s is back"), PlayerName.IsEmpty() ? TEXT("a player") : *PlayerName);
	}
}

void USleepwalkerSessionWatcher::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// The plugin's subsystem must exist first.
	if (UCKSessionsSubsystem* Sessions = Collection.InitializeDependency<UCKSessionsSubsystem>())
	{
		Sessions->OnPlayerReconnected.AddDynamic(this, &USleepwalkerSessionWatcher::HandlePlayerReconnected);
	}
}

void USleepwalkerSessionWatcher::Deinitialize()
{
	if (UCKSessionsSubsystem* Sessions = GetGameInstance() != nullptr ? GetGameInstance()->GetSubsystem<UCKSessionsSubsystem>() : nullptr)
	{
		Sessions->OnPlayerReconnected.RemoveDynamic(this, &USleepwalkerSessionWatcher::HandlePlayerReconnected);
	}
	Super::Deinitialize();
}

void USleepwalkerSessionWatcher::HandlePlayerReconnected(APlayerState* PlayerState)
{
	if (PlayerState == nullptr)
	{
		return;
	}
	// Give the gear back here: the project's own data for this player state (the host runs this).
	UE_LOG(LogTemp, Log, TEXT("%s"), *Sleepwalker::Sessions::DescribeReturn(PlayerState->GetPlayerName()));
}
```

`Build.cs` of the project module: `PublicDependencyModuleNames.Add("CoopKitSessions");`. For persistent gear use a save plugin's player restore event in the same handler.

## 4. The automation tests of the examples

`Source/Sleepwalker/Private/SleepwalkerSessionsTest.cpp`

```cpp
// Copyright 2026 Example Games. All Rights Reserved.

#include "CKSessionsProfileData.h"
#include "Misc/AutomationTest.h"
#include "SleepwalkerHeroPickComponent.h"
#include "SleepwalkerServerList.h"
#include "SleepwalkerSessionWatcher.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSleepwalkerSessionsTest, "Sleepwalker.Sessions.LobbyAndServerList",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSleepwalkerSessionsTest::RunTest(const FString& Parameters)
{
	// Example 1: ready needs a hero.
	TestFalse(TEXT("No hero, not ready"), Sleepwalker::Lobby::CanBeReady(NAME_None));
	TestTrue(TEXT("A hero, ready"), Sleepwalker::Lobby::CanBeReady(TEXT("Knight")));

	// Example 2: the row of the server list, with and without a difficulty.
	TestEqual(TEXT("A row with a difficulty"), Sleepwalker::Sessions::FormatServerRow(TEXT("Anna"), 2, 4, 35, TEXT("Hard")), FString(TEXT("Anna (2/4) 35 ms, Hard")));
	TestEqual(TEXT("A row without a difficulty"), Sleepwalker::Sessions::FormatServerRow(TEXT("Anna"), 2, 4, 35, FString()), FString(TEXT("Anna (2/4) 35 ms")));
	TestEqual(TEXT("An invalid result has no row"), Sleepwalker::Sessions::DescribeResult(FCKSessionsSearchResult()), FString());
	const TMap<FName, FString> Filter = Sleepwalker::Sessions::MakeDifficultyFilter(TEXT("Hard"));
	TestEqual(TEXT("The filter holds the difficulty"), Filter.FindRef(Sleepwalker::Sessions::DifficultyKey), FString(TEXT("Hard")));
	// The key must not be one the plugin or the engine reserves, or the plugin refuses the session.
	TestFalse(TEXT("The difficulty key is not reserved"), UCKSessionsProfileData::IsReservedAttributeKey(Sleepwalker::Sessions::DifficultyKey));

	// Example 3: the log line of a returning player.
	TestEqual(TEXT("A named player"), Sleepwalker::Sessions::DescribeReturn(TEXT("Anna")), FString(TEXT("Anna is back")));
	TestEqual(TEXT("A player without a name"), Sleepwalker::Sessions::DescribeReturn(FString()), FString(TEXT("a player is back")));
	return true;
}

#endif
```

The plugin's own tests host and join a real session on the Null online subsystem; a project test of its own flow does the same, never with Steam or EOS (see `CKSessionsSessionTest.cpp` in the plugin's tests folder for the shape).

### Testing a validation

A validation rule of the project lives in its own data asset (a `UPrimaryDataAsset` subclass with `IsDataValid`). Its test, inside `#if WITH_EDITOR`, because `IsDataValid` exists only in the editor: `USleepwalkerSquadData` and its `SquadSize` stand for your own class and property.

```cpp
#include "Misc/DataValidation.h"
#include "UObject/UnrealType.h"

// Bad data: the asset must report it.
USleepwalkerSquadData* Data = NewObject<USleepwalkerSquadData>(GetTransientPackage());
FIntProperty* Size = FindFProperty<FIntProperty>(Data->GetClass(), TEXT("SquadSize"));
if (!TestNotNull(TEXT("The property exists"), Size))
{
	return false;
}
Size->SetPropertyValue_InContainer(Data, 0);
FDataValidationContext Bad;
TestEqual(TEXT("A squad of 0 is invalid"), Data->IsDataValid(Bad), EDataValidationResult::Invalid);
TestTrue(TEXT("The error is reported"), Bad.GetNumErrors() > 0);

// Good data: no errors.
Size->SetPropertyValue_InContainer(Data, 4);
FDataValidationContext Good;
Data->IsDataValid(Good);
TestTrue(TEXT("A squad of 4 has no errors"), Good.GetNumErrors() == 0);
```
