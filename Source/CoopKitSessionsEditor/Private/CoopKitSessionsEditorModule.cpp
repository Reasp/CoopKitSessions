// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "Containers/Ticker.h"
#include "Framework/Application/SlateApplication.h"
#include "Editor.h"
#include "Modules/ModuleManager.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "CKSessionsCompat.h"
#include "CKSessionsDemoHUD.h"
#include "CKSessionsHub.h"
#include "CKSessionsSetup.h"
#include "Framework/Docking/TabManager.h"
#include "SCKSessionsSetup.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

DEFINE_LOG_CATEGORY(LogCKSessionsEditor);

namespace CKSessionsEditor::DemoPlay
{
	namespace
	{
		/** Seconds to wait for the old session to end and the new one to start before giving up. */
		constexpr double RestartTimeoutSeconds = 30.0;

		/**
		 * Restarts Play In Editor with Windows standalone game windows in one process, keeping everything else of the running
		 * session's request (map, viewport or new window). The Play settings are a private copy: the user's Editor Preferences
		 * do not change. Called by the demo panel's Play with N windows button through ACKSessionsDemoHUD.
		 */
		bool RestartPlayInEditor(const int32 Windows)
		{
			if (GEditor == nullptr || !GEditor->IsPlayingSessionInEditor() || Windows < 1)
			{
				return false;
			}

			const TOptional<FPlayInEditorSessionInfo> SessionInfo = GEditor->GetPlayInEditorSessionInfo();
			FRequestPlaySessionParams Params = SessionInfo.IsSet() ? SessionInfo->OriginalRequestParams : FRequestPlaySessionParams();
			const ULevelEditorPlaySettings* Source = Params.EditorPlaySettings != nullptr ? Params.EditorPlaySettings.Get() : GetDefault<ULevelEditorPlaySettings>();

			// Held strongly until the new session started: the request keeps only a plain pointer to the copy.
			TStrongObjectPtr<ULevelEditorPlaySettings> Settings(DuplicateObject(Source, GetTransientPackage()));
			Settings->SetPlayNumberOfClients(Windows);
			Settings->SetPlayNetMode(EPlayNetMode::PIE_Standalone);
			Settings->SetRunUnderOneProcess(true);
			Settings->bLaunchSeparateServer = false;
			Params.EditorPlaySettings = Settings.Get();

			GEditor->RequestEndPlayMap();

			const double StartSeconds = FPlatformTime::Seconds();
			bool bRequested = false;
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Params, Settings, StartSeconds, bRequested](float) mutable
			{
				if (GEditor == nullptr || FPlatformTime::Seconds() - StartSeconds > RestartTimeoutSeconds)
				{
					return false;
				}
				if (!bRequested)
				{
					if (GEditor->IsPlaySessionInProgress())
					{
						return true;
					}
					GEditor->RequestPlaySession(Params);
					bRequested = true;
					return true;
				}
				// Keep the settings copy alive until the new session runs.
				return !GEditor->IsPlayingSessionInEditor();
			}));
			return true;
		}
	}
}

namespace CKSessionsEditor::Setup
{
	/** Raises the setup window's tab: Tools > CoopKit Sessions Setup. Every copy of the plugin has its own, whoever owns the hub. */
	void RegisterTab()
	{
		if (!FSlateApplication::IsInitialized() || FGlobalTabmanager::Get()->HasTabSpawner(CKSessions::Setup::TabId))
		{
			return;
		}
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(CKSessions::Setup::TabId, FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&)
		{
			return SNew(SDockTab).TabRole(ETabRole::NomadTab)[SNew(SCKSessionsSetup)];
		}))
			.SetDisplayName(NSLOCTEXT("CKSessionsSetup", "TabTitle", "CoopKit Sessions Setup"))
			.SetTooltipText(NSLOCTEXT("CKSessionsSetup", "TabTooltip", "Checks the Steam and EOS setup of the project and applies the recommended settings."))
			.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());
	}

	void UnregisterTab()
	{
		if (FSlateApplication::IsInitialized())
		{
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(CKSessions::Setup::TabId);
		}
	}
}

/**
 * Editor module of CoopKitSessions. Factories, asset definitions and the card register themselves as classes; the module only
 * joins the CoopKit Hub election once every plugin module is loaded: the plugin with the highest hub contract version raises
 * the shared CoopKitHub tab (doc-57). The module loads in the Default phase: OnPostEngineInit fires before PostEngineInit
 * modules, so a later phase would miss the election. It also binds the demo's Play with N windows button (ACKSessionsDemoHUD)
 * and raises the Steam and EOS setup window (SCKSessionsSetup).
 */
class FCoopKitSessionsEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		ACKSessionsDemoHUD::OnRestartPlayInEditor().BindStatic(&CKSessionsEditor::DemoPlay::RestartPlayInEditor);
		PostEngineInitHandle = CKSessions::Compat::OnPostEngineInit().AddLambda([]()
		{
			CKSessions::Hub::RegisterTabIfElected();
			CKSessionsEditor::Setup::RegisterTab();
		});
	}

	virtual void ShutdownModule() override
	{
		ACKSessionsDemoHUD::OnRestartPlayInEditor().Unbind();
		CKSessions::Compat::OnPostEngineInit().Remove(PostEngineInitHandle);
		CKSessions::Hub::UnregisterTabIfOwner();
		CKSessionsEditor::Setup::UnregisterTab();
	}

private:
	FDelegateHandle PostEngineInitHandle;
};

IMPLEMENT_MODULE(FCoopKitSessionsEditorModule, CoopKitSessionsEditor);
