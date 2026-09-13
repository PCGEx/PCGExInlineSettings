// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExProxyInterface.h"

#include "PCGCommon.h"
#include "PCGSettings.h"
#include "PCGSubgraph.h"
#include "Elements/PCGExecuteBlueprint.h"
#include "Elements/Blueprint/PCGBlueprintBaseElement.h"

#include "PCGExProxyRunner.h"

#define LOCTEXT_NAMESPACE "PCGExProxyInterface"

namespace PCGExProxy
{
	const FName InterfaceMetaKey(TEXT("PCGExProxyInterface"));

#if WITH_EDITOR
	bool IsInterfaceClass(const UClass* InClass)
	{
		return InClass && (!InClass->HasAnyClassFlags(CLASS_Abstract) || InClass->HasMetaData(InterfaceMetaKey));
	}
#endif

	// Shared class-independent rejections; both kinds host through a null-node inner context.
	bool ValidateHostable(const UPCGSettings* InSettings, FText& OutError)
	{
		if (InSettings->IsA<UPCGBaseSubgraphSettings>())
		{
			OutError = LOCTEXT("SubgraphNotHostable", "Subgraph and loop settings cannot run through a proxy.");
			return false;
		}

		if (InSettings->ShouldExecuteOnGPU())
		{
			OutError = LOCTEXT("GPUNotHostable", "GPU settings cannot run through a proxy.");
			return false;
		}

		return true;
	}

	void DemoteRequiredPins(TArray<FPCGPinProperties>& InOutPins)
	{
		for (FPCGPinProperties& Pin : InOutPins)
		{
			if (Pin.IsRequiredPin())
			{
				Pin.SetNormalPin();
			}
		}
	}
}

#pragma region FPCGExProxyInterface

void FPCGExProxyInterface::GetForwardablePins(const UPCGSettings* InSettings, TArray<FPCGPinProperties>& OutPins) const
{
	if (!InSettings)
	{
		return;
	}

	// The global Overrides pin is multi-data and only merged by the executor, so it is never an extra-pin target.
	OutPins = InSettings->AllInputPinProperties();
	OutPins.RemoveAll([](const FPCGPinProperties& Pin)
	{
		return Pin.IsDatalessPin() || Pin.Label == PCGPinConstants::DefaultParamsLabel;
	});
}

TSharedRef<FPCGExProxyRunner> FPCGExProxyInterface::CreateRunner() const
{
	return MakeShared<FPCGExProxyInlineRunner>();
}

FString FPCGExProxyInterface::GetTitle(const UPCGSettings* InAuthoredSettings) const
{
	return InAuthoredSettings ? InAuthoredSettings->GetName() : LOCTEXT("MissingSettings", "Missing Settings").ToString();
}

#pragma endregion

#pragma region FPCGExProxyInterfaceSettings

void FPCGExProxyInterfaceSettings::GetPins(const UPCGSettings* InAuthoredSettings, TArray<FPCGPinProperties>& OutInputs, TArray<FPCGPinProperties>& OutOutputs) const
{
	if (const UClass* Class = InterfaceClass.Get())
	{
		const UPCGSettings* DefaultSettings = Class->GetDefaultObject<UPCGSettings>();
		OutInputs = DefaultSettings->DefaultInputPinProperties();
		OutOutputs = DefaultSettings->DefaultOutputPinProperties();

		if (Class->HasAnyClassFlags(CLASS_Abstract))
		{
			PCGExProxy::DemoteRequiredPins(OutInputs);
		}
	}
	else if (InAuthoredSettings)
	{
		OutInputs = InAuthoredSettings->DefaultInputPinProperties();
		OutOutputs = InAuthoredSettings->DefaultOutputPinProperties();
	}
}

bool FPCGExProxyInterfaceSettings::Validate(const UPCGSettings* InSettings, FText& OutError) const
{
	check(InSettings);

	if (InterfaceClass && !InSettings->GetClass()->IsChildOf(InterfaceClass))
	{
		OutError = FText::Format(LOCTEXT("ClassMismatch", "Settings '{0}' are not a '{1}'."), FText::FromName(InSettings->GetFName()), InterfaceClass->GetDisplayNameText());
		return false;
	}

	return PCGExProxy::ValidateHostable(InSettings, OutError);
}

FString FPCGExProxyInterfaceSettings::GetTitle(const UPCGSettings* InAuthoredSettings) const
{
	if (const UClass* Class = InterfaceClass.Get())
	{
#if WITH_EDITOR
		return Class->GetDefaultObject<UPCGSettings>()->GetDefaultNodeTitle().ToString();
#else
		return Class->GetName();
#endif
	}

	return FPCGExProxyInterface::GetTitle(InAuthoredSettings);
}

#pragma endregion

#pragma region FPCGExProxyInterfaceBlueprint

void FPCGExProxyInterfaceBlueprint::GetPins(const UPCGSettings* InAuthoredSettings, TArray<FPCGPinProperties>& OutInputs, TArray<FPCGPinProperties>& OutOutputs) const
{
	if (const UClass* Class = ElementClass.Get())
	{
		// Built from the element's fields: the element CDO's GetInputPins/GetOutputPins omit the default In/Out pins.
		const UPCGBlueprintBaseElement* DefaultElement = Class->GetDefaultObject<UPCGBlueprintBaseElement>();

		if (DefaultElement->bHasDefaultInPin)
		{
			OutInputs.Emplace(PCGPinConstants::DefaultInputLabel, EPCGDataType::Any);
		}
		OutInputs.Append(DefaultElement->CustomInputPins);

		if (DefaultElement->bHasDefaultOutPin)
		{
			OutOutputs.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Any);
		}
		OutOutputs.Append(DefaultElement->CustomOutputPins);
	}
	else if (InAuthoredSettings)
	{
		OutInputs = InAuthoredSettings->DefaultInputPinProperties();
		OutOutputs = InAuthoredSettings->DefaultOutputPinProperties();
	}
}

bool FPCGExProxyInterfaceBlueprint::Validate(const UPCGSettings* InSettings, FText& OutError) const
{
	check(InSettings);

	const UPCGBlueprintSettings* BlueprintSettings = Cast<UPCGBlueprintSettings>(InSettings);
	const UClass* ElementType = BlueprintSettings ? BlueprintSettings->GetElementType().Get() : nullptr;
	if (!ElementType)
	{
		OutError = FText::Format(LOCTEXT("NotBlueprintSettings", "Settings '{0}' do not run a Blueprint element."), FText::FromName(InSettings->GetFName()));
		return false;
	}

	if (ElementClass && !ElementType->IsChildOf(ElementClass))
	{
		OutError = FText::Format(LOCTEXT("ElementMismatch", "Blueprint element '{0}' is not a '{1}'."), ElementType->GetDisplayNameText(), ElementClass->GetDisplayNameText());
		return false;
	}

	return PCGExProxy::ValidateHostable(InSettings, OutError);
}

FString FPCGExProxyInterfaceBlueprint::GetTitle(const UPCGSettings* InAuthoredSettings) const
{
	if (const UClass* Class = ElementClass.Get())
	{
#if WITH_EDITOR
		return Class->GetDisplayNameText().ToString();
#else
		return Class->GetName();
#endif
	}

	return FPCGExProxyInterface::GetTitle(InAuthoredSettings);
}

#if WITH_EDITOR
UObject* FPCGExProxyInterfaceBlueprint::GetJumpTarget(const UPCGSettings* InAuthoredSettings) const
{
	return ElementClass ? ElementClass->ClassGeneratedBy.Get() : nullptr;
}
#endif

#pragma endregion

#undef LOCTEXT_NAMESPACE
