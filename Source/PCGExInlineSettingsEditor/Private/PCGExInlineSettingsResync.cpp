// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExInlineSettingsResync.h"

#include "PCGGraph.h"
#include "AssetRegistry/AssetData.h"
#include "StructUtils/PropertyBag.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

#include "PCGExInlineSettingsTypes.h"

namespace PCGExInlineSettingsResync
{
	void ResyncValues(const UStruct* InStruct, void* InMemory, const FName InRenamedPackageName)
	{
		if (!InStruct || !InMemory)
		{
			return;
		}

		for (TPropertyValueIterator<FStructProperty> It(InStruct, InMemory); It; ++It)
		{
			const FStructProperty* Property = It.Key();
			if (!Property->Struct || !Property->Struct->IsChildOf(FPCGExInlineSettings::StaticStruct()))
			{
				continue;
			}

			// Only values whose inline instance moved with the renamed package; the path is read without resolving.
			FPCGExInlineSettings* Value = static_cast<FPCGExInlineSettings*>(const_cast<void*>(It.Value()));
			if (Value->Instance && FSoftObjectPath(Value->Instance).GetLongPackageFName() == InRenamedPackageName)
			{
				Value->SyncSettings();
			}
		}
	}

	// PCG's execution copies are transient and hold override values that must not be re-derived.
	bool IsTransientObject(const UObject* InObject)
	{
		return InObject->HasAnyFlags(RF_Transient) || InObject->IsIn(GetTransientPackage());
	}

	void OnAssetRenamed(const FAssetData& InAssetData, const FString& InOldObjectPath)
	{
		if (!ensureMsgf(IsInGameThread(), TEXT("PCGExInlineSettings: asset rename notification received off the game thread; inline settings paths not re-synced.")))
		{
			return;
		}

		const FName RenamedPackageName = InAssetData.PackageName;

		if (const UPackage* Package = FindPackage(nullptr, *RenamedPackageName.ToString()))
		{
			ForEachObjectWithPackage(Package, [RenamedPackageName](UObject* Object)
			{
				if (!IsTransientObject(Object))
				{
					ResyncValues(Object->GetClass(), Object, RenamedPackageName);
				}
				return true;
			});
		}

		// Parameter bag contents are not reflected on their owner, and shared copies live in other packages.
		for (TObjectIterator<UPCGGraphInterface> It; It; ++It)
		{
			if (IsTransientObject(*It))
			{
				continue;
			}

			if (FInstancedPropertyBag* Parameters = It->GetMutableUserParametersStruct_Unsafe())
			{
				ResyncValues(Parameters->GetPropertyBagStruct(), Parameters->GetMutableValue().GetMemory(), RenamedPackageName);
			}
		}
	}
}
