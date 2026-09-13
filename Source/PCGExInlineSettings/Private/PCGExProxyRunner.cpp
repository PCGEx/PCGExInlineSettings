// Copyright 2026 Timothé Lapetite and contributors
// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGExProxyRunner.h"

#include "PCGCommon.h"
#include "PCGContext.h"
#include "PCGModule.h"
#include "PCGParamData.h"
#include "PCGSettings.h"
#include "Metadata/PCGMetadata.h"
#include "Misc/ScopeRWLock.h"
#include "UObject/UObjectHash.h"

#include "PCGExProxy.h"

#define LOCTEXT_NAMESPACE "PCGExProxyRunner"

namespace PCGExProxy
{
	bool IsForwardable(const FPCGDataTypeIdentifier& InDataType, const FPCGDataTypeIdentifier& InPinTypes)
	{
		// IsChildOf ensures on invalid identifiers, so both sides are checked first.
		return InDataType.IsValid() && InPinTypes.IsValid() && InDataType.IsChildOf(InPinTypes);
	}

	bool HasLabel(const TArray<FPCGPinProperties>& InPins, const FName InLabel)
	{
		return InPins.ContainsByPredicate([InLabel](const FPCGPinProperties& Pin) { return Pin.Label == InLabel; });
	}

	const FPCGPinProperties* FindLabel(const TArray<FPCGPinProperties>& InPins, const FName InLabel)
	{
		return InPins.FindByPredicate([InLabel](const FPCGPinProperties& Pin) { return Pin.Label == InLabel; });
	}

	// The key the override accessor matches attribute names against; the clash path is PCG's GetPropertyPath (unexported).
	FName GetOverrideKey(const FPCGSettingsOverridableParam& InParam)
	{
		if (InParam.PropertiesNames.IsEmpty())
		{
			return NAME_None;
		}

		if (!InParam.bHasNameClash)
		{
			return InParam.PropertiesNames.Last();
		}

		return FName(FString::JoinBy(InParam.PropertiesNames, TEXT("/"), [](const FName InName) { return InName.ToString(); }));
	}

	bool ParamsNameAnyOf(const FPCGTaggedData& InItem, const TArray<FName>& InKeys)
	{
		const UPCGParamData* ParamData = Cast<const UPCGParamData>(InItem.Data.Get());
		if (!ParamData || !ParamData->Metadata)
		{
			return false;
		}

		TArray<FName> Names;
		TArray<EPCGMetadataTypes> Types;
		ParamData->Metadata->GetAttributes(Names, Types);
		return Names.ContainsByPredicate([&InKeys](const FName Name) { return InKeys.Contains(Name); });
	}

	void BuildInnerInput(FPCGExProxyContext& Context, const UPCGExProxySettings& InProxySettings, UPCGSettings& InInnerSettings, FPCGDataCollection& OutInput)
	{
		TArray<FPCGPinProperties> InterfaceInputs;
		TArray<FPCGPinProperties> InterfaceOutputs;
		InProxySettings.GetInterfacePins(InterfaceInputs, InterfaceOutputs);
		const TArray<FPCGPinProperties> ExtraPins = InProxySettings.GetExtraInputPins(InterfaceInputs);

		TArray<FPCGPinProperties> ForwardablePins;
		if (const FPCGExProxyInterface* Kind = InProxySettings.GetInterface())
		{
			Kind->GetForwardablePins(&InInnerSettings, ForwardablePins);
		}

		TArray<FName> InnerOverrideKeys;
		for (const FPCGSettingsOverridableParam& Param : InInnerSettings.OverridableParams())
		{
			InnerOverrideKeys.Add(GetOverrideKey(Param));
		}

		OutInput.TaggedData.Reset(Context.InputData.TaggedData.Num() + 1);
		OutInput.DataCrcs.Reset();
		OutInput.bCancelExecution = Context.InputData.bCancelExecution;

		FPCGTaggedData& SettingsEntry = OutInput.TaggedData.Emplace_GetRef();
		SettingsEntry.Data = &InInnerSettings;
		SettingsEntry.bPinlessData = true;

		TSet<FName> SingleDataFilled;
		TSet<FName> Reported;
		auto ReportOnce = [&Context, &Reported](const FName InLabel, const FText& InMessage)
		{
			bool bAlreadyReported = false;
			Reported.Add(InLabel, &bAlreadyReported);
			if (!bAlreadyReported)
			{
				PCGE_LOG_C(Warning, GraphAndLog, &Context, InMessage);
			}
		};

		for (const FPCGTaggedData& Item : Context.InputData.TaggedData)
		{
			// Other settings objects would compete with the inner settings in the null-node lookup.
			if (!Item.Data || Cast<const UPCGSettingsInterface>(Item.Data.Get()))
			{
				continue;
			}

			if (Item.bPinlessData || HasLabel(InterfaceInputs, Item.Pin))
			{
				OutInput.TaggedData.Add(Item);
				continue;
			}

			if (HasLabel(ExtraPins, Item.Pin))
			{
				const FPCGPinProperties* Target = FindLabel(ForwardablePins, Item.Pin);
				if (!Target)
				{
					ReportOnce(Item.Pin, FText::Format(LOCTEXT("NoInnerPin", "Extra pin '{0}': the settings have no input pin with that label; data ignored."), FText::FromName(Item.Pin)));
					continue;
				}

				const FPCGDataTypeIdentifier DataType = Item.Data->GetUnderlyingDataTypeId();
				if (!IsForwardable(DataType, Target->AllowedTypes))
				{
					ReportOnce(Item.Pin, FText::Format(LOCTEXT("TypeMismatch", "Extra pin '{0}': data of type '{1}' does not fit the inner pin type '{2}'; data ignored."), FText::FromName(Item.Pin), DataType.ToDisplayText(), Target->AllowedTypes.ToDisplayText()));
					continue;
				}

				if (!Target->bAllowMultipleData)
				{
					bool bAlreadyFilled = false;
					SingleDataFilled.Add(Item.Pin, &bAlreadyFilled);
					if (bAlreadyFilled)
					{
						ReportOnce(Item.Pin, FText::Format(LOCTEXT("SingleData", "Extra pin '{0}': the inner pin takes a single data; extra data ignored."), FText::FromName(Item.Pin)));
						continue;
					}
				}

				OutInput.TaggedData.Add(Item);
				continue;
			}

			// The global set is forwarded only when it can override something: the inner duplicates itself as soon as it sees one.
			if (Item.Pin == PCGPinConstants::DefaultParamsLabel && ParamsNameAnyOf(Item, InnerOverrideKeys))
			{
				OutInput.TaggedData.Add(Item);
			}

			// The proxy's own per-param override pins (Settings, ...) and unknown labels stay on the proxy.
		}
	}

	void RouteOutput(const FPCGDataCollection& InInnerOutput, TConstArrayView<FPCGPinProperties> InOutputPins, bool bTagWithInnerPin, FPCGDataCollection& OutOutput, TArray<FName>& OutDroppedLabels)
	{
		OutOutput.TaggedData.Reset(InInnerOutput.TaggedData.Num());
		OutOutput.DataCrcs.Reset();
		OutOutput.bCancelExecution = InInnerOutput.bCancelExecution;

		for (const FPCGTaggedData& Item : InInnerOutput.TaggedData)
		{
			// A pass-through inner echoes its settings entry; it must not leave the proxy.
			if (!Item.Data || Cast<const UPCGSettingsInterface>(Item.Data.Get()))
			{
				continue;
			}

			FPCGTaggedData Routed = Item;
			if (!Routed.bPinlessData)
			{
				if (bTagWithInnerPin && !Routed.Pin.IsNone())
				{
					Routed.Tags.Add(Routed.Pin.ToString());
				}

				if (InOutputPins.Num() == 1)
				{
					Routed.Pin = InOutputPins[0].Label;
				}
				else if (!InOutputPins.ContainsByPredicate([&Routed](const FPCGPinProperties& Pin) { return Pin.Label == Routed.Pin; }))
				{
					OutDroppedLabels.AddUnique(Routed.Pin);
					continue;
				}
			}

			OutOutput.TaggedData.Add(MoveTemp(Routed));
		}
	}

	// Mirrors the executor's own cleanup for objects created off the game thread by the inner context.
	void ClearAsyncFlagsRecursive(const UObject* InObject)
	{
		InObject->ClearInternalFlags(EInternalObjectFlags::Async);
		ForEachObjectWithOuter(InObject, [](UObject* SubObject)
		{
			SubObject->ClearInternalFlags(EInternalObjectFlags::Async);
		}, EGetObjectsFlags::IncludeNestedObjects);
	}
}

#pragma region FPCGExProxyInlineRunner

bool FPCGExProxyInlineRunner::Prepare(FPCGExProxyContext& Context)
{
	if (InnerContext)
	{
		return true;
	}

	const UPCGExProxySettings* ProxySettings = Context.GetInputSettings<UPCGExProxySettings>();
	UPCGSettings* InnerSettings = Context.InnerSettings;
	check(ProxySettings && InnerSettings);

	InnerElement = InnerSettings->GetElement();
	if (!InnerElement)
	{
		PCGE_LOG_C(Error, GraphAndLog, &Context, LOCTEXT("NoElement", "The settings have no element to run."));
		return true;
	}

	// Forwarded data must be in place before InitializeSettings reads the inner's override pins.
	FPCGDataCollection InnerInput;
	PCGExProxy::BuildInnerInput(Context, *ProxySettings, *InnerSettings, InnerInput);

	InnerContext = InnerElement->Initialize(FPCGInitializeElementParams(&InnerInput, Context.ExecutionSource, /*Node=*/nullptr));
	check(InnerContext);
	Context.SetupInnerGraphExecutor(InnerContext);
	InnerContext->InitializeSettings(/*bSkipPostLoad=*/!IsInGameThread());
	InnerContext->TaskId = Context.TaskId;
	InnerContext->CompiledTaskId = Context.CompiledTaskId;
	// DependenciesCrc stays invalid on purpose: the inner must not write the outer's cache entry.

	RefreshSnapshot();
	return true;
}

bool FPCGExProxyInlineRunner::Execute(FPCGExProxyContext& Context)
{
	if (!InnerContext || !InnerElement)
	{
		return true;
	}

	if (bWaitsOnForwardedDependencies)
	{
		// The executor emptied our dependency set and woke us; the inner still shows the pause it took when scheduling.
		bWaitsOnForwardedDependencies = false;
		InnerContext->bIsPaused = false;
	}

	if (InnerContext->bIsPaused)
	{
		return WaitForInner(Context);
	}

	if (InnerNeedsMainThread() && !IsInGameThread())
	{
		// The executor re-evaluates CanExecuteOnlyOnMainThread and moves the task.
		return false;
	}

	// Only the scheduling fields: the rest of the inner's async state is its own time-slicing progress.
	InnerContext->AsyncState.NumAvailableTasks = Context.AsyncState.NumAvailableTasks;
	InnerContext->AsyncState.EndTime = Context.AsyncState.EndTime;
	InnerContext->AsyncState.bIsRunningOnMainThread = Context.AsyncState.bIsRunningOnMainThread;
	InnerContext->AsyncState.bIsRunningOutOfTick = Context.AsyncState.bIsRunningOutOfTick;

	RefreshSnapshot();
	bInnerExecuting = true;
	const bool bDone = InnerElement->Execute(InnerContext);
	bInnerExecuting = false;
	RefreshSnapshot();

	if (!InnerContext->DynamicDependencies.IsEmpty())
	{
		// The executor watches the outer set only. Never combined with a poll: it forbids a second wake-up source.
		Context.DynamicDependencies.Append(InnerContext->DynamicDependencies);
		InnerContext->DynamicDependencies.Reset();
		bWaitsOnForwardedDependencies = true;
		Context.bIsPaused = true;
		return false;
	}

	if (!bDone)
	{
		return InnerContext->bIsPaused ? WaitForInner(Context) : false;
	}

	CopyOutput(Context);
	return true;
}

bool FPCGExProxyInlineRunner::InnerNeedsMainThread() const
{
	return InnerElement->CanExecuteOnlyOnMainThread(InnerContext) || InnerContext->CanExecuteOnlyOnMainThread();
}

bool FPCGExProxyInlineRunner::CanExecuteOnlyOnMainThread(const FPCGExProxyContext& Context) const
{
	return InnerContext && InnerElement && InnerNeedsMainThread();
}

bool FPCGExProxyInlineRunner::ShouldComputeFullOutputDataCrc(const FPCGExProxyContext& Context) const
{
	return InnerContext && InnerElement && InnerElement->ShouldComputeFullOutputDataCrc(InnerContext);
}

bool FPCGExProxyInlineRunner::WaitForInner(FPCGExProxyContext& Context)
{
	const UPCGExProxySettings* ProxySettings = Context.GetInputSettings<UPCGExProxySettings>();
	check(ProxySettings);

	switch (ProxySettings->PauseWake)
	{
	case EPCGExProxyPauseWake::Poll:
		Context.bIsPaused = true;
		if (!bPollArmed.exchange(true))
		{
			QueuePoll(Context.WeakHandle, Context.Runner);
		}
		return false;

	case EPCGExProxyPauseWake::Redispatch:
		return false;

	default:
		checkNoEntry();
		return false;
	}
}

void FPCGExProxyInlineRunner::QueuePoll(TWeakPtr<FPCGContextHandle> InWeakContext, TWeakPtr<FPCGExProxyRunner> InWeakRunner)
{
	FPCGModule::GetPCGModuleChecked().ExecuteNextTick([InWeakContext, InWeakRunner]()
	{
		const TSharedPtr<FPCGExProxyRunner> Runner = InWeakRunner.Pin();
		FPCGContext::FSharedContext<FPCGExProxyContext> SharedContext(InWeakContext);
		FPCGExProxyContext* Context = SharedContext.Get();
		if (!Runner || !Context)
		{
			return;
		}

		FPCGExProxyInlineRunner* InlineRunner = static_cast<FPCGExProxyInlineRunner*>(Runner.Get());
		if (InlineRunner->InnerContext && InlineRunner->InnerContext->bIsPaused)
		{
			QueuePoll(InWeakContext, InWeakRunner);
			return;
		}

		InlineRunner->bPollArmed = false;
		Context->bIsPaused = false;
	});
}

void FPCGExProxyInlineRunner::Abort(FPCGExProxyContext& Context)
{
	if (InnerContext && InnerElement)
	{
		InnerElement->Abort(InnerContext);
	}
}

void FPCGExProxyInlineRunner::RefreshSnapshot()
{
	TArray<TObjectPtr<const UObject>> NewSnapshot;

	auto Gather = [&NewSnapshot](const FPCGDataCollection& Collection)
	{
		for (const FPCGTaggedData& TaggedData : Collection.TaggedData)
		{
			if (TaggedData.Data)
			{
				NewSnapshot.Add(TaggedData.Data.Get());
			}
		}
	};

	Gather(InnerContext->InputData);
	Gather(InnerContext->OutputData);
	for (const TPair<FPCGDataCollection, FPCGDataCollection>& Cached : InnerContext->CachedInputToOutputInternalResults)
	{
		Gather(Cached.Key);
		Gather(Cached.Value);
	}
	if (const UPCGSettings* OverrideSettings = InnerContext->GetMutableInputSettings<UPCGSettings>())
	{
		NewSnapshot.Add(OverrideSettings);
	}

	FRWScopeLock Lock(SnapshotLock, SLT_Write);
	Snapshot = MoveTemp(NewSnapshot);
}

void FPCGExProxyInlineRunner::AddReferencedObjects(FReferenceCollector& Collector)
{
	// While the inner runs on a worker its collections may be mid-mutation: the snapshot is the only safe view.
	// Objects created meanwhile carry the Async flag, which GC treats as a root.
	if (bInnerExecuting || !InnerContext)
	{
		FRWScopeLock Lock(SnapshotLock, SLT_ReadOnly);
		Collector.AddReferencedObjects(Snapshot);
	}
	else
	{
		InnerContext->AddStructReferencedObjects(Collector);
	}

	Collector.AddReferencedObjects(AsyncCleanup);
}

void FPCGExProxyInlineRunner::CopyOutput(FPCGExProxyContext& Context)
{
	const UPCGExProxySettings* ProxySettings = Context.GetInputSettings<UPCGExProxySettings>();
	check(ProxySettings);

	TArray<FPCGPinProperties> InterfaceInputs;
	TArray<FPCGPinProperties> OutputPins;
	ProxySettings->GetInterfacePins(InterfaceInputs, OutputPins);
	if (OutputPins.IsEmpty())
	{
		OutputPins = ProxySettings->AllOutputPinProperties();
	}

	TArray<FName> DroppedLabels;
	PCGExProxy::RouteOutput(InnerContext->OutputData, OutputPins, ProxySettings->bTagOutputsBasedOnOutputPins, Context.OutputData, DroppedLabels);

	for (const FName DroppedLabel : DroppedLabels)
	{
		PCGE_LOG_C(Warning, GraphAndLog, &Context, FText::Format(LOCTEXT("DroppedOutput", "Inner output on pin '{0}' has no matching proxy output pin; data dropped."), FText::FromName(DroppedLabel)));
	}

	if (InnerContext->OutputData.InactiveOutputPinBitmask != 0)
	{
		PCGE_LOG_C(Warning, GraphAndLog, &Context, LOCTEXT("DeactivationLost", "The inner element deactivated output pins; the proxy does not forward pin deactivation, downstream nodes run on empty data."));
	}

	// Inner-created objects the outer executor never tracked; the outer's own async objects are left to it.
	for (const FPCGTaggedData& TaggedData : Context.OutputData.TaggedData)
	{
		const UPCGData* Data = TaggedData.Data.Get();
		if (Data && Data->HasAnyInternalFlags(EInternalObjectFlags::Async) && !Context.ContainsAsyncObject(Data))
		{
			AsyncCleanup.AddUnique(Data);
		}
	}
	if (const UPCGSettings* OverrideSettings = InnerContext->GetMutableInputSettings<UPCGSettings>())
	{
		if (OverrideSettings->HasAnyInternalFlags(EInternalObjectFlags::Async))
		{
			AsyncCleanup.AddUnique(OverrideSettings);
		}
	}
}

void FPCGExProxyInlineRunner::Release(FPCGExProxyContext& Context)
{
	ensureMsgf(IsInGameThread(), TEXT("PCGEx | Proxy: the inner context is released off the game thread."));

	for (const TObjectPtr<const UObject>& Object : AsyncCleanup)
	{
		if (Object && Object->HasAnyInternalFlags(EInternalObjectFlags::Async))
		{
			PCGExProxy::ClearAsyncFlagsRecursive(Object);
		}
	}
	AsyncCleanup.Reset();

	if (InnerContext)
	{
		FPCGContext::Release(InnerContext);
		InnerContext = nullptr;
	}
	InnerElement.Reset();
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
