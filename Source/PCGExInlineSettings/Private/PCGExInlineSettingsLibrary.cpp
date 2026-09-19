// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExInlineSettingsLibrary.h"

#include "PCGGraph.h"
#include "PCGSettings.h"
#include "StructUtils/PropertyBag.h"

namespace PCGExInlineSettingsLibrary
{
	void Read(const FPCGExInlineSettings& InValue, const TSubclassOf<UPCGSettings>& InClass, UPCGSettings*& OutSettings, bool& bOutIsValid, bool& bOutIsExternal)
	{
		bOutIsExternal = InValue.IsExternal();
		OutSettings = InValue.Resolve();

		const UClass* Required = InClass ? InClass.Get() : UPCGSettings::StaticClass();
		if (OutSettings && !OutSettings->IsA(Required))
		{
			OutSettings = nullptr;
		}

		bOutIsValid = OutSettings != nullptr;
	}
}

void UPCGExInlineSettingsLibrary::GetInlineSettings(
	const FPCGExInlineSettings& Value, TSubclassOf<UPCGSettings> SettingsClass,
	UPCGSettings*& Settings, bool& bIsValid, bool& bIsExternal)
{
	PCGExInlineSettingsLibrary::Read(Value, SettingsClass, Settings, bIsValid, bIsExternal);
}

void UPCGExInlineSettingsLibrary::GetInlineSettingsParameter(
	const UPCGGraphInterface* GraphInterface, FName ParameterName, TSubclassOf<UPCGSettings> SettingsClass,
	UPCGSettings*& Settings, bool& bIsValid, bool& bIsExternal)
{
	Settings = nullptr;
	bIsValid = false;
	bIsExternal = false;

	const FInstancedPropertyBag* Parameters = GraphInterface ? GraphInterface->GetUserParametersStruct() : nullptr;
	if (!Parameters)
	{
		return;
	}

	// Accepts any struct deriving from FPCGExInlineSettings; the view carries the parameter's own type.
	const TValueOrError<FStructView, EPropertyBagResult> Result = Parameters->GetValueStruct(ParameterName, FPCGExInlineSettings::StaticStruct());
	if (!Result.HasValue())
	{
		return;
	}

	if (const FPCGExInlineSettings* Value = Result.GetValue().GetPtr<FPCGExInlineSettings>())
	{
		PCGExInlineSettingsLibrary::Read(*Value, SettingsClass, Settings, bIsValid, bIsExternal);
	}
}

void UPCGExInlineSettingsLibrary::NotifySettingsChanged(UPCGSettings* Settings)
{
#if WITH_EDITOR
	if (!Settings)
	{
		return;
	}

	// The engine's own "something changed, I don't know what" entry point, which is exactly what a Blueprint write
	// skipped: it refreshes the cached Crc, and the graph cache is keyed on that, so the next run misses and executes.
	// Cooked builds recompute the Crc on every read and keep no editor cache, so nothing is needed there.
	Settings->PostEditChange();
#endif
}
