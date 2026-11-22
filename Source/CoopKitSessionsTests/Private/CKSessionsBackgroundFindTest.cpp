// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

// Find Sessions In Background (TASK-124): a search in any state that leaves the state alone. On Null, in one process: the
// host H and client B host sessions of groups G1 and G2; H searches while it hosts; client A searches while idle, next to
// its own Find Sessions in both orders (the online subsystem runs one search at a time), while it joins H, and as a client
// in H's session. Refusals (a background search in flight, a reserved key) do not fire the event.

#include "CKSessionsLibrary.h"
#include "CKSessionsNetTestSession.h"
#include "CKSessionsSubsystem.h"
#include "CKSessionsTestFindRecorder.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace CKSessions::Tests::BackgroundFind
{
	namespace
	{
		const TCHAR* DemoMap = TEXT("/CoopKitSessions/Maps/L_SessionsDemo");
		const FName GroupKey(TEXT("Group"));

		/** One Null search (5 s) plus room for a busy machine, at the 30 fps cap of the session. */
		constexpr int32 SearchTicks = 300;

		/** Opening the map as a listen server and creating the session, or resolving, travelling and logging in. */
		constexpr int32 TravelTicks = 300;

		TSoftObjectPtr<UWorld> GetDemoMap()
		{
			return TSoftObjectPtr<UWorld>(FSoftObjectPath(FString(DemoMap) + TEXT(".L_SessionsDemo")));
		}

		UCKSessionsSubsystem* SessionsOf(UGameInstance* GameInstance)
		{
			return GameInstance != nullptr ? GameInstance->GetSubsystem<UCKSessionsSubsystem>() : nullptr;
		}

		int32 CountPlayersIn(const UWorld* World)
		{
			return World != nullptr && World->GetGameState() != nullptr ? World->GetGameState()->PlayerArray.Num() : -1;
		}

		TMap<FName, FString> MakeGroup(const FString& Group)
		{
			TMap<FName, FString> Attributes;
			Attributes.Add(GroupKey, Group);
			return Attributes;
		}

		bool IsListenHost(UGameInstance* GameInstance, const UWorld* World)
		{
			const UCKSessionsSubsystem* Sessions = SessionsOf(GameInstance);
			return Sessions != nullptr && Sessions->GetState() == ECKSessionsState::InSession && Sessions->IsHost()
				&& World != nullptr && World->GetNetMode() == NM_ListenServer;
		}

		/** Exactly one result, and it advertises the group. */
		bool IsOnlyGroup(const TArray<FCKSessionsSearchResult>& Results, const FString& Group)
		{
			FString Value;
			return Results.Num() == 1 && UCKSessionsLibrary::GetSessionAttribute(Results[0], GroupKey, Value) && Value == Group;
		}

		using FRecorderRef = TSharedRef<TStrongObjectPtr<UCKSessionsTestFindRecorder>>;

		FRecorderRef MakeRecorder()
		{
			return MakeShared<TStrongObjectPtr<UCKSessionsTestFindRecorder>>(NewObject<UCKSessionsTestFindRecorder>(GetTransientPackage()));
		}

		struct FRun
		{
			FRecorderRef HostRecorder = MakeRecorder();
			FRecorderRef RecorderA = MakeRecorder();
			/** H's session as client A's background search found it, to join it later. */
			FCKSessionsSearchResult HostSession;
		};

		/** A background search that ended: one event, success, only the group's session, and no state change on the way. */
		void CheckBackground(FAutomationTestBase& Test, const FString& Name, const UCKSessionsTestFindRecorder& Recorder, const FString& Group)
		{
			Test.TestEqual(*FString::Printf(TEXT("%s: one background event"), *Name), Recorder.GetBackgroundCount(), 1);
			Test.TestTrue(*FString::Printf(TEXT("%s: the background search succeeded (failure %s)"), *Name, *UEnum::GetValueAsString(Recorder.GetBackgroundFailure())),
				Recorder.BackgroundSucceeded() && Recorder.GetBackgroundFailure() == ECKSessionsFailure::None);
			Test.TestTrue(*FString::Printf(TEXT("%s: the background search found only %s's session"), *Name, *Group), IsOnlyGroup(Recorder.GetBackgroundResults(), Group));
		}

		bool RunScenario(FAutomationTestBase& Test)
		{
			FNetSessionSettings Settings;
			Settings.Host = ENetSessionHost::ListenServer;
			Settings.ClientConnect = ENetClientConnect::Manual;
			Settings.RemoteClients = 2;
			const TSharedRef<FNetTestSession> Session = MakeShared<FNetTestSession>(Settings);
			const TSharedRef<FRun> Run = MakeShared<FRun>();
			FAutomationTestBase* TestPtr = &Test;

			TArray<FNetStep> Steps;
			Steps.Add({ TEXT("Host H (Group = G1) and client B (Group = G2) host listen server sessions"),
				[Session, Run]()
				{
					(*Run->HostRecorder)->Watch(*SessionsOf(Session->GetHostGameInstance()));
					(*Run->RecorderA)->Watch(*SessionsOf(Session->GetClientGameInstance(0)));
					SessionsOf(Session->GetHostGameInstance())->HostSessionWithAttributes(nullptr, GetDemoMap(), MakeGroup(TEXT("G1")));
					SessionsOf(Session->GetClientGameInstance(1))->HostSessionWithAttributes(nullptr, GetDemoMap(), MakeGroup(TEXT("G2")));
				},
				[Session]()
				{
					return IsListenHost(Session->GetHostGameInstance(), Session->GetServerWorld())
						&& IsListenHost(Session->GetClientGameInstance(1), Session->GetClientWorld(1));
				},
				TravelTicks });

			Steps.Add({ TEXT("H, the host, searches in the background for G2: finds B's session and stays In Session"),
				[TestPtr, Session, Run]()
				{
					(*Run->HostRecorder)->Reset();
					TestPtr->TestTrue(TEXT("H: the background search starts"), SessionsOf(Session->GetHostGameInstance())->FindSessionsInBackground(nullptr, MakeGroup(TEXT("G2"))));
					TestPtr->TestTrue(TEXT("H: a background search is in flight"), SessionsOf(Session->GetHostGameInstance())->IsBackgroundSearchInFlight());
				},
				[TestPtr, Session, Run]()
				{
					const UCKSessionsTestFindRecorder& Recorder = *Run->HostRecorder->Get();
					if (Recorder.GetBackgroundCount() == 0)
					{
						return false;
					}
					const UCKSessionsSubsystem* Sessions = SessionsOf(Session->GetHostGameInstance());
					CheckBackground(*TestPtr, TEXT("H"), Recorder, TEXT("G2"));
					TestPtr->TestEqual(TEXT("H: no state change"), Recorder.GetStates().Num(), 0);
					TestPtr->TestTrue(TEXT("H: still the host In Session"), IsListenHost(Session->GetHostGameInstance(), Session->GetServerWorld()));
					TestPtr->TestEqual(TEXT("H: Get Search Results untouched"), Sessions->GetSearchResults().Num(), 0);
					TestPtr->TestEqual(TEXT("H: Find Sessions did not fire"), Recorder.GetFindCount(), 0);
					TestPtr->TestFalse(TEXT("H: no background search in flight"), Sessions->IsBackgroundSearchInFlight());
					return true;
				},
				SearchTicks });

			Steps.Add({ TEXT("Client A, idle, searches in the background for G1; a second request and a reserved key are refused without an event"),
				[TestPtr, Session, Run]()
				{
					(*Run->RecorderA)->Reset();
					UCKSessionsSubsystem* Sessions = SessionsOf(Session->GetClientGameInstance(0));
					TestPtr->TestTrue(TEXT("A: the background search starts"), Sessions->FindSessionsInBackground(nullptr, MakeGroup(TEXT("G1"))));
					TestPtr->TestFalse(TEXT("A: a second background search is refused"), Sessions->FindSessionsInBackground(nullptr, MakeGroup(TEXT("G2"))));
					TMap<FName, FString> Reserved;
					Reserved.Add(TEXT("CKS_TYPE"), TEXT("Coop"));
					TestPtr->TestFalse(TEXT("A: a reserved key is refused"), Sessions->FindSessionsInBackground(nullptr, Reserved));
					TestPtr->TestEqual(TEXT("A: the refusals fired no event"), (*Run->RecorderA)->GetBackgroundCount(), 0);
				},
				[TestPtr, Session, Run]()
				{
					const UCKSessionsTestFindRecorder& Recorder = *Run->RecorderA->Get();
					if (Recorder.GetBackgroundCount() == 0)
					{
						return false;
					}
					CheckBackground(*TestPtr, TEXT("A idle"), Recorder, TEXT("G1"));
					TestPtr->TestEqual(TEXT("A: no state change"), Recorder.GetStates().Num(), 0);
					TestPtr->TestTrue(TEXT("A: still Idle"), SessionsOf(Session->GetClientGameInstance(0))->GetState() == ECKSessionsState::Idle);
					TestPtr->TestTrue(TEXT("A: Get Last Failure untouched"), SessionsOf(Session->GetClientGameInstance(0))->GetLastFailure() == ECKSessionsFailure::None);
					if (Recorder.GetBackgroundResults().Num() == 1)
					{
						Run->HostSession = Recorder.GetBackgroundResults()[0];
					}
					return true;
				},
				SearchTicks });

			Steps.Add({ TEXT("A: background search for G2, then Find Sessions for G1: the main search waits its turn, each gets its own results"),
				[TestPtr, Session, Run]()
				{
					(*Run->RecorderA)->Reset();
					UCKSessionsSubsystem* Sessions = SessionsOf(Session->GetClientGameInstance(0));
					TestPtr->TestTrue(TEXT("A: the background search starts"), Sessions->FindSessionsInBackground(nullptr, MakeGroup(TEXT("G2"))));
					TestPtr->TestTrue(TEXT("A: Find Sessions is accepted"), Sessions->FindSessionsWithAttributes(nullptr, MakeGroup(TEXT("G1"))));
					TestPtr->TestTrue(TEXT("A: Searching at once"), Sessions->GetState() == ECKSessionsState::Searching);
				},
				[TestPtr, Session, Run]()
				{
					const UCKSessionsTestFindRecorder& Recorder = *Run->RecorderA->Get();
					if (Recorder.GetBackgroundCount() == 0 || Recorder.GetFindCount() == 0)
					{
						return false;
					}
					const UCKSessionsSubsystem* Sessions = SessionsOf(Session->GetClientGameInstance(0));
					CheckBackground(*TestPtr, TEXT("A, background first"), Recorder, TEXT("G2"));
					TestPtr->TestEqual(TEXT("A, background first: one Find Sessions event"), Recorder.GetFindCount(), 1);
					TestPtr->TestTrue(TEXT("A, background first: Find Sessions succeeded"), Recorder.FindSucceeded());
					TestPtr->TestTrue(TEXT("A, background first: Find Sessions found only G1"), IsOnlyGroup(Sessions->GetSearchResults(), TEXT("G1")));
					TestPtr->TestTrue(TEXT("A, background first: Idle again"), Sessions->GetState() == ECKSessionsState::Idle);
					return true;
				},
				2 * SearchTicks });

			Steps.Add({ TEXT("A: Find Sessions for G2, then a background search for G1: the background search waits its turn"),
				[TestPtr, Session, Run]()
				{
					(*Run->RecorderA)->Reset();
					UCKSessionsSubsystem* Sessions = SessionsOf(Session->GetClientGameInstance(0));
					TestPtr->TestTrue(TEXT("A: Find Sessions starts"), Sessions->FindSessionsWithAttributes(nullptr, MakeGroup(TEXT("G2"))));
					TestPtr->TestTrue(TEXT("A: the background search is accepted"), Sessions->FindSessionsInBackground(nullptr, MakeGroup(TEXT("G1"))));
					TestPtr->TestTrue(TEXT("A: the background search counts as in flight while it waits"), Sessions->IsBackgroundSearchInFlight());
				},
				[TestPtr, Session, Run]()
				{
					const UCKSessionsTestFindRecorder& Recorder = *Run->RecorderA->Get();
					if (Recorder.GetBackgroundCount() == 0 || Recorder.GetFindCount() == 0)
					{
						return false;
					}
					const UCKSessionsSubsystem* Sessions = SessionsOf(Session->GetClientGameInstance(0));
					CheckBackground(*TestPtr, TEXT("A, main first"), Recorder, TEXT("G1"));
					TestPtr->TestTrue(TEXT("A, main first: Find Sessions succeeded"), Recorder.FindSucceeded());
					TestPtr->TestTrue(TEXT("A, main first: Find Sessions found only G2"), IsOnlyGroup(Sessions->GetSearchResults(), TEXT("G2")));
					TestPtr->TestTrue(TEXT("A, main first: Idle again"), Sessions->GetState() == ECKSessionsState::Idle);
					return true;
				},
				2 * SearchTicks });

			Steps.Add({ TEXT("A joins H while a background search for G2 runs: the join is not held up, the search ends on its own"),
				[TestPtr, Session, Run]()
				{
					(*Run->RecorderA)->Reset();
					UCKSessionsSubsystem* Sessions = SessionsOf(Session->GetClientGameInstance(0));
					TestPtr->TestTrue(TEXT("A: the background search starts"), Sessions->FindSessionsInBackground(nullptr, MakeGroup(TEXT("G2"))));
					TestPtr->TestTrue(TEXT("A: the join starts"), Sessions->JoinSession(Run->HostSession));
				},
				[TestPtr, Session, Run]()
				{
					const UCKSessionsTestFindRecorder& Recorder = *Run->RecorderA->Get();
					const UCKSessionsSubsystem* Sessions = SessionsOf(Session->GetClientGameInstance(0));
					if (Recorder.GetBackgroundCount() == 0 || Sessions->GetState() != ECKSessionsState::InSession || CountPlayersIn(Session->GetServerWorld()) != 2)
					{
						return false;
					}
					CheckBackground(*TestPtr, TEXT("A joining"), Recorder, TEXT("G2"));
					TestPtr->TestFalse(TEXT("A is a client of H"), Sessions->IsHost());
					return true;
				},
				SearchTicks + TravelTicks });

			Steps.Add({ TEXT("A, a client in H's session, searches in the background for G2 and stays In Session"),
				[TestPtr, Session, Run]()
				{
					(*Run->RecorderA)->Reset();
					TestPtr->TestTrue(TEXT("A: the background search starts"), SessionsOf(Session->GetClientGameInstance(0))->FindSessionsInBackground(nullptr, MakeGroup(TEXT("G2"))));
				},
				[TestPtr, Session, Run]()
				{
					const UCKSessionsTestFindRecorder& Recorder = *Run->RecorderA->Get();
					if (Recorder.GetBackgroundCount() == 0)
					{
						return false;
					}
					CheckBackground(*TestPtr, TEXT("A in a session"), Recorder, TEXT("G2"));
					TestPtr->TestEqual(TEXT("A in a session: no state change"), Recorder.GetStates().Num(), 0);
					TestPtr->TestTrue(TEXT("A in a session: still In Session"), SessionsOf(Session->GetClientGameInstance(0))->GetState() == ECKSessionsState::InSession);
					TestPtr->TestEqual(TEXT("H still sees two players"), CountPlayersIn(Session->GetServerWorld()), 2);
					return true;
				},
				SearchTicks });

			// The refused reserved key is part of the scenario: exactly one warning.
			Test.AddExpectedMessagePlain(TEXT("cannot search in the background, attribute key 'CKS_TYPE' is empty or reserved"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);

			if (!Session->Start(Test, DemoMap))
			{
				return false;
			}
			Test.AddInfo(FString::Printf(TEXT("Scenario: %s, background searches next to hosting, Find Sessions and a join, budgets: search %d, travel %d ticks"),
				*Session->Describe(), SearchTicks, TravelTicks));
			Test.AddCommand(new FNetScenarioCommand(Test, Session, MoveTemp(Steps)));
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsBackgroundFindListenTest, "CoopKit.Sessions.BackgroundFind.ListenServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsBackgroundFindListenTest::RunTest(const FString& Parameters)
{
	return CKSessions::Tests::BackgroundFind::RunScenario(*this);
}

#endif // WITH_DEV_AUTOMATION_TESTS
