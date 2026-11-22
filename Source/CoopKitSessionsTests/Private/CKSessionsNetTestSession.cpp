// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsNetTestSession.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "UObject/Package.h"

namespace CKSessions::Tests
{
	namespace
	{
		TAutoConsoleVariable<int32> CVarBlockNetwork(
			TEXT("ck.sessions.tests.BlockNetwork"),
			0,
			TEXT("Test only. 1 drops every packet after the players connect, so any replication test must fail. Proves a test detects broken replication without touching product code."),
			ECVF_Cheat);

		/**
		 * Players of the session on the server: an overview map's scenarios add bots as extra local players of the host
		 * (no connection of their own), so they are not counted against the expected host and remote clients.
		 */
		int32 CountSessionPlayers(const AGameStateBase& GameState)
		{
			int32 Count = 0;
			for (const APlayerState* Player : GameState.PlayerArray)
			{
				const APlayerController* Controller = Player != nullptr ? Cast<APlayerController>(Player->GetOwningController()) : nullptr;
				const ULocalPlayer* Local = Controller != nullptr ? Controller->GetLocalPlayer() : nullptr;
				const UGameInstance* GameInstance = Local != nullptr ? Local->GetGameInstance() : nullptr;
				const bool bBot = GameInstance != nullptr && GameInstance->GetLocalPlayers().IndexOfByKey(Local) > 0;
				Count += Player != nullptr && !bBot ? 1 : 0;
			}
			return Count;
		}

		IConsoleVariable* FindMaxFpsVariable()
		{
			return IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"));
		}

		bool IsServerNetMode(const UWorld* World)
		{
			const ENetMode NetMode = World->GetNetMode();
			return NetMode == NM_DedicatedServer || NetMode == NM_ListenServer;
		}

		/** PIE world contexts that have a world, ordered by PIE instance (0 is the first instance PIE created). */
		TArray<const FWorldContext*> GetPieContexts()
		{
			TArray<const FWorldContext*> Contexts;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType == EWorldType::PIE && Context.World() != nullptr)
				{
					Contexts.Add(&Context);
				}
			}
			Contexts.Sort([](const FWorldContext& Left, const FWorldContext& Right) { return Left.PIEInstance < Right.PIEInstance; });
			return Contexts;
		}
	}

	FNetTestSession::FNetTestSession(const FNetSessionSettings& InSettings)
		: Settings(InSettings)
	{
	}

	bool FNetTestSession::Start(FAutomationTestBase& Test, const FString& MapPackageName)
	{
		if (GEditor == nullptr)
		{
			Test.AddError(TEXT("Replication tests need the editor (EditorContext)."));
			return false;
		}

		if (GEditor->IsPlaySessionInProgress())
		{
			Test.AddError(TEXT("A PIE session is already running; replication tests start their own."));
			return false;
		}

		if (UEditorLoadingAndSavingUtils::LoadMap(MapPackageName) == nullptr)
		{
			Test.AddError(FString::Printf(TEXT("Map %s did not load."), *MapPackageName));
			return false;
		}

		// A private copy keeps the user's Play settings untouched.
		ULevelEditorPlaySettings* PlaySettings = DuplicateObject(GetDefault<ULevelEditorPlaySettings>(), GetTransientPackage());
		const bool bDedicated = Settings.Host == ENetSessionHost::DedicatedServer;
		if (Settings.ClientConnect == ENetClientConnect::Manual)
		{
			// Standalone instances do not connect anywhere by themselves; an extra in-process server stands in for the
			// dedicated server. For a listen server, instance 0 is the future host.
			PlaySettings->SetPlayNetMode(EPlayNetMode::PIE_Standalone);
			PlaySettings->bLaunchSeparateServer = bDedicated;
		}
		else
		{
			PlaySettings->SetPlayNetMode(bDedicated ? EPlayNetMode::PIE_Client : EPlayNetMode::PIE_ListenServer);
			PlaySettings->bLaunchSeparateServer = false;
		}
		PlaySettings->SetPlayNumberOfClients(bDedicated ? Settings.RemoteClients : Settings.RemoteClients + 1);
		PlaySettings->SetRunUnderOneProcess(true);

		// The user's own PIE network emulation would add to the test's emulation, so the copy starts without it.
		PlaySettings->NetworkEmulationSettings.bIsNetworkEmulationEnabled = false;

		if (Settings.ConfigurePlaySettings)
		{
			Settings.ConfigurePlaySettings(*PlaySettings);
		}

		FRequestPlaySessionParams Params;
		Params.WorldType = EPlaySessionWorldType::PlayInEditor;
		Params.EditorPlaySettings = PlaySettings;

		if (IConsoleVariable* MaxFps = FindMaxFpsVariable())
		{
			SavedMaxFps = MaxFps->GetFloat();
			MaxFps->Set(Settings.MaxFps, ECVF_SetByCode);
			bFpsCapApplied = true;
		}

		GEditor->RequestPlaySession(Params);
		return true;
	}

	UWorld* FNetTestSession::GetWorldOfHandle(const FName ContextHandle) const
	{
		const FWorldContext* Context = ContextHandle.IsNone() ? nullptr : GEngine->GetWorldContextFromHandle(ContextHandle);
		return Context != nullptr ? Context->World() : nullptr;
	}

	UWorld* FNetTestSession::GetServerWorld() const
	{
		if (Settings.ClientConnect == ENetClientConnect::Manual)
		{
			return GetWorldOfHandle(HostContextHandle);
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (Context.WorldType == EWorldType::PIE && World != nullptr && IsServerNetMode(World))
			{
				return World;
			}
		}
		return nullptr;
	}

	TArray<UWorld*> FNetTestSession::GetClientWorlds() const
	{
		TArray<UWorld*> Worlds;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (Context.WorldType == EWorldType::PIE && World != nullptr && World->GetNetMode() == NM_Client)
			{
				Worlds.Add(World);
			}
		}
		return Worlds;
	}

	UWorld* FNetTestSession::GetClientWorld(const int32 ClientIndex) const
	{
		return ClientContextHandles.IsValidIndex(ClientIndex) ? GetWorldOfHandle(ClientContextHandles[ClientIndex]) : nullptr;
	}

	UGameInstance* FNetTestSession::GetHostGameInstance() const
	{
		const FWorldContext* Context = HostContextHandle.IsNone() ? nullptr : GEngine->GetWorldContextFromHandle(HostContextHandle);
		return Context != nullptr ? Context->OwningGameInstance.Get() : nullptr;
	}

	UGameInstance* FNetTestSession::GetClientGameInstance(const int32 ClientIndex) const
	{
		const FWorldContext* Context = ClientContextHandles.IsValidIndex(ClientIndex) ? GEngine->GetWorldContextFromHandle(ClientContextHandles[ClientIndex]) : nullptr;
		return Context != nullptr ? Context->OwningGameInstance.Get() : nullptr;
	}

	int32 FNetTestSession::GetExpectedPlayerCount() const
	{
		return Settings.RemoteClients + LateJoinClients + (Settings.Host == ENetSessionHost::ListenServer ? 1 : 0);
	}

	bool FNetTestSession::AreAllPlayersConnected() const
	{
		const UWorld* ServerWorld = GetServerWorld();
		const AGameStateBase* ServerGameState = ServerWorld != nullptr ? ServerWorld->GetGameState() : nullptr;
		if (ServerGameState == nullptr || CountSessionPlayers(*ServerGameState) != GetExpectedPlayerCount())
		{
			return false;
		}

		const TArray<UWorld*> ClientWorlds = GetClientWorlds();
		if (ClientWorlds.Num() != Settings.RemoteClients + LateJoinClients)
		{
			return false;
		}

		for (const UWorld* ClientWorld : ClientWorlds)
		{
			const APlayerController* Controller = ClientWorld->GetFirstPlayerController();
			const AGameStateBase* ClientGameState = ClientWorld->GetGameState();
			if (Controller == nullptr || Controller->PlayerState == nullptr || ClientGameState == nullptr
				|| ClientGameState->PlayerArray.Num() != ServerGameState->PlayerArray.Num())
			{
				return false;
			}
		}
		return true;
	}

	bool FNetTestSession::AreAllInstancesReady() const
	{
		const TArray<const FWorldContext*> Contexts = GetPieContexts();
		if (Contexts.Num() != Settings.RemoteClients + 1)
		{
			return false;
		}
		for (const FWorldContext* Context : Contexts)
		{
			if (!Context->World()->HasBegunPlay() || Context->World()->GetGameState() == nullptr)
			{
				return false;
			}
		}
		return true;
	}

	bool FNetTestSession::CaptureInstances(FAutomationTestBase& Test)
	{
		const TArray<const FWorldContext*> Contexts = GetPieContexts();
		if (Contexts.Num() != Settings.RemoteClients + 1)
		{
			Test.AddError(FString::Printf(TEXT("Expected %d PIE instances, found %d."), Settings.RemoteClients + 1, Contexts.Num()));
			return false;
		}

		const ENetMode HostMode = Settings.Host == ENetSessionHost::DedicatedServer ? NM_DedicatedServer : NM_Standalone;
		if (Contexts[0]->World()->GetNetMode() != HostMode)
		{
			Test.AddError(FString::Printf(TEXT("PIE instance 0 net mode is %d, expected %d."), static_cast<int32>(Contexts[0]->World()->GetNetMode()), static_cast<int32>(HostMode)));
			return false;
		}
		HostContextHandle = Contexts[0]->ContextHandle;
		ClientContextHandles.Reset();
		for (int32 Index = 1; Index < Contexts.Num(); ++Index)
		{
			if (Contexts[Index]->World()->GetNetMode() != NM_Standalone)
			{
				Test.AddError(FString::Printf(TEXT("PIE instance %d connected by itself (net mode %d); manual mode needs standalone clients."),
					Index, static_cast<int32>(Contexts[Index]->World()->GetNetMode())));
				return false;
			}
			ClientContextHandles.Add(Contexts[Index]->ContextHandle);
		}
		return true;
	}

	int32 FNetTestSession::EmulateNewDrivers(FAutomationTestBase& Test) const
	{
		int32 WorldsWithoutDriver = 0;
#if DO_ENABLE_NET_TEST
		const bool bBlockNetwork = CVarBlockNetwork.GetValueOnGameThread() != 0;
		if (!Settings.bEmulateOnNetDrivers && !bBlockNetwork)
		{
			// Emulation comes from the Play settings (ConfigurePlaySettings); the net drivers already carry it.
			return 0;
		}

		FPacketSimulationSettings Simulation;
		Simulation.PktLag = Settings.bEmulateOnNetDrivers ? Settings.PacketLagMs : 0;
		Simulation.PktLoss = Settings.bEmulateOnNetDrivers ? Settings.PacketLossPercent : 0;
		if (bBlockNetwork)
		{
			Simulation.PktLoss = 100;
			if (!bBlockNoticeShown)
			{
				bBlockNoticeShown = true;
				Test.AddInfo(TEXT("ck.sessions.tests.BlockNetwork is set: every packet is dropped, this run must fail."));
			}
		}

		EmulatedDrivers.RemoveAll([](const TWeakObjectPtr<UNetDriver>& Driver) { return !Driver.IsValid(); });
		for (const FWorldContext* Context : GetPieContexts())
		{
			UNetDriver* NetDriver = Context->World()->GetNetDriver();
			if (NetDriver == nullptr)
			{
				++WorldsWithoutDriver;
				continue;
			}
			if (!EmulatedDrivers.Contains(NetDriver))
			{
				// Connections created later copy the driver's settings, so a server driver covers its future clients.
				NetDriver->SetPacketSimulationSettings(Simulation);
				EmulatedDrivers.Add(NetDriver);
			}
		}
#endif
		return WorldsWithoutDriver;
	}

	bool FNetTestSession::ApplyNetworkEmulation(FAutomationTestBase& Test) const
	{
#if DO_ENABLE_NET_TEST
		const int32 WorldsWithoutDriver = EmulateNewDrivers(Test);
		if (Settings.ClientConnect == ENetClientConnect::Automatic && WorldsWithoutDriver > 0)
		{
			Test.AddError(TEXT("A PIE world has no net driver; network emulation is not applied."));
			return false;
		}
		return true;
#else
		Test.AddError(TEXT("Network emulation needs DO_ENABLE_NET_TEST (any non-Shipping editor build)."));
		return false;
#endif
	}

	void FNetTestSession::RefreshNetworkEmulation(FAutomationTestBase& Test) const
	{
		EmulateNewDrivers(Test);
	}

	void FNetTestSession::RequestLateJoin()
	{
		if (GEditor != nullptr && GEditor->IsPlaySessionInProgress())
		{
			GEditor->RequestLateJoin();
			++LateJoinClients;
		}
	}

	void FNetTestSession::Stop()
	{
		if (GEditor != nullptr && GEditor->IsPlaySessionInProgress())
		{
			GEditor->RequestEndPlayMap();
		}

		if (bFpsCapApplied)
		{
			if (IConsoleVariable* MaxFps = FindMaxFpsVariable())
			{
				MaxFps->Set(SavedMaxFps, ECVF_SetByCode);
			}
			bFpsCapApplied = false;
		}
	}

	bool FNetTestSession::IsStopped() const
	{
		return GEditor == nullptr || !GEditor->IsPlaySessionInProgress();
	}

	FString FNetTestSession::Describe() const
	{
		const TCHAR* HostName = Settings.Host == ENetSessionHost::DedicatedServer ? TEXT("dedicated server") : TEXT("listen server");
		const TCHAR* Connect = Settings.ClientConnect == ENetClientConnect::Manual ? TEXT(", clients connect themselves") : TEXT("");
		if (!Settings.bEmulateOnNetDrivers)
		{
			return FString::Printf(TEXT("%s + %d clients%s, emulation from Play settings, %.0f fps cap"), HostName, Settings.RemoteClients, Connect, Settings.MaxFps);
		}
		return FString::Printf(TEXT("%s + %d clients%s, lag %d ms, loss %d%%, %.0f fps cap"),
			HostName, Settings.RemoteClients, Connect, Settings.PacketLagMs, Settings.PacketLossPercent, Settings.MaxFps);
	}

	FNetScenarioCommand::FNetScenarioCommand(FAutomationTestBase& InTest, TSharedRef<FNetTestSession> InSession, TArray<FNetStep> InSteps)
		: Test(InTest)
		, Session(MoveTemp(InSession))
		, Steps(MoveTemp(InSteps))
		, PhaseStartSeconds(FPlatformTime::Seconds())
	{
	}

	void FNetScenarioCommand::Fail(const FString& Message)
	{
		Test.AddError(Message);
		Session->Stop();
		Phase = EPhase::Stopping;
		PhaseStartSeconds = FPlatformTime::Seconds();
	}

	bool FNetScenarioCommand::Update()
	{
		const double Now = FPlatformTime::Seconds();
		const bool bManual = Session->GetSettings().ClientConnect == ENetClientConnect::Manual;

		switch (Phase)
		{
		case EPhase::Connecting:
			if (bManual ? Session->AreAllInstancesReady() : Session->AreAllPlayersConnected())
			{
				if (bManual)
				{
					if (!Session->CaptureInstances(Test))
					{
						Fail(TEXT("PIE instances are not in the expected net modes."));
						break;
					}
					Test.AddInfo(FString::Printf(TEXT("Instances ready in %.1f s: %s"), Now - PhaseStartSeconds, *Session->Describe()));
				}
				else
				{
					Test.AddInfo(FString::Printf(TEXT("Connected in %.1f s: %s"), Now - PhaseStartSeconds, *Session->Describe()));

					// The point of the dedicated server run is a server world without a local player.
					const ENetMode ExpectedMode = Session->GetSettings().Host == ENetSessionHost::DedicatedServer ? NM_DedicatedServer : NM_ListenServer;
					if (Session->GetServerWorld()->GetNetMode() != ExpectedMode)
					{
						Fail(FString::Printf(TEXT("Server world net mode is %d, expected %d."), static_cast<int32>(Session->GetServerWorld()->GetNetMode()), static_cast<int32>(ExpectedMode)));
						break;
					}
				}
				if (!Session->ApplyNetworkEmulation(Test))
				{
					Fail(TEXT("Network emulation failed."));
					break;
				}
				Phase = EPhase::Steps;
			}
			else if (Now - PhaseStartSeconds > Session->GetSettings().ConnectTimeoutSeconds)
			{
				Fail(FString::Printf(TEXT("Players did not connect within %.0f s (%s)."), Session->GetSettings().ConnectTimeoutSeconds, *Session->Describe()));
			}
			break;

		case EPhase::Steps:
		{
			// Joins, travels, reconnects and late joins create net drivers; they get the same emulation.
			Session->RefreshNetworkEmulation(Test);

			if (!Steps.IsValidIndex(StepIndex))
			{
				Session->Stop();
				Phase = EPhase::Stopping;
				PhaseStartSeconds = Now;
				break;
			}

			FNetStep& Step = Steps[StepIndex];
			if (!bStepEntered)
			{
				bStepEntered = true;
				StepTicks = 0;
				PhaseStartSeconds = Now;
				if (Step.Enter)
				{
					Step.Enter();
				}
			}

			++StepTicks;
			if (Step.IsDone())
			{
				Test.AddInfo(FString::Printf(TEXT("%s: %d ticks (%.0f ms)"), *Step.Name, StepTicks, (Now - PhaseStartSeconds) * 1000.0));
				++StepIndex;
				bStepEntered = false;
			}
			else if (StepTicks >= Step.MaxTicks)
			{
				Fail(FString::Printf(TEXT("%s: not reached within %d ticks (%.0f ms)."), *Step.Name, Step.MaxTicks, (Now - PhaseStartSeconds) * 1000.0));
			}
			break;
		}

		case EPhase::Stopping:
			if (Session->IsStopped())
			{
				Phase = EPhase::Done;
			}
			else if (Now - PhaseStartSeconds > 30.0)
			{
				Test.AddError(TEXT("PIE did not stop within 30 s."));
				Phase = EPhase::Done;
			}
			break;

		case EPhase::Done:
		default:
			break;
		}

		return Phase == EPhase::Done;
	}
}
