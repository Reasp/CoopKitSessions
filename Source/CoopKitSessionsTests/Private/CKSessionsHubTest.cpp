// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "SCKSessionsHub.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "CKSessionsDataEditorToolkit.h"
#include "CKSessionsHub.h"
#include "CKSessionsProfileData.h"
#include "CKSessionsProfileDataFactory.h"
#include "CKSessionsTestWorkspace.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsHubElectionTest, "CoopKit.Sessions.Hub.Election",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsHubElectionTest::RunTest(const FString& Parameters)
{
	using CKSessions::Hub::FHubCandidate;

	// Contract version 2 (TASK-99): in a mix of version 1 and version 2 plugins a version 2 copy raises the tab.
	// Synthetic plugins: ours (version 2), an older one with a smaller class path (version 1) and another version 2 one with
	// a larger class path. The marker class is not exported (it is a contract name, not an API): known to tests by its path.
	TestEqual(TEXT("This copy implements contract version 2"), CKSessions::Hub::ContractVersion, 2);
	const FHubCandidate Ours{ 2, TEXT("/Script/CoopKitSessionsEditor.CKSessionsHubInfo") };
	const FHubCandidate OlderV1{ 1, TEXT("/Script/AnotherKit.AnotherHubInfo") };
	const FHubCandidate OtherV2{ 2, TEXT("/Script/ZNewerKit.NewerHubInfo") };
	const FHubCandidate All[] = { Ours, OlderV1, OtherV2 };
	const int32 Orders[6][3] = { { 0, 1, 2 }, { 0, 2, 1 }, { 1, 0, 2 }, { 1, 2, 0 }, { 2, 0, 1 }, { 2, 1, 0 } };
	int32 Checked = 0;
	for (const int32 (&Order)[3] : Orders)
	{
		const TArray<FHubCandidate> Candidates = { All[Order[0]], All[Order[1]], All[Order[2]] };
		const int32 Owner = CKSessions::Hub::ElectOwner(Candidates);
		TestEqual(TEXT("Version 2 beats version 1, then the smallest class path wins, in every load order"), Candidates[Owner].ClassPath, Ours.ClassPath);
		++Checked;
	}
	TestEqual(TEXT("Every load order of three plugins checked"), Checked, 6);

	const TArray<FHubCandidate> Mix = { OlderV1, OtherV2 };
	TestEqual(TEXT("A mix of version 1 and version 2: version 2 raises the tab"), Mix[CKSessions::Hub::ElectOwner(Mix)].ClassPath, OtherV2.ClassPath);
	const TArray<FHubCandidate> MixReversed = { OtherV2, OlderV1 };
	TestEqual(TEXT("The mix does not depend on the order"), MixReversed[CKSessions::Hub::ElectOwner(MixReversed)].ClassPath, OtherV2.ClassPath);
	const FHubCandidate SmallerPathV2{ 2, OlderV1.ClassPath };
	const TArray<FHubCandidate> Tie = { Ours, SmallerPathV2 };
	TestEqual(TEXT("A tie goes to the smallest class path (case-insensitive)"), Tie[CKSessions::Hub::ElectOwner(Tie)].ClassPath, SmallerPathV2.ClassPath);
	const TArray<FHubCandidate> TieReversed = { SmallerPathV2, Ours };
	TestEqual(TEXT("A tie does not depend on the order"), TieReversed[CKSessions::Hub::ElectOwner(TieReversed)].ClassPath, SmallerPathV2.ClassPath);
	TestEqual(TEXT("No candidates, no owner"), CKSessions::Hub::ElectOwner(TArray<FHubCandidate>()), INDEX_NONE);

	// Live: our marker is a candidate, exactly one plugin raised the tab, and it is the elected one.
	const TArray<FHubCandidate> Live = CKSessions::Hub::CollectCandidates();
	TestTrue(TEXT("The marker class publishes the contract version"), Live.ContainsByPredicate([&Ours](const FHubCandidate& Candidate)
	{
		return Candidate.ClassPath == Ours.ClassPath && Candidate.Version == CKSessions::Hub::ContractVersion;
	}));
	const int32 LiveOwner = CKSessions::Hub::ElectOwner(Live);
	const bool bWeOwn = LiveOwner != INDEX_NONE && Live[LiveOwner].ClassPath == Ours.ClassPath;
	TestEqual(TEXT("This copy owns the tab exactly when it won the election"), CKSessions::Hub::OwnsHubTab(), bWeOwn);
	const IConsoleVariable* OwnsHub = IConsoleManager::Get().FindConsoleVariable(TEXT("ck.sessions.OwnsHub"));
	if (TestNotNull(TEXT("The OwnsHub variable exists"), OwnsHub))
	{
		TestEqual(TEXT("The OwnsHub variable follows the ownership"), OwnsHub->GetInt(), bWeOwn ? 1 : 0);
	}
	if (FSlateApplication::IsInitialized())
	{
		TestTrue(TEXT("The CoopKitHub tab is registered"), FGlobalTabmanager::Get()->HasTabSpawner(CKSessions::Hub::TabId));
	}
	TArray<FString> Paths;
	for (const FHubCandidate& Candidate : Live)
	{
		Paths.Add(FString::Printf(TEXT("%s v%d"), *Candidate.ClassPath, Candidate.Version));
	}
	AddInfo(FString::Printf(TEXT("Hub candidates: %s; owner: %s"), *FString::Join(Paths, TEXT(", ")), LiveOwner != INDEX_NONE ? *Live[LiveOwner].ClassPath : TEXT("none")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsHubTypesTest, "CoopKit.Sessions.Hub.Types",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsHubTypesTest::RunTest(const FString& Parameters)
{
	const TArray<CKSessions::Hub::FEntityType> Types = CKSessions::Hub::CollectEntityTypes();
	const CKSessions::Hub::FEntityType* Ours = Types.FindByPredicate([](const CKSessions::Hub::FEntityType& Type) { return Type.Class == UCKSessionsProfileData::StaticClass(); });
	if (!TestNotNull(TEXT("The hub finds the sessions profile type by class metadata"), Ours))
	{
		return false;
	}
	TestEqual(TEXT("Type name"), Ours->Entity, FString(TEXT("Sessions Profile")));
	TestEqual(TEXT("Folder token"), Ours->Folder, FString(TEXT("SessionsProfiles")));
	TestTrue(TEXT("Sessions profiles have no id property"), Ours->IdProperty.IsNone());
	TestEqual(TEXT("Plugin token"), Ours->Plugin, FString(TEXT("CoopKitSessions")));
	TestFalse(TEXT("Abstract classes are not types"), Types.ContainsByPredicate([](const CKSessions::Hub::FEntityType& Type) { return Type.Class->HasAnyClassFlags(CLASS_Abstract); }));

	// The hub window shows every type: ours and the types of other CoopKit plugins installed next to it.
	const TSharedRef<SCKSessionsHub> Window = SNew(SCKSessionsHub);
	TArray<FString> Others;
	for (const CKSessions::Hub::FEntityType& Type : Types)
	{
		if (Type.Plugin != TEXT("CoopKitSessions"))
		{
			Others.Add(FString::Printf(TEXT("%s (%s)"), *Type.Entity, *Type.Plugin));
		}
	}
	TestTrue(TEXT("The hub window lists the sessions profile type"), Window->GetTypes().ContainsByPredicate([](const CKSessions::Hub::FEntityType& Type) { return Type.Entity == TEXT("Sessions Profile"); }));
	TestEqual(TEXT("The hub window lists every type"), Window->GetTypes().Num(), Types.Num());
	if (Others.IsEmpty())
	{
		AddInfo(TEXT("No other CoopKit plugin with entity types is loaded: only this plugin's type is checked."));
	}
	else
	{
		AddInfo(FString::Printf(TEXT("The type Sessions Profile is listed next to the types of other plugins: %s"), *FString::Join(Others, TEXT(", "))));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsHubNewAndOpenTest, "CoopKit.Sessions.Hub.NewAndOpen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsHubNewAndOpenTest::RunTest(const FString& Parameters)
{
	using namespace CKSessions::Tests;

	const FScopedTestWorkspace Workspace;
	CKSessions::Hub::FEntityType Type;
	TestTrue(TEXT("Sessions profile type"), CKSessions::Hub::FindEntityType(UCKSessionsProfileData::StaticClass(), Type));
	const FString Name = MakeTestName(TEXT("Hub"));

	TestEqual(TEXT("Folder by the template of [CoopKit.Workspace]"), CKSessions::Hub::ResolveEntityFolder(Type, Name), FString(TestEntitiesRoot) / Name);
	TestEqual(TEXT("Built-in template"), CKSessions::Hub::ExpandPathTemplate(CKSessions::Hub::DefaultEntityPathTemplate, Type, TEXT("Alpha")),
		FString(TEXT("/Game/CoopKit/SessionsProfiles/Alpha")));
	TestFalse(TEXT("A name with a space is refused"), CKSessions::Hub::IsValidEntityName(TEXT("Bad Name")));

	// "New ...": the type's factory (the wizard) creates the entity in the folder of the template.
	UCKSessionsProfileData* Entity = Cast<UCKSessionsProfileData>(CKSessions::Hub::CreateEntity(Type, Name, MakeTestWizard(), true));
	if (!TestNotNull(TEXT("The hub creates an entity through its factory"), Entity))
	{
		return false;
	}
	TestEqual(TEXT("Package path by the template, asset name DA_<Entity>_<Name>"), Entity->GetOutermost()->GetName(),
		FString(TestEntitiesRoot) / Name / (TEXT("DA_SessionsProfile_") + Name));
	TestTrue(TEXT("The hub lists the new entity"), CKSessions::Hub::FindEntities(Type).ContainsByPredicate([Entity](const FAssetData& Asset) { return Asset.GetAsset() == Entity; }));

	// "Open": the plugin's own card, with the Preview and Parts tabs.
	TestTrue(TEXT("The hub opens the card"), CKSessions::Hub::OpenCard(Entity));
	IAssetEditorInstance* Instance = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(Entity, false);
	if (TestNotNull(TEXT("Card of the entity"), Instance) && TestEqual(TEXT("The card is the plugin's editor"), Instance->GetEditorName(), FCKSessionsDataEditorToolkit::ToolkitName))
	{
		const TArray<FName> Tabs = static_cast<FCKSessionsDataEditorToolkit*>(Instance)->GetExtraTabIds();
		TestTrue(TEXT("The card has the Preview tab"), Tabs.Contains(FName(TEXT("CKSessionsCard_Preview"))));
		TestTrue(TEXT("The card has the Parts tab"), Tabs.Contains(FName(TEXT("CKSessionsCard_Parts"))));
	}

	// Project check: the new profile is valid data (sessions profiles have no id, so no duplicate ids either).
	const FSoftObjectPath EntityPath(Entity);
	const TArray<CKSessions::Hub::FProjectIssue> Issues = CKSessions::Hub::CheckProject();
	TestFalse(TEXT("The project check finds nothing wrong with the new profile"), Issues.ContainsByPredicate([&EntityPath](const CKSessions::Hub::FProjectIssue& Issue)
	{
		return Issue.bError && Issue.Asset == EntityPath;
	}));
	DeleteTestEntity(*Entity);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsHubNewKeepsReportTest, "CoopKit.Sessions.Hub.NewKeepsReport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsHubNewKeepsReportTest::RunTest(const FString& Parameters)
{
	using namespace CKSessions::Tests;

	const FScopedTestWorkspace Workspace;
	CKSessions::Hub::FEntityType Type;
	TestTrue(TEXT("Entity type"), CKSessions::Hub::FindEntityType(UCKSessionsProfileData::StaticClass(), Type));
	const TSharedRef<SCKSessionsHub> Window = SNew(SCKSessionsHub);
	const FString Summary = Window->GetReport().ToString();
	TestTrue(TEXT("A fresh hub shows the summary below the list"), Summary.Contains(TEXT("entity types")));

	// New: the entity is created, the list refreshes, and the line below the list still says what New did (TASK-18.17).
	const FString Name = MakeTestName(TEXT("HubNew"));
	UCKSessionsProfileData* Entity = Cast<UCKSessionsProfileData>(Window->CreateNewEntity(Type, Name, MakeTestWizard(), true));
	if (!TestNotNull(TEXT("New creates the entity"), Entity))
	{
		return false;
	}
	const FString Report = Window->GetReport().ToString();
	TestTrue(TEXT("After the refresh the hub still says Created with the entity's path"), Report.StartsWith(TEXT("Created ")) && Report.Contains(Entity->GetPathName()));
	TestTrue(TEXT("The list shows the new entity"), Window->GetVisibleEntries().ContainsByPredicate([Entity](const TSharedPtr<FCKSessionsHubEntry>& Entry) { return Entry->Asset.GetAsset() == Entity; }));
	const TSharedPtr<FCKSessionsHubEntry> Selected = Window->GetSelectedEntry();
	TestTrue(TEXT("The new entity is selected in the list"), Selected.IsValid() && Selected->Asset.GetAsset() == Entity);

	// Refresh writes the summary again.
	Window->Refresh();
	TestFalse(TEXT("Refresh replaces Created with the summary"), Window->GetReport().ToString().StartsWith(TEXT("Created ")));
	DeleteTestEntity(*Entity);
	return true;
}

namespace CKSessions::Tests::HubWindow
{
	constexpr const TCHAR* BackTag = TEXT("CoopKitHub.Back");
	constexpr const TCHAR* SearchTag = TEXT("CoopKitHub.Search");
	constexpr const TCHAR* NewNameTag = TEXT("CoopKitHub.NewName");
	constexpr const TCHAR* NewTag = TEXT("CoopKitHub.New");
	constexpr const TCHAR* OpenTag = TEXT("CoopKitHub.Open");
	constexpr const TCHAR* WhereUsedTag = TEXT("CoopKitHub.WhereUsed");
	constexpr const TCHAR* CheckProjectTag = TEXT("CoopKitHub.CheckProject");

	/** The widget with the tag below Root (the shown page of a switcher only), or null. */
	TSharedPtr<SWidget> HubTestFindTagged(const TSharedRef<SWidget>& Root, const FName Tag)
	{
		if (Root->GetTag() == Tag)
		{
			return Root;
		}
		TSharedPtr<SWidget> Found;
		Root->GetChildren()->ForEachWidget([&Found, Tag](SWidget& Child)
		{
			if (!Found.IsValid())
			{
				Found = HubTestFindTagged(Child.AsShared(), Tag);
			}
		});
		return Found;
	}

	/** Clicks the tagged button as the mouse can: only a shown and enabled one. False when there is no such button. */
	bool HubTestClick(const TSharedRef<SWidget>& Hub, const FName Tag)
	{
		// The prepass updates the bound attributes (IsEnabled) the way a frame of the editor does.
		Hub->SlatePrepass(1.0f);
		const TSharedPtr<SWidget> Widget = HubTestFindTagged(Hub, Tag);
		if (!Widget.IsValid() || Widget->GetType() != FName(TEXT("SButton")) || !Widget->IsEnabled())
		{
			return false;
		}
		StaticCastSharedPtr<SButton>(Widget)->SimulateClick();
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsHubCategoriesTest, "CoopKit.Sessions.Hub.Categories",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsHubCategoriesTest::RunTest(const FString& Parameters)
{
	using namespace CKSessions::Tests;
	using namespace CKSessions::Tests::HubWindow;

	if (!FSlateApplication::IsInitialized())
	{
		AddInfo(TEXT("Slate is not initialized (commandlet run): the hub window cannot be built."));
		return true;
	}
	const FScopedTestWorkspace Workspace;
	CKSessions::Hub::FEntityType Type;
	TestTrue(TEXT("Entity type"), CKSessions::Hub::FindEntityType(UCKSessionsProfileData::StaticClass(), Type));
	const FName CategoryTag = SCKSessionsHub::MakeCategoryTag(Type.Entity);
	const TSharedRef<SCKSessionsHub> Hub = SNew(SCKSessionsHub);

	// First screen (contract version 2, TASK-99): the categories, a button per entity type of every plugin.
	TestTrue(TEXT("The hub opens on the categories"), Hub->GetCategory().IsEmpty());
	TestTrue(TEXT("No entity is listed before a category is picked"), Hub->GetVisibleEntries().IsEmpty());
	for (const CKSessions::Hub::FEntityType& Each : Hub->GetTypes())
	{
		TestTrue(*FString::Printf(TEXT("The categories have a button for %s"), *Each.Entity), HubTestFindTagged(Hub, SCKSessionsHub::MakeCategoryTag(Each.Entity)).IsValid());
	}
	TestFalse(TEXT("No Back button on the categories"), HubTestFindTagged(Hub, BackTag).IsValid());
	const int32 CountBefore = Hub->CountEntities(Type.Entity);

	// A click on the category: only its entities, a search, New and Back.
	if (!TestTrue(TEXT("The category button is clicked"), HubTestClick(Hub, CategoryTag)))
	{
		return false;
	}
	TestEqual(TEXT("The category opens"), Hub->GetCategory(), Type.Entity);
	TestEqual(TEXT("Every entity of the category is listed"), Hub->GetVisibleEntries().Num(), CountBefore);
	TestFalse(TEXT("Only entities of the category are listed"), Hub->GetVisibleEntries().ContainsByPredicate([&Type](const TSharedPtr<FCKSessionsHubEntry>& Entry) { return Entry->Entity != Type.Entity; }));
	TestFalse(TEXT("The category buttons give way to the entities"), HubTestFindTagged(Hub, CategoryTag).IsValid());
	TestTrue(TEXT("The Back button shows"), HubTestFindTagged(Hub, BackTag).IsValid());
	TestFalse(TEXT("New is disabled without a name"), HubTestClick(Hub, NewTag));

	// New: the name typed into the box (the only typing in the hub), the New button runs the type's wizard (no window in
	// an unattended run), creates the entity, selects it and says Created.
	const FString Name = MakeTestName(TEXT("HubCategory"));
	const TSharedPtr<SWidget> NameBox = HubTestFindTagged(Hub, NewNameTag);
	if (!TestTrue(TEXT("The name box shows"), NameBox.IsValid() && NameBox->GetType() == FName(TEXT("SEditableTextBox"))))
	{
		return false;
	}
	StaticCastSharedPtr<SEditableTextBox>(NameBox)->SetText(FText::FromString(Name));
	TestTrue(TEXT("New is clicked"), HubTestClick(Hub, NewTag));
	const TSharedPtr<FCKSessionsHubEntry> Created = Hub->GetSelectedEntry();
	UCKSessionsProfileData* Entity = Created.IsValid() ? Cast<UCKSessionsProfileData>(Created->Asset.GetAsset()) : nullptr;
	if (!TestNotNull(TEXT("New created the entity and selected it"), Entity))
	{
		return false;
	}
	TestTrue(TEXT("The hub says Created"), Hub->GetReport().ToString().StartsWith(TEXT("Created ")));
	TestEqual(TEXT("The category lists the new entity"), Hub->GetVisibleEntries().Num(), CountBefore + 1);

	// The search works inside the category.
	const TSharedPtr<SWidget> Search = HubTestFindTagged(Hub, SearchTag);
	if (TestTrue(TEXT("The search box shows"), Search.IsValid() && Search->GetType() == FName(TEXT("SSearchBox"))))
	{
		StaticCastSharedPtr<SSearchBox>(Search)->SetText(FText::FromString(Name));
		TestTrue(TEXT("The search finds the new entity only"), Hub->GetVisibleEntries().Num() == 1 && Hub->GetVisibleEntries()[0]->Asset.GetAsset() == Entity);
		StaticCastSharedPtr<SSearchBox>(Search)->SetText(FText::FromString(TEXT("NoSuchEntity") + Name));
		TestTrue(TEXT("A search without matches lists nothing"), Hub->GetVisibleEntries().IsEmpty());
		StaticCastSharedPtr<SSearchBox>(Search)->SetText(FText::GetEmpty());
	}

	// The right panel: Where Used and Check Project for the selected entity (a click on its row selects it).
	Hub->SelectEntity(*Entity);
	TestTrue(TEXT("Where Used is clicked"), HubTestClick(Hub, WhereUsedTag));
	TestTrue(TEXT("Where Used lists the referencers"), Hub->GetReport().ToString().Contains(TEXT(" is used by ")));
	TestTrue(TEXT("Check Project is clicked"), HubTestClick(Hub, CheckProjectTag));
	const FString Check = Hub->GetReport().ToString();
	TestTrue(TEXT("Check Project reports"), Check.StartsWith(TEXT("Every entity")) || Check.Contains(TEXT("problem")));

	// Open: the card in its own window (the plugin's asset editor), not inside the hub.
	UAssetEditorSubsystem* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	Editors->CloseAllEditorsForAsset(Entity);
	TestNull(TEXT("The card is closed before Open"), Editors->FindEditorForAsset(Entity, false));
	TestTrue(TEXT("Open is clicked"), HubTestClick(Hub, OpenTag));
	IAssetEditorInstance* Card = Editors->FindEditorForAsset(Entity, false);
	if (TestNotNull(TEXT("Open shows the card"), Card))
	{
		TestEqual(TEXT("The card is the plugin's own editor window"), Card->GetEditorName(), FCKSessionsDataEditorToolkit::ToolkitName);
	}

	// Back: the categories again, counting the new entity; no selection, so Open is disabled.
	TestTrue(TEXT("Back is clicked"), HubTestClick(Hub, BackTag));
	TestTrue(TEXT("Back shows the categories"), Hub->GetCategory().IsEmpty() && Hub->GetVisibleEntries().IsEmpty());
	TestTrue(TEXT("The category buttons are back"), HubTestFindTagged(Hub, CategoryTag).IsValid());
	TestEqual(TEXT("The category counts the new entity"), Hub->CountEntities(Type.Entity), CountBefore + 1);
	TestFalse(TEXT("Open is disabled without a selection"), HubTestClick(Hub, OpenTag));

	DeleteTestEntity(*Entity);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
