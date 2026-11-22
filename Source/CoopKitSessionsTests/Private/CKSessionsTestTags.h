// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

namespace CKSessions::Tests
{
	/** Test-only tags, registered by the tests module at startup (CoopKitSessionsTestsModule.cpp). */
	inline constexpr const TCHAR* TestTagNames[] = {
		TEXT("CK.Sessions.Test.State"),
		TEXT("CK.Sessions.Test.State.Down"),
		TEXT("CK.Sessions.Test.State.Busy"),
		TEXT("CK.Sessions.Test.Other")
	};

	/** A registered test tag; invalid when the tests module loaded too late to register its tags. */
	FGameplayTag GetTestTag(const TCHAR* TagName);
}
