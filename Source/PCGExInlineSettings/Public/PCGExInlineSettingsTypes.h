// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/SoftObjectPtr.h"

#include "PCGExInlineSettingsTypes.generated.h"

class UPCGSettings;

/**
 * Storage half of FPCGExInlineSettings, never used on its own. PCG graph sites ignore it: override pins honour
 * PCG_NotOverridable, while Get Graph Parameter and subgraph pins never walk super-struct members.
 */
USTRUCT()
struct PCGEXINLINESETTINGS_API FPCGExInlineSettingsBase
{
	GENERATED_BODY()

	/** Inline settings, owned by the object holding this struct. */
	UPROPERTY(VisibleAnywhere, Instanced, Category = "Settings", meta = (PCG_NotOverridable))
	TObjectPtr<UPCGSettings> Instance = nullptr;

	/** Settings asset used instead of the inline instance when set. */
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (PCG_NotOverridable))
	TSoftObjectPtr<UPCGSettings> External;

	/** Restricts which settings classes can be picked. None allows any settings class. */
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (PCG_NotOverridable, AllowAbstract))
	TSubclassOf<UPCGSettings> AllowedClass;
};

/**
 * Inline-editable PCG settings, or a reference to a settings asset. PCG graph sites (override pins, Get Graph
 * Parameter, subgraph pins) see a single soft object path, Settings, pointing at the effective settings object.
 */
USTRUCT(BlueprintType)
struct PCGEXINLINESETTINGS_API FPCGExInlineSettings : public FPCGExInlineSettingsBase
{
	GENERATED_BODY()

	FPCGExInlineSettings() = default;
	explicit FPCGExInlineSettings(TSubclassOf<UPCGSettings> InAllowedClass);

	/** External when set, otherwise the inline instance. Derived by SyncSettings; PCG overrides write it directly. */
	UPROPERTY(EditAnywhere, Category = "Settings")
	FSoftObjectPath Settings;

	/** Re-derives Settings from External and Instance. */
	void SyncSettings();

	void SetInstance(UPCGSettings* InInstance);
	void SetExternal(const TSoftObjectPtr<UPCGSettings>& InExternal);

	bool IsExternal() const { return !External.IsNull(); }

	/**
	 * Effective settings object, resolved from Settings without loading; null when unloaded or unset. Read this at
	 * execution rather than Instance: on PCG's transient override copies Instance is a nested duplicate.
	 */
	UPCGSettings* Resolve() const;

	/** Whether InClass may be used, given AllowedClass. */
	bool IsAllowedClass(const UClass* InClass) const;
	bool IsAllowed(const UPCGSettings* InSettings) const;

	void PostSerialize(const FArchive& Ar);
	bool ImportTextItem(const TCHAR*& Buffer, int32 PortFlags, UObject* Parent, FOutputDevice* ErrorText);
};

template <>
struct TStructOpsTypeTraits<FPCGExInlineSettings> : public TStructOpsTypeTraitsBase2<FPCGExInlineSettings>
{
	enum
	{
		WithPostSerialize = true,
		WithImportTextItem = true,
	};
};

namespace PCGExInlineSettings
{
	/** Flags for an inline instance outered to InOuter, matching what the details panel gives instanced subobjects. */
	PCGEXINLINESETTINGS_API EObjectFlags GetInstanceFlags(const UObject* InOuter);

	/** New inline instance owned by InOuter. Authoring only, game thread. */
	PCGEXINLINESETTINGS_API UPCGSettings* CreateInstance(UObject* InOuter, TSubclassOf<UPCGSettings> InClass);

	/** Copy of InSource owned by InOuter. Authoring only, game thread. */
	PCGEXINLINESETTINGS_API UPCGSettings* DuplicateInstance(const UPCGSettings* InSource, UObject* InOuter);
}
