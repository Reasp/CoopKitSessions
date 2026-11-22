// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UCKSessionsProfileData;
class UCKSessionsProfileDataFactory;

namespace CKSessions::Tests
{
	/**
	 * Folder the workspace tests create entities in. The entity path template points there while a test runs; assets stay
	 * unsaved (IAssetTools::CreateAsset does not write to the read-only /Temp root) and the tests delete what they create.
	 */
	inline constexpr const TCHAR* TestEntitiesRoot = TEXT("/Game/CKSessionsTest/SessionsProfiles");

	/** Folder of the profile in the older layout: a buyer's own asset, not named DA_SessionsProfile_<Name> and not in an own folder. */
	inline constexpr const TCHAR* LegacyFolder = TEXT("/Game/CKSessionsTest/Legacy");
	inline constexpr const TCHAR* LegacyProfileName = TEXT("DA_LegacyProfile");

	/** Points [CoopKit.Workspace] of the editor config at the test folders and puts the old values back at the end. */
	class FScopedTestWorkspace
	{
	public:
		FScopedTestWorkspace();
		~FScopedTestWorkspace();

	private:
		FString OldEntityTemplate;
		FString OldSharedTemplate;
	};

	/** An entity name unique in this editor session: Prefix and 8 hex digits. */
	FString MakeTestName(const TCHAR* Prefix);

	/** The New Sessions Profile wizard with its default options: the plugin's default profile. */
	UCKSessionsProfileDataFactory* MakeTestWizard();

	/** Creates a sessions profile the way the hub does (the wizard's options as set, no window). Null when nothing was created. */
	UCKSessionsProfileData* CreateTestEntity(const FString& Name, UCKSessionsProfileDataFactory* Wizard = nullptr);

	/** Sets Max Players of the profile as the Details panel would. */
	void SetTestMaxPlayers(UCKSessionsProfileData& Profile, int32 MaxPlayers);

	/** Creates a profile the way a buyer made it before the workspace existed: through the Content Browser factory, in LegacyFolder, clean. */
	UCKSessionsProfileData* CreateLegacyTestProfile();

	/** Deletes the entity with its own parts (cleanup of a test). */
	void DeleteTestEntity(UCKSessionsProfileData& Entity);
}
