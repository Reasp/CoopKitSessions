// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsLibrary.h"
#include "CKSessionsNetTestSession.h"
#include "CKSessionsSubsystem.h"
#include "CKSessionsTestQuickPlayRecorder.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace CKSessions::Tests
{
	namespace
	{
		const TCHAR* AttributesDemoMap = TEXT("/CoopKitSessions/Maps/L_SessionsDemo");

		/** One Null search (5 s) plus room for a busy machine, at the 30 fps cap of the session. */
		constexpr int32 AttributeSearchTicks = 300;

		/** Opening the map as a listen server and creating the session, or resolving, travelling and logging in. */
		constexpr int32 AttributeTravelTicks = 300;

		TSoftObjectPtr<UWorld> GetAttributesDemoMap()
		{
			return TSoftObjectPtr<UWorld>(FSoftObjectPath(FString(AttributesDemoMap) + TEXT(".L_SessionsDemo")));
		}

		UCKSessionsSubsystem* SessionsOf(UGameInstance* GameInstance)
		{
			return GameInstance != nullptr ? GameInstance->GetSubsystem<UCKSessionsSubsystem>() : nullptr;
		}

		int32 CountPlayersIn(const UWorld* World)
		{
			return World != nullptr && World->GetGameState() != nullptr ? World->GetGameState()->PlayerArray.Num() : -1;
		}

		TMap<FName, FString> MakeAttributes(const FName Key, const FString& Value)
		{
			TMap<FName, FString> Attributes;
			Attributes.Add(Key, Value);
			return Attributes;
		}

		/** True once the instance hosts its own listen server session. */
		bool IsListenHost(UGameInstance* GameInstance, const UWorld* World)
		{
			const UCKSessionsSubsystem* Sessions = SessionsOf(GameInstance);
			return Sessions != nullptr && Sessions->GetState() == ECKSessionsState::InSession && Sessions->IsHost()
				&& World != nullptr && World->GetNetMode() == NM_ListenServer;
		}

		/**
		 * Client A searches with Filter. The step passes once the search is over; a wrong number of results or a result
		 * without the expected attribute value is reported as an error.
		 */
		FNetStep MakeFilteredSearchStep(FAutomationTestBase& Test, const TSharedRef<FNetTestSession>& Session, const FString& Name,
			const TMap<FName, FString>& Filter, const int32 ExpectedResults)
		{
			return { Name,
				[Session, Filter]()
				{
					SessionsOf(Session->GetClientGameInstance(0))->FindSessionsWithAttributes(nullptr, Filter);
				},
				[TestPtr = &Test, Session, Name, Filter, ExpectedResults]()
				{
					const UCKSessionsSubsystem* Sessions = SessionsOf(Session->GetClientGameInstance(0));
					if (Sessions->GetState() != ECKSessionsState::Idle)
					{
						return false;
					}
					const TArray<FCKSessionsSearchResult>& Results = Sessions->GetSearchResults();
					TestPtr->TestEqual(*FString::Printf(TEXT("%s: sessions found"), *Name), Results.Num(), ExpectedResults);
					for (const FCKSessionsSearchResult& Result : Results)
					{
						for (const TPair<FName, FString>& Wanted : Filter)
						{
							FString Value;
							TestPtr->TestTrue(*FString::Printf(TEXT("%s: result advertises %s = %s"), *Name, *Wanted.Key.ToString(), *Wanted.Value),
								UCKSessionsLibrary::GetSessionAttribute(Result, Wanted.Key, Value) && Value == Wanted.Value);
						}
					}
					return true;
				},
				AttributeSearchTicks };
		}

		/**
		 * The host and client B both host listen server sessions of the same type (B on the next free port), the host
		 * sets MatchId during the game, and client A's searches with attribute filters return only the matching
		 * sessions; A then joins the session of the match.
		 */
		bool RunAttributesScenario(FAutomationTestBase& Test)
		{
			FNetSessionSettings Settings;
			Settings.Host = ENetSessionHost::ListenServer;
			Settings.ClientConnect = ENetClientConnect::Manual;
			Settings.RemoteClients = 2;
			const TSharedRef<FNetTestSession> Session = MakeShared<FNetTestSession>(Settings);

			TArray<FNetStep> Steps;
			Steps.Add({ TEXT("Host (Mode = Coop) and client B (Mode = Coop, MatchId = M2) host listen server sessions"),
				[Session]()
				{
					SessionsOf(Session->GetHostGameInstance())->HostSessionWithAttributes(nullptr, GetAttributesDemoMap(), MakeAttributes(TEXT("Mode"), TEXT("Coop")));
					TMap<FName, FString> OtherMatch = MakeAttributes(TEXT("Mode"), TEXT("Coop"));
					OtherMatch.Add(TEXT("MatchId"), TEXT("M2"));
					SessionsOf(Session->GetClientGameInstance(1))->HostSessionWithAttributes(nullptr, GetAttributesDemoMap(), OtherMatch);
				},
				[Session]()
				{
					return IsListenHost(Session->GetHostGameInstance(), Session->GetServerWorld())
						&& IsListenHost(Session->GetClientGameInstance(1), Session->GetClientWorld(1));
				},
				AttributeTravelTicks });

			Steps.Add({ TEXT("Host refuses a reserved key and sets MatchId = M1 during the game"),
				[TestPtr = &Test, Session]()
				{
					UCKSessionsSubsystem* Sessions = SessionsOf(Session->GetHostGameInstance());
					TestPtr->TestFalse(TEXT("Reserved key refused"), Sessions->SetSessionAttribute(TEXT("CKS_TYPE"), TEXT("Versus")));
					TestPtr->TestTrue(TEXT("MatchId set"), Sessions->SetSessionAttribute(TEXT("MatchId"), TEXT("M1")));
				},
				[TestPtr = &Test, Session]()
				{
					const TMap<FName, FString> Attributes = SessionsOf(Session->GetHostGameInstance())->GetSessionAttributes();
					TestPtr->TestEqual(TEXT("Host advertises MatchId"), Attributes.FindRef(TEXT("MatchId")), FString(TEXT("M1")));
					TestPtr->TestEqual(TEXT("Host keeps Mode"), Attributes.FindRef(TEXT("Mode")), FString(TEXT("Coop")));
					TestPtr->TestFalse(TEXT("Reserved key not stored"), Attributes.Contains(TEXT("CKS_TYPE")));
					return true;
				},
				5 });

			Steps.Add(MakeFilteredSearchStep(Test, Session, TEXT("Client A searches Mode = Coop and finds both sessions"),
				MakeAttributes(TEXT("Mode"), TEXT("Coop")), 2));
			Steps.Add(MakeFilteredSearchStep(Test, Session, TEXT("Client A searches MatchId = M3 and finds none"),
				MakeAttributes(TEXT("MatchId"), TEXT("M3")), 0));
			Steps.Add(MakeFilteredSearchStep(Test, Session, TEXT("Client A searches MatchId = M1 and finds only the host's session"),
				MakeAttributes(TEXT("MatchId"), TEXT("M1")), 1));

			Steps.Add({ TEXT("Client A joins the session of match M1"),
				[Session]()
				{
					UCKSessionsSubsystem* Sessions = SessionsOf(Session->GetClientGameInstance(0));
					if (Sessions->GetSearchResults().Num() == 1)
					{
						Sessions->JoinSession(Sessions->GetSearchResults()[0]);
					}
				},
				[Session]()
				{
					return SessionsOf(Session->GetClientGameInstance(0))->GetState() == ECKSessionsState::InSession
						&& CountPlayersIn(Session->GetServerWorld()) == 2 && CountPlayersIn(Session->GetClientWorld(1)) == 1;
				},
				AttributeTravelTicks });

			// The refused reserved key is part of the scenario: exactly one warning.
			Test.AddExpectedMessagePlain(TEXT("Set Session Attribute: key 'CKS_TYPE' is empty or reserved"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);

			if (!Session->Start(Test, AttributesDemoMap))
			{
				return false;
			}
			Test.AddInfo(FString::Printf(TEXT("Scenario: %s, two listen server sessions, budgets: search %d, travel %d ticks"),
				*Session->Describe(), AttributeSearchTicks, AttributeTravelTicks));
			Test.AddCommand(new FNetScenarioCommand(Test, Session, MoveTemp(Steps)));
			return true;
		}

		using FQuickPlayRecorderRef = TSharedRef<TStrongObjectPtr<UCKSessionsTestQuickPlayRecorder>>;

		FQuickPlayRecorderRef MakeQuickPlayRecorder()
		{
			return MakeShared<TStrongObjectPtr<UCKSessionsTestQuickPlayRecorder>>(NewObject<UCKSessionsTestQuickPlayRecorder>(GetTransientPackage()));
		}

		void StartQuickPlay(UGameInstance* GameInstance, const FQuickPlayRecorderRef& Recorder, const FName Key, const FString& Value)
		{
			UCKSessionsSubsystem* Sessions = SessionsOf(GameInstance);
			(*Recorder)->Watch(*Sessions);
			Sessions->QuickPlay(nullptr, MakeAttributes(Key, Value), GetAttributesDemoMap());
		}

		/** Reports how a quick play ended against the expectation; true once it ended. */
		bool CheckQuickPlay(FAutomationTestBase& Test, const FQuickPlayRecorderRef& Recorder, const TCHAR* Who, const bool bExpectHosted)
		{
			const UCKSessionsTestQuickPlayRecorder* Record = Recorder->Get();
			if (!Record->IsCompleted())
			{
				return false;
			}
			Test.TestTrue(*FString::Printf(TEXT("%s: quick play succeeded (failure %s)"), Who, *UEnum::GetValueAsString(Record->GetFailure())), Record->Succeeded());
			Test.TestTrue(*FString::Printf(TEXT("%s: quick play %s"), Who, bExpectHosted ? TEXT("hosted") : TEXT("joined")), Record->Hosted() == bExpectHosted);
			return true;
		}

		/**
		 * Quick play on Null: the host (Mode = Survival) and client B (Mode = Versus) find nothing and host their own
		 * sessions; client A (Mode = Survival) finds the host's session, not B's, and joins it.
		 */
		bool RunQuickPlayScenario(FAutomationTestBase& Test)
		{
			FNetSessionSettings Settings;
			Settings.Host = ENetSessionHost::ListenServer;
			Settings.ClientConnect = ENetClientConnect::Manual;
			Settings.RemoteClients = 2;
			const TSharedRef<FNetTestSession> Session = MakeShared<FNetTestSession>(Settings);
			const FQuickPlayRecorderRef HostRecorder = MakeQuickPlayRecorder();
			const FQuickPlayRecorderRef RecorderA = MakeQuickPlayRecorder();
			const FQuickPlayRecorderRef RecorderB = MakeQuickPlayRecorder();

			TArray<FNetStep> Steps;
			Steps.Add({ TEXT("Host (Survival) and client B (Versus) quick play, find nothing and host"),
				[Session, HostRecorder, RecorderB]()
				{
					StartQuickPlay(Session->GetHostGameInstance(), HostRecorder, TEXT("Mode"), TEXT("Survival"));
					StartQuickPlay(Session->GetClientGameInstance(1), RecorderB, TEXT("Mode"), TEXT("Versus"));
				},
				[TestPtr = &Test, Session, HostRecorder, RecorderB]()
				{
					if (!(*HostRecorder)->IsCompleted() || !(*RecorderB)->IsCompleted())
					{
						return false;
					}
					CheckQuickPlay(*TestPtr, HostRecorder, TEXT("Host"), true);
					CheckQuickPlay(*TestPtr, RecorderB, TEXT("Client B"), true);
					TestPtr->TestTrue(TEXT("Host is a listen server host"), IsListenHost(Session->GetHostGameInstance(), Session->GetServerWorld()));
					TestPtr->TestTrue(TEXT("Client B is a listen server host"), IsListenHost(Session->GetClientGameInstance(1), Session->GetClientWorld(1)));
					return true;
				},
				AttributeSearchTicks + AttributeTravelTicks });

			Steps.Add({ TEXT("Client A quick plays Survival and joins the host, not client B"),
				[Session, RecorderA]()
				{
					StartQuickPlay(Session->GetClientGameInstance(0), RecorderA, TEXT("Mode"), TEXT("Survival"));
				},
				[TestPtr = &Test, Session, RecorderA]()
				{
					if (!CheckQuickPlay(*TestPtr, RecorderA, TEXT("Client A"), false))
					{
						return false;
					}
					const UCKSessionsSubsystem* Sessions = SessionsOf(Session->GetClientGameInstance(0));
					TestPtr->TestEqual(TEXT("Client A found only the Survival session"), Sessions->GetSearchResults().Num(), 1);
					TestPtr->TestTrue(TEXT("Client A is in the session as a client"), Sessions->GetState() == ECKSessionsState::InSession && !Sessions->IsHost());
					TestPtr->TestEqual(TEXT("Host sees two players"), CountPlayersIn(Session->GetServerWorld()), 2);
					TestPtr->TestEqual(TEXT("Client B is still alone"), CountPlayersIn(Session->GetClientWorld(1)), 1);
					return true;
				},
				AttributeSearchTicks + AttributeTravelTicks });

			if (!Session->Start(Test, AttributesDemoMap))
			{
				return false;
			}
			Test.AddInfo(FString::Printf(TEXT("Scenario: %s, quick play, budgets: search %d, travel %d ticks"),
				*Session->Describe(), AttributeSearchTicks, AttributeTravelTicks));
			Test.AddCommand(new FNetScenarioCommand(Test, Session, MoveTemp(Steps)));
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsAttributesListenTest, "CoopKit.Sessions.Attributes.ListenServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsAttributesListenTest::RunTest(const FString& Parameters)
{
	return CKSessions::Tests::RunAttributesScenario(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsQuickPlayListenTest, "CoopKit.Sessions.QuickPlay.ListenServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsQuickPlayListenTest::RunTest(const FString& Parameters)
{
	return CKSessions::Tests::RunQuickPlayScenario(*this);
}

#endif // WITH_DEV_AUTOMATION_TESTS
