// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "Blueprint/UserWidget.h"
#include "CKSessionsDemoHUD.h"
#include "CKSessionsDemoPanelWidget.h"
#include "CKSessionsLobbyComponent.h"
#include "CKSessionsNetTestSession.h"
#include "CKSessionsSettings.h"
#include "CKSessionsSubsystem.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "IMessageLogListing.h"
#include "ImageUtils.h"
#include "Logging/TokenizedMessage.h"
#include "MessageLogModule.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDevice.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SButton.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace CKSessions::Tests
{
	namespace
	{
		const TCHAR* DemoLobbyMap = TEXT("/CoopKitSessions/Maps/L_SessionsDemo");
		const TCHAR* DemoMatchMap = TEXT("/CoopKitSessions/Maps/L_SessionsMatch");

		namespace WidgetNames = CKSessions::Demo::WidgetNames;

		TAutoConsoleVariable<FString> CVarDemoShotsDir(
			TEXT("ck.sessions.tests.DemoShotsDir"),
			TEXT(""),
			TEXT("Test only. A folder: the demo tests save a PNG of every PIE window at each milestone. Needs rendering (not -nullrhi)."),
			ECVF_Cheat);

		/** Collects every warning and error the engine logs while it is alive, from any thread. */
		class FDemoLogCapture final : public FOutputDevice
		{
		public:
			FDemoLogCapture()
			{
				GLog->AddOutputDevice(this);
			}

			virtual ~FDemoLogCapture() override
			{
				GLog->RemoveOutputDevice(this);
			}

			virtual void Serialize(const TCHAR* Message, const ELogVerbosity::Type Verbosity, const FName& Category) override
			{
				const ELogVerbosity::Type Level = static_cast<ELogVerbosity::Type>(Verbosity & ELogVerbosity::VerbosityMask);
				if (Level == ELogVerbosity::Fatal || Level == ELogVerbosity::Error || Level == ELogVerbosity::Warning)
				{
					FScopeLock Lock(&Guard);
					Lines.Add(FString::Printf(TEXT("%s: %s: %s"), *Category.ToString(), ToString(Level), Message));
				}
			}

			virtual bool CanBeUsedOnAnyThread() const override
			{
				return true;
			}

			/** Lines caught since the last call; the capture starts over. */
			TArray<FString> TakeLines()
			{
				FScopeLock Lock(&Guard);
				TArray<FString> Taken = MoveTemp(Lines);
				Lines.Reset();
				return Taken;
			}

		private:
			FCriticalSection Guard;
			TArray<FString> Lines;
		};

		/** Reports every warning and error line as a test error; returns how many there were. */
		int32 ReportLogProblems(FAutomationTestBase& Test, FDemoLogCapture& Capture, const TCHAR* When)
		{
			const TArray<FString> Lines = Capture.TakeLines();
			for (const FString& Line : Lines)
			{
				Test.AddError(FString::Printf(TEXT("%s: %s"), When, *Line));
			}
			return Lines.Num();
		}

		/** Loads a map in the editor like a buyer opening it, runs Map Check and reports its warnings and errors. */
		bool LoadMapAndCheck(FAutomationTestBase& Test, FDemoLogCapture& Capture, const TCHAR* MapPackageName)
		{
			UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(MapPackageName);
			if (World == nullptr)
			{
				Test.AddError(FString::Printf(TEXT("Map %s did not load."), MapPackageName));
				return false;
			}
			GEditor->Exec(World, TEXT("MAP CHECK"));

			FMessageLogModule& MessageLog = FModuleManager::LoadModuleChecked<FMessageLogModule>(TEXT("MessageLog"));
			int32 Problems = 0;
			for (const TSharedRef<FTokenizedMessage>& Message : MessageLog.GetLogListing(TEXT("MapCheck"))->GetFilteredMessages())
			{
				if (Message->GetSeverity() <= EMessageSeverity::Warning)
				{
					Test.AddError(FString::Printf(TEXT("Map Check of %s: %s"), MapPackageName, *Message->ToText().ToString()));
					++Problems;
				}
			}
			Problems += ReportLogProblems(Test, Capture, *FString::Printf(TEXT("Loading %s"), MapPackageName));
			Test.AddInfo(FString::Printf(TEXT("%s loaded in the editor, Map Check: %d warnings or errors."), MapPackageName, Problems));
			return true;
		}

		/**
		 * Saves a PNG of every PIE window as <DemoShotsDir>/<Tag>_Window<N>.png when ck.sessions.tests.DemoShotsDir is set and
		 * the editor renders. A run without rendering (-nullrhi) saves nothing.
		 */
		void SaveWindowShots(FAutomationTestBase& Test, const FString& Tag)
		{
			const FString Dir = CVarDemoShotsDir.GetValueOnGameThread();
			if (Dir.IsEmpty() || !FApp::CanEverRender())
			{
				return;
			}
			TArray<const FWorldContext*> Contexts;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType == EWorldType::PIE && Context.GameViewport != nullptr)
				{
					Contexts.Add(&Context);
				}
			}
			Contexts.Sort([](const FWorldContext& Left, const FWorldContext& Right) { return Left.PIEInstance < Right.PIEInstance; });
			for (int32 Index = 0; Index < Contexts.Num(); ++Index)
			{
				// The whole window, as the player sees it: the viewport's own pixels lack the UMG widgets Slate draws on top.
				const TSharedPtr<SWindow> Window = Contexts[Index]->GameViewport->GetWindow();
				TArray<FColor> Pixels;
				FIntVector Size = FIntVector::ZeroValue;
				if (!Window.IsValid() || !FSlateApplication::IsInitialized() || !FSlateApplication::Get().TakeScreenshot(Window.ToSharedRef(), Pixels, Size)
					|| Pixels.Num() != Size.X * Size.Y)
				{
					Test.AddWarning(FString::Printf(TEXT("Window %d of '%s': no pixels to save."), Index + 1, *Tag));
					continue;
				}
				for (FColor& Pixel : Pixels)
				{
					Pixel.A = 255;
				}
				TArray64<uint8> Png;
				FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, Png);
				const FString File = FPaths::Combine(Dir, FString::Printf(TEXT("%s_Window%d.png"), *Tag, Index + 1));
				if (FFileHelper::SaveArrayToFile(Png, *File))
				{
					Test.AddInfo(FString::Printf(TEXT("Saved %s"), *File));
				}
			}
		}

		/** PIE worlds with a local player (dedicated servers excluded), by PIE instance. */
		TArray<UWorld*> GetDemoPieWorlds()
		{
			TArray<const FWorldContext*> Contexts;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType == EWorldType::PIE && !Context.RunAsDedicated && Context.World() != nullptr && Context.World()->HasBegunPlay())
				{
					Contexts.Add(&Context);
				}
			}
			Contexts.Sort([](const FWorldContext& Left, const FWorldContext& Right) { return Left.PIEInstance < Right.PIEInstance; });
			TArray<UWorld*> Worlds;
			for (const FWorldContext* Context : Contexts)
			{
				Worlds.Add(Context->World());
			}
			return Worlds;
		}

		/** The demo panel of a world's first local player, as the demo HUD put it on the screen; null when there is none. */
		UUserWidget* FindDemoPanel(const UWorld* World)
		{
			const APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
			const ACKSessionsDemoHUD* HUD = Controller != nullptr ? Cast<ACKSessionsDemoHUD>(Controller->GetHUD()) : nullptr;
			UUserWidget* Panel = HUD != nullptr ? HUD->GetPanel() : nullptr;
			return Panel != nullptr && Panel->IsInViewport() ? Panel : nullptr;
		}

		/** True when the widget and every parent up to the panel are visible. */
		bool IsDemoWidgetShown(const UUserWidget* Panel, const TCHAR* Name)
		{
			const UWidget* Widget = Panel != nullptr ? Panel->GetWidgetFromName(Name) : nullptr;
			if (Widget == nullptr)
			{
				return false;
			}
			for (const UWidget* Current = Widget; Current != nullptr; Current = Current->GetParent())
			{
				if (!Current->IsVisible())
				{
					return false;
				}
			}
			return true;
		}

		FString GetDemoText(const UUserWidget* Panel, const TCHAR* Name)
		{
			const UTextBlock* Text = Panel != nullptr ? Cast<UTextBlock>(Panel->GetWidgetFromName(Name)) : nullptr;
			return Text != nullptr && Text->IsVisible() ? Text->GetText().ToString() : FString();
		}

		/**
		 * Clicks a button of the panel the way a mouse click ends: the Slate button runs its On Clicked, which the UMG button
		 * forwards to its own On Clicked. Only a shown, enabled button is clicked. Returns false when it could not click.
		 */
		bool ClickDemoButton(const UUserWidget* Panel, const TCHAR* Name)
		{
			const UButton* Button = Panel != nullptr ? Cast<UButton>(Panel->GetWidgetFromName(Name)) : nullptr;
			if (Button == nullptr || !IsDemoWidgetShown(Panel, Name) || !Button->GetIsEnabled())
			{
				return false;
			}
			const TSharedPtr<SWidget> SlateWidget = Button->GetCachedWidget();
			if (!SlateWidget.IsValid() || SlateWidget->GetType() != FName(TEXT("SButton")))
			{
				return false;
			}
			StaticCastSharedPtr<SButton>(SlateWidget)->SimulateClick();
			return true;
		}

		FString GetDemoMapName(const UWorld* World)
		{
			return World != nullptr ? UWorld::RemovePIEPrefix(World->GetPackage()->GetName()) : FString();
		}

		UCKSessionsSubsystem* GetDemoSessions(const UWorld* World)
		{
			return World != nullptr ? UCKSessionsSubsystem::Get(World) : nullptr;
		}

		const UCKSessionsLobbyComponent* GetDemoLobby(const UWorld* World)
		{
			return World != nullptr ? UCKSessionsLobbyComponent::FindLobbyComponent(World) : nullptr;
		}

		/**
		 * A step that clicks a button of a window's panel (as soon as the button is shown and enabled), then waits for the
		 * result. GetWorld picks the window: its world changes when the window travels.
		 */
		FNetStep MakeClickStep(const FString& Name, TFunction<UWorld*()> GetWorld, const FString& Button, TFunction<bool()> IsDone, const int32 MaxTicks)
		{
			const TSharedRef<bool> bClicked = MakeShared<bool>(false);
			return { Name,
				[bClicked]() { *bClicked = false; },
				[bClicked, GetWorld, Button, IsDone]()
				{
					if (!*bClicked)
					{
						*bClicked = ClickDemoButton(FindDemoPanel(GetWorld()), *Button);
						return false;
					}
					return IsDone();
				},
				MaxTicks };
		}

		/**
		 * Frames IsDone must keep holding before ThenOnce runs its check: wrapped text settles one layout pass after it changes,
		 * the panel refreshes every 0.2 s, and a window that just travelled needs a few frames to draw its new map.
		 */
		constexpr int32 DemoSettleTicks = 15;

		/** Runs Check once when IsDone has held for a few frames, e.g. to save the windows' pictures of a milestone. */
		TFunction<bool()> ThenOnce(TFunction<bool()> IsDone, TFunction<void()> Check)
		{
			const TSharedRef<int32> HeldTicks = MakeShared<int32>(0);
			return [IsDone, Check, HeldTicks]()
			{
				if (!IsDone())
				{
					*HeldTicks = 0;
					return false;
				}
				if (++*HeldTicks < DemoSettleTicks)
				{
					return false;
				}
				Check();
				return true;
			};
		}

		/**
		 * The Fab reviewer's path through the old Quick start: open the match and lobby maps in the editor, then Play with three
		 * standalone windows. Every warning and error of the load, Map Check and the PIE start fails the test.
		 */
		bool RunMapsLoadScenario(FAutomationTestBase& Test)
		{
			const TSharedRef<FDemoLogCapture> Capture = MakeShared<FDemoLogCapture>();
			if (!LoadMapAndCheck(Test, *Capture, DemoMatchMap) || !LoadMapAndCheck(Test, *Capture, DemoLobbyMap))
			{
				return false;
			}

			FNetSessionSettings Settings;
			Settings.Host = ENetSessionHost::ListenServer;
			Settings.ClientConnect = ENetClientConnect::Manual;
			Settings.RemoteClients = 2;
			Settings.bEmulateOnNetDrivers = false;
			const TSharedRef<FNetTestSession> Session = MakeShared<FNetTestSession>(Settings);

			TArray<FNetStep> Steps;
			// Two seconds of play: begin play of every actor, the first frames of every window.
			Steps.Add({ TEXT("Three standalone windows play the lobby map for 2 s"),
				nullptr,
				[Counter = MakeShared<int32>(0)]() { return ++*Counter >= 60; },
				61 });
			Steps.Add({ TEXT("Every window shows the demo panel with Host game and Find games, no request for more windows"),
				nullptr,
				[]()
				{
					for (const UWorld* World : GetDemoPieWorlds())
					{
						const UUserWidget* Panel = FindDemoPanel(World);
						if (!IsDemoWidgetShown(Panel, WidgetNames::HostButton) || !IsDemoWidgetShown(Panel, WidgetNames::FindButton)
							|| IsDemoWidgetShown(Panel, WidgetNames::PlayWindowsButton))
						{
							return false;
						}
					}
					return GetDemoPieWorlds().Num() == 3;
				},
				30 });
			Steps.Add({ TEXT("No warning or error from the PIE start"),
				nullptr,
				[TestPtr = &Test, Capture]()
				{
					SaveWindowShots(*TestPtr, TEXT("MapsLoad_Lobby"));
					const int32 Problems = ReportLogProblems(*TestPtr, *Capture, TEXT("PIE start"));
					TestPtr->AddInfo(FString::Printf(TEXT("PIE start: %d warnings or errors in the log."), Problems));
					return true;
				},
				1 });

			if (!Session->Start(Test, DemoLobbyMap))
			{
				return false;
			}
			Test.AddCommand(new FNetScenarioCommand(Test, Session, MoveTemp(Steps)));
			return true;
		}

		/** Search with the Null subsystem always takes 5 s; joins, travels and the countdown take a few seconds under lag. */
		constexpr int32 DemoSearchTicks = 300;
		constexpr int32 DemoJoinTicks = 300;
		constexpr int32 DemoTravelTicks = 450;

		/**
		 * The whole demo with the mouse, in two standalone windows: Host game -> Find games -> Join -> Ready in both windows ->
		 * the countdown -> the match map with its player list -> Back to lobby -> Leave. Every action is a click on the panel.
		 */
		bool RunDemoClickScenario(FAutomationTestBase& Test)
		{
			FNetSessionSettings Settings;
			Settings.Host = ENetSessionHost::ListenServer;
			Settings.ClientConnect = ENetClientConnect::Manual;
			Settings.RemoteClients = 1;
			const TSharedRef<FNetTestSession> Session = MakeShared<FNetTestSession>(Settings);
			const TFunction<UWorld*()> HostWorld = [Session]() { return Session->GetServerWorld(); };
			const TFunction<UWorld*()> ClientWorld = [Session]() { return Session->GetClientWorld(0); };
			FAutomationTestBase* TestPtr = &Test;

			const TFunction<TArray<UWorld*>()> BothWorlds = [HostWorld, ClientWorld]() { return TArray<UWorld*>{ HostWorld(), ClientWorld() }; };
			const TFunction<bool(const TCHAR*, ECKSessionsLobbyPhase, int32)> AllOnMap = [BothWorlds](const TCHAR* Map, const ECKSessionsLobbyPhase Phase, const int32 Players)
			{
				for (const UWorld* World : BothWorlds())
				{
					const UCKSessionsLobbyComponent* Lobby = GetDemoLobby(World);
					if (GetDemoMapName(World) != Map || Lobby == nullptr || Lobby->GetPhase() != Phase || Lobby->GetPlayerCount() != Players
						|| FindDemoPanel(World) == nullptr)
					{
						return false;
					}
				}
				return true;
			};

			TArray<FNetStep> Steps;
			Steps.Add({ TEXT("Both windows show the panel: Host game and Find games, what to do next, no request for more windows"),
				nullptr,
				ThenOnce([BothWorlds]()
				{
					for (const UWorld* World : BothWorlds())
					{
						const UUserWidget* Panel = FindDemoPanel(World);
						if (!IsDemoWidgetShown(Panel, WidgetNames::HostButton) || !IsDemoWidgetShown(Panel, WidgetNames::FindButton)
							|| IsDemoWidgetShown(Panel, WidgetNames::PlayWindowsButton) || GetDemoText(Panel, WidgetNames::HintText).IsEmpty())
						{
							return false;
						}
					}
					return true;
				},
				[TestPtr]() { SaveWindowShots(*TestPtr, TEXT("1_Start")); }),
				90 });

			Steps.Add(MakeClickStep(TEXT("Window 1 clicks Host game: a listen server with a session, the panel shows the lobby"), HostWorld, WidgetNames::HostButton,
				[HostWorld]()
				{
					const UWorld* World = HostWorld();
					const UUserWidget* Panel = FindDemoPanel(World);
					return GetDemoSessions(World) != nullptr && GetDemoSessions(World)->GetState() == ECKSessionsState::InSession
						&& World->GetNetMode() == NM_ListenServer && IsDemoWidgetShown(Panel, WidgetNames::ReadyButton)
						&& IsDemoWidgetShown(Panel, WidgetNames::LeaveButton) && GetDemoText(Panel, WidgetNames::SessionText).Contains(TEXT("host"));
				},
				DemoJoinTicks));

			Steps.Add(MakeClickStep(TEXT("Window 2 clicks Find games: the host's game is listed with a Join button"), ClientWorld, WidgetNames::FindButton,
				ThenOnce([ClientWorld]() { return IsDemoWidgetShown(FindDemoPanel(ClientWorld()), *FString::Printf(TEXT("%s0"), WidgetNames::JoinButtonPrefix)); },
					[TestPtr]() { SaveWindowShots(*TestPtr, TEXT("2_Found")); }),
				DemoSearchTicks));

			Steps.Add(MakeClickStep(TEXT("Window 2 clicks Join: both windows list two players in the lobby, Ready is clickable"), ClientWorld,
				FString::Printf(TEXT("%s0"), WidgetNames::JoinButtonPrefix),
				ThenOnce([AllOnMap, BothWorlds]()
				{
					if (!AllOnMap(DemoLobbyMap, ECKSessionsLobbyPhase::Waiting, 2))
					{
						return false;
					}
					for (const UWorld* World : BothWorlds())
					{
						const UUserWidget* Panel = FindDemoPanel(World);
						const UWidget* Ready = Panel->GetWidgetFromName(WidgetNames::ReadyButton);
						if (Ready == nullptr || !Ready->GetIsEnabled() || !GetDemoText(Panel, WidgetNames::PlayersText).Contains(TEXT("(2)")))
						{
							return false;
						}
					}
					return true;
				},
				[TestPtr]() { SaveWindowShots(*TestPtr, TEXT("3_Lobby")); }),
				DemoJoinTicks));

			// Both clicks in one step: the countdown (3 s) may start right after the second one.
			const TSharedRef<int32> ReadyClicks = MakeShared<int32>(0);
			Steps.Add({ TEXT("Both windows click Ready: the lobby counts down"),
				[ReadyClicks]() { *ReadyClicks = 0; },
				[ReadyClicks, HostWorld, ClientWorld, BothWorlds, TestPtr]()
				{
					if (*ReadyClicks == 0)
					{
						*ReadyClicks += ClickDemoButton(FindDemoPanel(HostWorld()), WidgetNames::ReadyButton) ? 1 : 0;
						return false;
					}
					if (*ReadyClicks == 1)
					{
						*ReadyClicks += ClickDemoButton(FindDemoPanel(ClientWorld()), WidgetNames::ReadyButton) ? 1 : 0;
						return false;
					}
					for (const UWorld* World : BothWorlds())
					{
						const UCKSessionsLobbyComponent* Lobby = GetDemoLobby(World);
						if (Lobby == nullptr || Lobby->GetPhase() != ECKSessionsLobbyPhase::Countdown
							|| !GetDemoText(FindDemoPanel(World), WidgetNames::LobbyText).Contains(TEXT("starts in")))
						{
							return false;
						}
					}
					SaveWindowShots(*TestPtr, TEXT("4_Countdown"));
					return true;
				},
				90 });

			Steps.Add({ TEXT("The lobby travels both windows to L_SessionsMatch; the panel lists the players, the host gets Back to lobby"),
				nullptr,
				ThenOnce([AllOnMap, HostWorld, ClientWorld]()
				{
					if (!AllOnMap(DemoMatchMap, ECKSessionsLobbyPhase::InMatch, 2))
					{
						return false;
					}
					const UUserWidget* HostPanel = FindDemoPanel(HostWorld());
					const UUserWidget* ClientPanel = FindDemoPanel(ClientWorld());
					return IsDemoWidgetShown(HostPanel, WidgetNames::BackToLobbyButton) && !IsDemoWidgetShown(ClientPanel, WidgetNames::BackToLobbyButton)
						&& IsDemoWidgetShown(ClientPanel, WidgetNames::LeaveButton) && !IsDemoWidgetShown(HostPanel, WidgetNames::ReadyButton)
						&& GetDemoText(HostPanel, WidgetNames::PlayersText).Contains(TEXT("Players in the match (2)"))
						&& GetDemoText(ClientPanel, WidgetNames::PlayersText).Contains(TEXT("Players in the match (2)"));
				},
				[TestPtr]() { SaveWindowShots(*TestPtr, TEXT("5_Match")); }),
				DemoTravelTicks });

			Steps.Add(MakeClickStep(TEXT("Window 1 clicks Back to lobby: both windows are back in the lobby, nobody ready"), HostWorld, WidgetNames::BackToLobbyButton,
				ThenOnce([AllOnMap, BothWorlds]()
				{
					if (!AllOnMap(DemoLobbyMap, ECKSessionsLobbyPhase::Waiting, 2))
					{
						return false;
					}
					for (const UWorld* World : BothWorlds())
					{
						if (!IsDemoWidgetShown(FindDemoPanel(World), WidgetNames::ReadyButton) || GetDemoLobby(World)->GetReadyPlayerCount() != 0)
						{
							return false;
						}
					}
					return !GetDemoSessions(BothWorlds()[0])->IsMatchInProgress();
				},
				[TestPtr]() { SaveWindowShots(*TestPtr, TEXT("6_BackInLobby")); }),
				DemoTravelTicks));

			Steps.Add(MakeClickStep(TEXT("Window 2 clicks Leave: it is alone on L_SessionsDemo with Host game and Find games, the host's lobby has one player"),
				ClientWorld, WidgetNames::LeaveButton,
				[HostWorld, ClientWorld]()
				{
					const UWorld* World = ClientWorld();
					const UCKSessionsLobbyComponent* HostLobby = GetDemoLobby(HostWorld());
					return World != nullptr && World->GetNetMode() == NM_Standalone && GetDemoMapName(World) == DemoLobbyMap
						&& GetDemoSessions(World)->GetState() == ECKSessionsState::Idle && IsDemoWidgetShown(FindDemoPanel(World), WidgetNames::HostButton)
						&& HostLobby != nullptr && HostLobby->GetPlayerCount() == 1;
				},
				DemoJoinTicks));

			if (!Session->Start(Test, DemoLobbyMap))
			{
				return false;
			}
			Test.AddInfo(FString::Printf(TEXT("Scenario: %s, every action is a click on the demo panel"), *Session->Describe()));
			Test.AddCommand(new FNetScenarioCommand(Test, Session, MoveTemp(Steps)));
			return true;
		}

		/**
		 * Play In Editor with its default settings, one window: the panel says the demo needs two windows, and its Play with 2
		 * windows button restarts Play In Editor with two standalone windows, each with the panel.
		 */
		bool RunDemoRestartScenario(FAutomationTestBase& Test)
		{
			FNetSessionSettings Settings;
			Settings.Host = ENetSessionHost::ListenServer;
			Settings.ClientConnect = ENetClientConnect::Manual;
			Settings.RemoteClients = 0;
			Settings.bEmulateOnNetDrivers = false;
			const TSharedRef<FNetTestSession> Session = MakeShared<FNetTestSession>(Settings);
			FAutomationTestBase* TestPtr = &Test;

			TArray<FNetStep> Steps;
			Steps.Add({ TEXT("One window: the panel asks for two windows and shows Play with 2 windows"),
				nullptr,
				ThenOnce([]()
				{
					const TArray<UWorld*> Worlds = GetDemoPieWorlds();
					const UUserWidget* Panel = Worlds.Num() == 1 ? FindDemoPanel(Worlds[0]) : nullptr;
					return IsDemoWidgetShown(Panel, WidgetNames::PlayWindowsButton) && GetDemoText(Panel, WidgetNames::HintText).Contains(TEXT("2 game windows"));
				},
				[TestPtr]() { SaveWindowShots(*TestPtr, TEXT("0_OneWindow")); }),
				90 });

			// Ending and starting Play In Editor takes a few seconds; 900 ticks are 30 s at the session's frame cap.
			Steps.Add(MakeClickStep(TEXT("Click Play with 2 windows: Play In Editor restarts with two standalone windows, each with the panel"),
				[]() { const TArray<UWorld*> Worlds = GetDemoPieWorlds(); return Worlds.Num() == 1 ? Worlds[0] : nullptr; },
				WidgetNames::PlayWindowsButton,
				ThenOnce([]()
				{
					const TArray<UWorld*> Worlds = GetDemoPieWorlds();
					if (Worlds.Num() != 2)
					{
						return false;
					}
					for (const UWorld* World : Worlds)
					{
						const UUserWidget* Panel = FindDemoPanel(World);
						if (World->GetNetMode() != NM_Standalone || !IsDemoWidgetShown(Panel, WidgetNames::HostButton)
							|| IsDemoWidgetShown(Panel, WidgetNames::PlayWindowsButton))
						{
							return false;
						}
					}
					return true;
				},
				[TestPtr]() { SaveWindowShots(*TestPtr, TEXT("0_TwoWindows")); }),
				900));

			if (!Session->Start(Test, DemoLobbyMap))
			{
				return false;
			}
			Test.AddCommand(new FNetScenarioCommand(Test, Session, MoveTemp(Steps)));
			return true;
		}

		/** True when a friend row of the panel is shown. */
		bool IsAnyFriendRowShown(const UUserWidget* Panel)
		{
			for (int32 Index = 0; Index < WidgetNames::MaxFriendButtons; ++Index)
			{
				if (IsDemoWidgetShown(Panel, *FString::Printf(TEXT("%s%d"), WidgetNames::FriendButtonPrefix, Index)))
				{
					return true;
				}
			}
			return false;
		}

		/** The panel's friends line says that friends need Steam or EOS and why: no empty list on Null. */
		bool ExplainsNoFriendsOnNull(const UUserWidget* Panel)
		{
			const FString Line = GetDemoText(Panel, WidgetNames::FriendsText);
			return Line.Contains(TEXT("Steam or EOS")) && Line.Contains(TEXT("no friends list")) && !IsAnyFriendRowShown(Panel);
		}

		/**
		 * Friends and invites with the mouse on Null: Friends in the menu and Invite friends in a hosted game both answer with
		 * the reason (no friends list on Null) instead of an empty list.
		 */
		bool RunDemoFriendsScenario(FAutomationTestBase& Test)
		{
			FNetSessionSettings Settings;
			Settings.Host = ENetSessionHost::ListenServer;
			Settings.ClientConnect = ENetClientConnect::Manual;
			Settings.RemoteClients = 1;
			const TSharedRef<FNetTestSession> Session = MakeShared<FNetTestSession>(Settings);
			const TFunction<UWorld*()> HostWorld = [Session]() { return Session->GetServerWorld(); };
			FAutomationTestBase* TestPtr = &Test;

			TArray<FNetStep> Steps;
			Steps.Add({ TEXT("The menu shows Friends; Invite friends needs a session and is hidden"),
				nullptr,
				[HostWorld]()
				{
					const UUserWidget* Panel = FindDemoPanel(HostWorld());
					return IsDemoWidgetShown(Panel, WidgetNames::FriendsButton) && !IsDemoWidgetShown(Panel, WidgetNames::InviteButton)
						&& GetDemoText(Panel, WidgetNames::FriendsText).IsEmpty();
				},
				90 });

			Steps.Add({ TEXT("The demo joins accepted invites by itself (Join At Once); the plugin's default stays Event Only"),
				[TestPtr, HostWorld]()
				{
					TestPtr->TestEqual(TEXT("Project setting: Event Only"), UEnum::GetValueAsString(UCKSessionsSettings::Get().GetInviteAcceptPolicy()),
						UEnum::GetValueAsString(ECKSessionsInvitePolicy::EventOnly));
					TestPtr->TestEqual(TEXT("The panel switched its subsystem to Join At Once"), UEnum::GetValueAsString(GetDemoSessions(HostWorld())->GetInviteAcceptPolicy()),
						UEnum::GetValueAsString(ECKSessionsInvitePolicy::JoinAtOnce));
				},
				[]() { return true; },
				2 });

			Steps.Add(MakeClickStep(TEXT("Click Friends: the panel explains that friends need Steam or EOS, no empty list"), HostWorld, WidgetNames::FriendsButton,
				ThenOnce([HostWorld]() { return ExplainsNoFriendsOnNull(FindDemoPanel(HostWorld())); },
					[TestPtr]() { SaveWindowShots(*TestPtr, TEXT("Friends_Menu")); }),
				60));

			Steps.Add(MakeClickStep(TEXT("Click Host game: in the game, Invite friends replaces Friends and the menu's answer goes away"), HostWorld, WidgetNames::HostButton,
				[HostWorld]()
				{
					const UWorld* World = HostWorld();
					const UUserWidget* Panel = FindDemoPanel(World);
					return GetDemoSessions(World) != nullptr && GetDemoSessions(World)->GetState() == ECKSessionsState::InSession
						&& IsDemoWidgetShown(Panel, WidgetNames::InviteButton) && !IsDemoWidgetShown(Panel, WidgetNames::FriendsButton)
						&& GetDemoText(Panel, WidgetNames::FriendsText).IsEmpty();
				},
				DemoJoinTicks));

			Steps.Add(MakeClickStep(TEXT("Click Invite friends: no overlay on Null, the panel explains why instead of an empty list"), HostWorld, WidgetNames::InviteButton,
				ThenOnce([HostWorld]()
				{
					const UWorld* World = HostWorld();
					return ExplainsNoFriendsOnNull(FindDemoPanel(World)) && GetDemoSessions(World)->GetState() == ECKSessionsState::InSession;
				},
				[TestPtr]() { SaveWindowShots(*TestPtr, TEXT("Friends_Invite")); }),
				60));

			if (!Session->Start(Test, DemoLobbyMap))
			{
				return false;
			}
			Test.AddCommand(new FNetScenarioCommand(Test, Session, MoveTemp(Steps)));
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsDemoFriendsTest, "CoopKit.Sessions.Demo.Friends",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsDemoFriendsTest::RunTest(const FString& Parameters)
{
	return CKSessions::Tests::RunDemoFriendsScenario(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsDemoMapsLoadTest, "CoopKit.Sessions.Demo.MapsLoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsDemoMapsLoadTest::RunTest(const FString& Parameters)
{
	return CKSessions::Tests::RunMapsLoadScenario(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsDemoListenServerTest, "CoopKit.Sessions.Demo.ListenServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsDemoListenServerTest::RunTest(const FString& Parameters)
{
	return CKSessions::Tests::RunDemoClickScenario(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsDemoRestartPlayTest, "CoopKit.Sessions.Demo.RestartPlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsDemoRestartPlayTest::RunTest(const FString& Parameters)
{
	return CKSessions::Tests::RunDemoRestartScenario(*this);
}

#endif // WITH_DEV_AUTOMATION_TESTS
