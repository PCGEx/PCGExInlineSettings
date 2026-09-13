// Copyright 2026 Timothé Lapetite and contributors
// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGExProxy.h"

#include "PCGCommon.h"
#include "PCGModule.h"
#include "PCGPin.h"
#include "Helpers/PCGDynamicTrackingHelpers.h"

#include "PCGExProxyRunner.h"

#define LOCTEXT_NAMESPACE "PCGExProxy"

#pragma region UPCGExProxySettings

UPCGExProxySettings::UPCGExProxySettings()
{
	Interface.InitializeAs<FPCGExProxyInterfaceSettings>();
}

#if WITH_EDITOR
FText UPCGExProxySettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip", "Executes another settings object, which can be overridden, through an interface that may be an abstract settings class. Extra input pins feed the inner settings' pins of the same label.");
}

void UPCGExProxySettings::GetStaticTrackedKeys(FPCGSelectionKeyToSettingsMap& OutKeysToSettings, TArray<TObjectPtr<const UPCGGraph>>& OutVisitedGraphs) const
{
	// Overridden settings are tracked dynamically at execution.
	if (IsPropertyOverriddenByPin(GET_MEMBER_NAME_CHECKED(UPCGExProxySettings, Settings)) || !Settings)
	{
		return;
	}

	OutKeysToSettings.FindOrAdd(FPCGSelectionKey::CreateFromPath(FSoftObjectPath(Settings))).Emplace(this, /*bCulling=*/false);
}

UObject* UPCGExProxySettings::GetJumpTargetForDoubleClick() const
{
	if (Settings)
	{
		return Settings;
	}

	const FPCGExProxyInterface* Kind = GetInterface();
	if (UObject* Target = Kind ? Kind->GetJumpTarget(Settings) : nullptr)
	{
		return Target;
	}

	return Super::GetJumpTargetForDoubleClick();
}

void UPCGExProxySettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(UPCGExProxySettings, ExtraInputPins))
	{
		TArray<FPCGPinProperties> InterfaceInputs;
		TArray<FPCGPinProperties> InterfaceOutputs;
		GetInterfacePins(InterfaceInputs, InterfaceOutputs);
		const TArray<FPCGPinProperties> ExposedPins = GetExtraInputPins(InterfaceInputs);

		for (const FPCGPinProperties& Pin : ExtraInputPins)
		{
			if (!ExposedPins.ContainsByPredicate([&Pin](const FPCGPinProperties& Exposed) { return Exposed.Label == Pin.Label; }))
			{
				UE_LOGF(LogPCG, Warning, "[%ls] Extra input pin '%ls' is ignored: empty, duplicate, or a label the interface or the proxy already uses.", *GetName(), *Pin.Label.ToString());
			}
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

bool UPCGExProxySettings::ShouldFilterInterfaceClass(const UClass* InClass)
{
	return !PCGExProxy::IsInterfaceClass(InClass);
}

EPCGChangeType UPCGExProxySettings::GetChangeTypeForProperty(FPropertyChangedEvent& PropertyChangedEvent) const
{
	// Cosmetic keeps the subtitle fresh (stock); the event overload sees the member, the FName one only the leaf.
	EPCGChangeType ChangeType = Super::GetChangeTypeForProperty(PropertyChangedEvent) | EPCGChangeType::Cosmetic;
	if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(UPCGExProxySettings, ExtraInputPins))
	{
		ChangeType |= EPCGChangeType::Structural;
	}

	return ChangeType;
}
#endif // WITH_EDITOR

FString UPCGExProxySettings::GetAdditionalTitleInformation() const
{
	const FPCGExProxyInterface* Kind = GetInterface();
	return Kind ? Kind->GetTitle(Settings) : LOCTEXT("MissingInterface", "Missing Interface").ToString();
}

void UPCGExProxySettings::GetInterfacePins(TArray<FPCGPinProperties>& OutInputs, TArray<FPCGPinProperties>& OutOutputs) const
{
	if (const FPCGExProxyInterface* Kind = GetInterface())
	{
		Kind->GetPins(Settings, OutInputs, OutOutputs);
	}
}

TArray<FPCGPinProperties> UPCGExProxySettings::GetExtraInputPins(const TArray<FPCGPinProperties>& InInterfaceInputs) const
{
	TArray<FName> Reserved;
	Reserved.Add(PCGPinConstants::DefaultParamsLabel);
	Reserved.Add(PCGPinConstants::DefaultExecutionDependencyLabel);
	for (const FPCGPinProperties& Pin : InInterfaceInputs)
	{
		Reserved.Add(Pin.Label);
	}
	for (const FPCGSettingsOverridableParam& Param : OverridableParams())
	{
		Reserved.Add(Param.Label);
	}

	TArray<FPCGPinProperties> Pins;
	for (const FPCGPinProperties& Extra : ExtraInputPins)
	{
		if (Extra.Label.IsNone() || Reserved.Contains(Extra.Label))
		{
			continue;
		}

		Reserved.Add(Extra.Label);

		// Only the routing-relevant fields are the user's; usage, visibility and status are what a plain data pin has.
		FPCGPinProperties& Pin = Pins.Add_GetRef(Extra);
		Pin.Usage = EPCGPinUsage::Normal;
		Pin.bInvisiblePin = false;
		if (Pin.IsOverrideOrUserParamPin())
		{
			Pin.SetNormalPin();
		}
		if (!Pin.AllowedTypes.IsValid())
		{
			Pin.AllowedTypes = FPCGDataTypeIdentifier(EPCGDataType::Any);
		}
	}

	return Pins;
}

TArray<FPCGPinProperties> UPCGExProxySettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Inputs;
	TArray<FPCGPinProperties> Outputs;
	GetInterfacePins(Inputs, Outputs);

	if (Inputs.IsEmpty() && Outputs.IsEmpty())
	{
		// No interface yet: default pins, optional so the node is not culled while unset.
		Inputs = Super::InputPinProperties();
		for (FPCGPinProperties& Pin : Inputs)
		{
			if (Pin.IsRequiredPin())
			{
				Pin.SetNormalPin();
			}
		}
	}

	Inputs.Append(GetExtraInputPins(Inputs));
	return Inputs;
}

TArray<FPCGPinProperties> UPCGExProxySettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Inputs;
	TArray<FPCGPinProperties> Outputs;
	GetInterfacePins(Inputs, Outputs);
	return Outputs.IsEmpty() ? Super::OutputPinProperties() : Outputs;
}

FPCGElementPtr UPCGExProxySettings::CreateElement() const
{
	return MakeShared<FPCGExProxyElement>();
}

#pragma endregion

#pragma region FPCGExProxyContext

FPCGExProxyContext::FPCGExProxyContext()
{
	WeakHandle = GetOrCreateHandle();
}

FPCGExProxyContext::~FPCGExProxyContext()
{
	if (Runner)
	{
		Runner->Release(*this);
		Runner.Reset();
	}
}

void FPCGExProxyContext::AddExtraStructReferencedObjects(FReferenceCollector& Collector)
{
	if (InnerSettings)
	{
		Collector.AddReferencedObject(InnerSettings);
	}

	if (Runner)
	{
		Runner->AddReferencedObjects(Collector);
	}
}

#pragma endregion

#pragma region FPCGExProxyElement

FPCGContext* FPCGExProxyElement::CreateContext()
{
	return new FPCGExProxyContext();
}

bool FPCGExProxyElement::CanExecuteOnlyOnMainThread(FPCGContext* Context) const
{
	// No inner yet: stay on the game thread so the inner context (and a Blueprint element's instance) is created there.
	const FPCGExProxyContext* ProxyContext = static_cast<FPCGExProxyContext*>(Context);
	return !ProxyContext || !ProxyContext->Runner || ProxyContext->Runner->CanExecuteOnlyOnMainThread(*ProxyContext);
}

bool FPCGExProxyElement::ShouldComputeFullOutputDataCrc(FPCGContext* Context) const
{
	const FPCGExProxyContext* ProxyContext = static_cast<FPCGExProxyContext*>(Context);
	return ProxyContext && ProxyContext->Runner && ProxyContext->Runner->ShouldComputeFullOutputDataCrc(*ProxyContext);
}

bool FPCGExProxyElement::PrepareDataInternal(FPCGContext* InContext) const
{
	check(InContext);
	FPCGExProxyContext* Context = static_cast<FPCGExProxyContext*>(InContext);

	if (Context->Runner)
	{
		return Context->Runner->Prepare(*Context);
	}

	const UPCGExProxySettings* Settings = Context->GetInputSettings<UPCGExProxySettings>();
	check(Settings);

	// Every rejection leaves the output empty: passing the input through would leak override params downstream.
	const FPCGExProxyInterface* Kind = Settings->GetInterface();
	if (!Kind)
	{
		PCGE_LOG(Error, GraphAndLog, LOCTEXT("MissingInterface", "No interface kind set."));
		return true;
	}

	UPCGSettings* InnerSettings = Settings->Settings;
	if (!InnerSettings)
	{
		PCGE_LOG(Error, GraphAndLog, LOCTEXT("MissingSettings", "No settings to run: set or override the Settings property."));
		return true;
	}

	FText ValidationError;
	if (!Kind->Validate(InnerSettings, ValidationError))
	{
		PCGE_LOG(Error, GraphAndLog, ValidationError);
		return true;
	}

	Context->InnerSettings = InnerSettings;

#if WITH_EDITOR
	if (Context->IsValueOverriden(GET_MEMBER_NAME_CHECKED(UPCGExProxySettings, Settings)))
	{
		FPCGDynamicTrackingHelper::AddSingleDynamicTrackingKey(Context, FPCGSelectionKey::CreateFromPath(FSoftObjectPath(InnerSettings)), /*bIsCulled=*/false);
	}
#endif

	if (!InnerSettings->bEnabled)
	{
		Context->bInnerDisabled = true;
		return true;
	}

	Context->Runner = Kind->CreateRunner();
	return Context->Runner->Prepare(*Context);
}

bool FPCGExProxyElement::ExecuteInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExProxyElement::Execute);

	check(InContext);
	FPCGExProxyContext* Context = static_cast<FPCGExProxyContext*>(InContext);

	if (Context->bInnerDisabled)
	{
		// The node's own pass-through pins apply, as they would for the inner node itself.
		DisabledPassThroughData(Context);
		return true;
	}

	return !Context->Runner || Context->Runner->Execute(*Context);
}

void FPCGExProxyElement::AbortInternal(FPCGContext* InContext) const
{
	FPCGExProxyContext* Context = static_cast<FPCGExProxyContext*>(InContext);
	if (Context && Context->Runner)
	{
		Context->Runner->Abort(*Context);
	}
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
