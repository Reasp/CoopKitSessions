// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

// Engine version compatibility layer for CoopKitSessions.
//
// The plugin supports every Unreal Engine version from the minimum in Tools/Fab/engine-matrix.json up to the latest.
// All differences between engine versions are handled HERE and only here, so the rest of the code is written
// against a single API. Use the engine macros from Misc/EngineVersionComparison.h:
//
//   #if UE_VERSION_OLDER_THAN(5, 5, 0)
//       // UE 5.4 code path
//   #else
//       // UE 5.5+ code path
//   #endif
//
// Put each difference into the CKSessions::Compat namespace and document which engine change it covers
// (version and deprecated/renamed API). Switch to the new API only from a version where its presence is
// verified; otherwise keep the old API (with deprecation warnings suppressed here only) until the last
// verified version. Real example (GameplayAbilities: UE 5.5 deprecated FGameplayAbilitySpec::DynamicAbilityTags
// in favor of GetDynamicSpecSourceTags(), which does not exist in 5.4):
//
//   namespace CKSessions::Compat
//   {
//       inline FGameplayTagContainer& GetDynamicSpecTags(FGameplayAbilitySpec& Spec)
//       {
//   #if UE_VERSION_OLDER_THAN(5, 5, 0)
//           return Spec.DynamicAbilityTags;
//   #else
//           return Spec.GetDynamicSpecSourceTags();
//   #endif
//       }
//   }

#include "GameplayTagsManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/EngineVersionComparison.h"
#include "Online/OnlineSessionNames.h"
#include "OnlineSessionSettings.h"

/** True when compiling against Unreal Engine Major.Minor or newer. */
#define CKSESSIONS_ENGINE_AT_LEAST(Major, Minor) (!UE_VERSION_OLDER_THAN(Major, Minor, 0))

namespace CKSessions::Compat
{
	/**
	 * Runs Delegate when the tag manager accepts tags added with UGameplayTagsManager::AddNativeGameplayTag (for tags that
	 * cannot be FNativeGameplayTag, e.g. test tags of an Editor module). Register it from a module's StartupModule.
	 * UE 5.4-5.7: OnLastChanceToAddNativeTags, fired once during startup; adding tags later trips an ensure.
	 * UE 5.8: OnLastChanceToAddNativeTags is deprecated (UE_DEPRECATED(5.8)); CallOrRegister_OnAddNativeTagsDelegate
	 * runs the delegate at once when tags are already accepted.
	 */
	inline void CallOrRegisterOnAddNativeTags(const FSimpleMulticastDelegate::FDelegate& Delegate)
	{
#if UE_VERSION_OLDER_THAN(5, 8, 0)
		UGameplayTagsManager::OnLastChanceToAddNativeTags().Add(Delegate);
#else
		UGameplayTagsManager::CallOrRegister_OnAddNativeTagsDelegate(Delegate);
#endif
	}

	/**
	 * Fires once every module of the loading phases up to Default has started (the CoopKit hub election runs then).
	 * UE 5.4-5.7: the field FCoreDelegates::OnPostEngineInit. UE 5.8: the field is deprecated (UE_DEPRECATED(5.8)); the
	 * replacement GetOnPostEngineInit() exists only since 5.8.
	 */
	inline FSimpleMulticastDelegate& OnPostEngineInit()
	{
#if UE_VERSION_OLDER_THAN(5, 8, 0)
		return FCoreDelegates::OnPostEngineInit;
#else
		return FCoreDelegates::GetOnPostEngineInit();
#endif
	}

	/**
	 * Search key for lobbies in UE 5.4: SEARCH_PRESENCE ("PRESENCESEARCH"). UE 5.5 deprecated it (Steam 5.5-5.6 reads
	 * either key), UE 5.7 removed it and its Steam subsystem reads SEARCH_LOBBIES ("LOBBYSEARCH") only (checked in the
	 * 5.4-5.8 sources). The plugin spells the old key itself and never uses the symbol.
	 */
	inline FName GetLegacyPresenceSearchKey()
	{
		return FName(TEXT("PRESENCESEARCH"));
	}

	/**
	 * Asks the search for lobbies (or for game sessions when false) on every engine version: sets both the 5.4 Steam key
	 * and SEARCH_LOBBIES, which EOS reads on every version and Steam reads from 5.5. Subsystems ignore the key they do not
	 * know.
	 */
	inline void SetLobbySearch(FOnlineSearchSettings& QuerySettings, const bool bLobbies)
	{
		QuerySettings.Set(SEARCH_LOBBIES, bLobbies, EOnlineComparisonOp::Equals);
		QuerySettings.Set(GetLegacyPresenceSearchKey(), bLobbies, EOnlineComparisonOp::Equals);
	}
}
