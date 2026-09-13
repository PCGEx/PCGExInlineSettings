// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExInlineSettingsTypes.h"

#include "PCGContext.h"
#include "PCGSettings.h"
#include "UObject/UObjectGlobals.h"

#pragma region FPCGExInlineSettings

FPCGExInlineSettings::FPCGExInlineSettings(TSubclassOf<UPCGSettings> InAllowedClass)
{
	AllowedClass = InAllowedClass;
}

void FPCGExInlineSettings::SyncSettings()
{
	FSoftObjectPath DerivedSettings;
	if (!External.IsNull())
	{
		DerivedSettings = External.ToSoftObjectPath();
	}
	else if (Instance)
	{
		// Built from the handle, so an unresolved Instance is never resolved (safe during load).
		DerivedSettings = FSoftObjectPath(Instance);
	}

	// Write only on change: PCG workers may be reading this value.
	if (Settings != DerivedSettings)
	{
		Settings = DerivedSettings;
	}
}

void FPCGExInlineSettings::SetInstance(UPCGSettings* InInstance)
{
	Instance = InInstance;
	SyncSettings();
}

void FPCGExInlineSettings::SetExternal(const TSoftObjectPtr<UPCGSettings>& InExternal)
{
	External = InExternal;
	SyncSettings();
}

UPCGSettings* FPCGExInlineSettings::Resolve() const
{
	return Cast<UPCGSettings>(Settings.ResolveObject());
}

bool FPCGExInlineSettings::IsAllowedClass(const UClass* InClass) const
{
	const UClass* BaseClass = AllowedClass.Get() ? AllowedClass.Get() : UPCGSettings::StaticClass();
	return InClass && InClass->IsChildOf(BaseClass);
}

bool FPCGExInlineSettings::IsAllowed(const UPCGSettings* InSettings) const
{
	return InSettings && IsAllowedClass(InSettings->GetClass());
}

void FPCGExInlineSettings::PostSerialize(const FArchive& Ar)
{
	// Load, duplication (asset duplicate, PIE) and undo/redo all move the instance. PCG's execution copies keep the
	// persistent path: their nested instance dies with the context.
	if (Ar.IsLoading() && !FPCGContext::IsInitializingSettings())
	{
		SyncSettings();
	}
}

bool FPCGExInlineSettings::ImportTextItem(const TCHAR*& Buffer, int32 PortFlags, UObject* Parent, FOutputDevice* ErrorText)
{
	// Default import with the native override off (it would recurse here), then re-derive: pasted text carries the source's path.
	const UScriptStruct* Struct = StaticStruct();
	const TCHAR* Result = Struct->ImportText(Buffer, static_cast<void*>(this), Parent, PortFlags, ErrorText, Struct->GetName(), /*bAllowNativeOverride=*/false);
	if (!Result)
	{
		return false;
	}

	Buffer = Result;
	SyncSettings();
	return true;
}

#pragma endregion

namespace PCGExInlineSettings
{
	EObjectFlags GetInstanceFlags(const UObject* InOuter)
	{
		check(InOuter);

		EObjectFlags Flags = InOuter->GetMaskedFlags(RF_PropagateToSubObjects) | RF_Transactional;
		if (InOuter->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
		{
			Flags |= RF_ArchetypeObject;
		}

		return Flags;
	}

	UPCGSettings* CreateInstance(UObject* InOuter, TSubclassOf<UPCGSettings> InClass)
	{
		check(IsInGameThread());
		check(InOuter);

		if (!ensure(InClass && !InClass->HasAnyClassFlags(CLASS_Abstract)))
		{
			return nullptr;
		}

		return NewObject<UPCGSettings>(InOuter, InClass, NAME_None, GetInstanceFlags(InOuter));
	}

	UPCGSettings* DuplicateInstance(const UPCGSettings* InSource, UObject* InOuter)
	{
		check(IsInGameThread());
		check(InOuter);

		if (!InSource)
		{
			return nullptr;
		}

		// Propagated flags come from the new outer, not from wherever the source lived.
		FObjectDuplicationParameters Params = InitStaticDuplicateObjectParams(InSource, InOuter);
		Params.FlagMask &= ~(RF_Public | RF_ArchetypeObject | RF_Transactional | RF_Transient);
		Params.ApplyFlags |= GetInstanceFlags(InOuter);

		return Cast<UPCGSettings>(StaticDuplicateObjectEx(Params));
	}
}
