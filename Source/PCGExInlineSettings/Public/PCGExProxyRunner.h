// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include <atomic>

#include "CoreMinimal.h"
#include "PCGData.h"
#include "PCGElement.h"
#include "PCGPin.h"
#include "Data/Registry/PCGDataTypeIdentifier.h"

class FReferenceCollector;
class UPCGExProxySettings;
class UPCGSettings;
struct FPCGContext;
struct FPCGContextHandle;
struct FPCGExProxyContext;

/** Hosting strategy for one proxy execution, owned by the proxy context. Every call comes from the proxy element. */
class PCGEXINLINESETTINGS_API FPCGExProxyRunner
{
public:
	virtual ~FPCGExProxyRunner() = default;

	/** Called from the proxy's PrepareData phase; false postpones the phase. */
	virtual bool Prepare(FPCGExProxyContext& Context) = 0;

	/** Called from the proxy's Execute phase until it returns true. Sets Context.bIsPaused / DynamicDependencies when waiting. */
	virtual bool Execute(FPCGExProxyContext& Context) = 0;

	virtual bool CanExecuteOnlyOnMainThread(const FPCGExProxyContext& Context) const = 0;
	virtual bool ShouldComputeFullOutputDataCrc(const FPCGExProxyContext& Context) const { return false; }
	virtual void Abort(FPCGExProxyContext& Context) = 0;

	/** GC hook. Runs on the game thread while the proxy may be executing on a worker: must never block on it. */
	virtual void AddReferencedObjects(FReferenceCollector& Collector) = 0;

	/** Last call, game thread. */
	virtual void Release(FPCGExProxyContext& Context) = 0;
};

namespace PCGExProxy
{
	/** Runtime type gate for forwarded data: the same subtype rule PCG applies to output validation. */
	PCGEXINLINESETTINGS_API bool IsForwardable(const FPCGDataTypeIdentifier& InDataType, const FPCGDataTypeIdentifier& InPinTypes);

	/**
	 * Inner input from the proxy input: the inner settings first (null-node lookup is first match), data on the
	 * interface pins, extra-pin data matched to an inner pin by label and type, pinless pre-task data, and the global
	 * Overrides set when one of its attributes names an inner parameter. Everything else stays on the proxy.
	 */
	PCGEXINLINESETTINGS_API void BuildInnerInput(FPCGExProxyContext& Context, const UPCGExProxySettings& InProxySettings, UPCGSettings& InInnerSettings, FPCGDataCollection& OutInput);

	/**
	 * Inner output onto the proxy's output pins. One output pin: everything is relabelled to it (what the outer
	 * PostExecute would do later, after this copy). Several: undeclared labels are dropped and reported.
	 */
	PCGEXINLINESETTINGS_API void RouteOutput(const FPCGDataCollection& InInnerOutput, TConstArrayView<FPCGPinProperties> InOutputPins, bool bTagWithInnerPin, FPCGDataCollection& OutOutput, TArray<FName>& OutDroppedLabels);
}

/** Runs the inner element in a nested, node-less context owned by the proxy context. */
class PCGEXINLINESETTINGS_API FPCGExProxyInlineRunner : public FPCGExProxyRunner
{
public:
	virtual bool Prepare(FPCGExProxyContext& Context) override;
	virtual bool Execute(FPCGExProxyContext& Context) override;
	virtual bool CanExecuteOnlyOnMainThread(const FPCGExProxyContext& Context) const override;
	virtual bool ShouldComputeFullOutputDataCrc(const FPCGExProxyContext& Context) const override;
	virtual void Abort(FPCGExProxyContext& Context) override;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual void Release(FPCGExProxyContext& Context) override;

private:
	bool InnerNeedsMainThread() const;

	/** Pauses the proxy until the inner unpauses, per the node's wake mode. Always returns false. */
	bool WaitForInner(FPCGExProxyContext& Context);
	static void QueuePoll(TWeakPtr<FPCGContextHandle> InWeakContext, TWeakPtr<FPCGExProxyRunner> InWeakRunner);

	/** Objects the GC hook reports while the inner executes; rebuilt around every inner call, never read under a lock the worker holds. */
	void RefreshSnapshot();
	void CopyOutput(FPCGExProxyContext& Context);

	FPCGElementPtr InnerElement;
	FPCGContext* InnerContext = nullptr;

	/** The executor cleared our forwarded dependencies: the inner's own pause flag must be cleared before re-entry. */
	bool bWaitsOnForwardedDependencies = false;
	std::atomic<bool> bInnerExecuting{false};
	std::atomic<bool> bPollArmed{false};

	mutable FRWLock SnapshotLock;
	TArray<TObjectPtr<const UObject>> Snapshot;

	/** Inner-created objects still carrying the Async flag once done; cleared on release, like the executor does for its own. */
	TArray<TObjectPtr<const UObject>> AsyncCleanup;
};
