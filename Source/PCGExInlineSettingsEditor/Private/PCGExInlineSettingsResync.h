// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

struct FAssetData;

namespace PCGExInlineSettingsResync
{
	/** Re-derives FPCGExInlineSettings::Settings for values under InStruct at InMemory whose Instance is in InRenamedPackageName. */
	void ResyncValues(const UStruct* InStruct, void* InMemory, const FName InRenamedPackageName);

	/**
	 * In-session asset rename moves inline instances without serializing their owners. Re-derives values in the
	 * renamed package and in loaded graph parameter bags (shared copies live in other packages); transient objects are skipped.
	 */
	void OnAssetRenamed(const FAssetData& InAssetData, const FString& InOldObjectPath);
}
