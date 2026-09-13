// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "PCGCommon.h"
#include "PCGParamData.h"
#include "PCGPin.h"
#include "PCGSettings.h"
#include "Data/PCGPointArrayData.h"
#include "Metadata/PCGMetadata.h"
#include "UObject/Package.h"

#include "PCGExProxy.h"
#include "PCGExProxyInterface.h"
#include "PCGExProxyRunner.h"
#include "Tests/PCGExInlineSettingsTestSettings.h"

// These cover the node-level rules of PCGEx | Proxy: interface keyword, pin exposure, and data routing in and out.
// Execution itself (inner context, pause bridge, GC) needs a running graph and is covered by the manual matrix.

namespace PCGExProxyTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	template <typename T>
	T* NewTransient()
	{
		return NewObject<T>(GetTransientPackage(), NAME_None, RF_Transient);
	}

	UPCGExProxySettings* NewProxy(TSubclassOf<UPCGSettings> InInterfaceClass)
	{
		UPCGExProxySettings* Proxy = NewTransient<UPCGExProxySettings>();
		// 5.7's typed InitializeAs returns void (5.8 returns the struct).
		Proxy->Interface.InitializeAs<FPCGExProxyInterfaceSettings>();
		Proxy->Interface.GetMutable<FPCGExProxyInterfaceSettings>().InterfaceClass = InInterfaceClass;
		return Proxy;
	}

	const FPCGPinProperties* FindPin(const TArray<FPCGPinProperties>& InPins, const FName InLabel)
	{
		return InPins.FindByPredicate([InLabel](const FPCGPinProperties& Pin) { return Pin.Label == InLabel; });
	}

	FPCGTaggedData MakeItem(const UPCGData* InData, const FName InPin)
	{
		FPCGTaggedData Item;
		Item.Data = InData;
		Item.Pin = InPin;
		return Item;
	}

	UPCGParamData* NewParams(const FName InAttributeName)
	{
		UPCGParamData* Params = NewTransient<UPCGParamData>();
		Params->Metadata->CreateAttribute<int32>(InAttributeName, 0, /*bAllowsInterpolation=*/false, /*bOverrideParent=*/false);
		return Params;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGExProxyKeywordTest, "PCGEx.Proxy.Interface.Keyword", PCGExProxyTests::Flags)

bool FPCGExProxyKeywordTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Concrete classes qualify"), PCGExProxy::IsInterfaceClass(UPCGExInlineSettingsTestSettings::StaticClass()));
	TestTrue(TEXT("Tagged abstract classes qualify"), PCGExProxy::IsInterfaceClass(UPCGExProxyTestInterfaceSettings::StaticClass()));
	TestFalse(TEXT("Untagged abstract classes do not qualify"), PCGExProxy::IsInterfaceClass(UPCGExProxyTestUntaggedSettings::StaticClass()));
	TestFalse(TEXT("UPCGSettings itself does not qualify"), PCGExProxy::IsInterfaceClass(UPCGSettings::StaticClass()));
	TestFalse(TEXT("Null does not qualify"), PCGExProxy::IsInterfaceClass(nullptr));

	// The picker delegate hides on true.
	TestFalse(TEXT("Picker keeps tagged abstract classes"), UPCGExProxySettings::ShouldFilterInterfaceClass(UPCGExProxyTestInterfaceSettings::StaticClass()));
	TestTrue(TEXT("Picker hides untagged abstract classes"), UPCGExProxySettings::ShouldFilterInterfaceClass(UPCGExProxyTestUntaggedSettings::StaticClass()));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGExProxyAbstractPinsTest, "PCGEx.Proxy.Pins.AbstractInterface", PCGExProxyTests::Flags)

bool FPCGExProxyAbstractPinsTest::RunTest(const FString& Parameters)
{
	using namespace PCGExProxyTests;

	const UPCGExProxySettings* Proxy = NewProxy(UPCGExProxyTestInterfaceSettings::StaticClass());

	TArray<FPCGPinProperties> Inputs;
	TArray<FPCGPinProperties> Outputs;
	Proxy->GetInterfacePins(Inputs, Outputs);

	if (!TestEqual(TEXT("One input from the abstract interface"), Inputs.Num(), 1) || !TestEqual(TEXT("One output from the abstract interface"), Outputs.Num(), 1))
	{
		return false;
	}

	TestTrue(TEXT("Input label comes from the interface"), Inputs[0].Label == UPCGExProxyTestInterfaceSettings::SourcePinLabel);
	TestTrue(TEXT("Required input of an abstract interface is demoted to Normal"), Inputs[0].IsNormalPin());
	TestTrue(TEXT("Output label comes from the interface"), Outputs[0].Label == UPCGExProxyTestInterfaceSettings::ResultPinLabel);
	TestTrue(TEXT("Output type comes from the interface"), Outputs[0].AllowedTypes == FPCGDataTypeIdentifier(EPCGDataType::Param));

	const UPCGExProxySettings* Concrete = NewProxy(UPCGExInlineSettingsTestSettings::StaticClass());
	Inputs.Reset();
	Outputs.Reset();
	Concrete->GetInterfacePins(Inputs, Outputs);
	const FPCGPinProperties* PointPin = FindPin(Inputs, UPCGExInlineSettingsTestSettings::PointPinLabel);
	TestTrue(TEXT("Concrete interfaces keep their pins as declared"), PointPin && PointPin->AllowedTypes == FPCGDataTypeIdentifier(EPCGDataType::Point));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGExProxyExtraPinsTest, "PCGEx.Proxy.Pins.ExtraSanitizing", PCGExProxyTests::Flags)

bool FPCGExProxyExtraPinsTest::RunTest(const FString& Parameters)
{
	using namespace PCGExProxyTests;

	UPCGExProxySettings* Proxy = NewProxy(UPCGExProxyTestInterfaceSettings::StaticClass());

	// Never default-construct FPCGPinProperties: its default constructor is not exported.
	Proxy->ExtraInputPins.Emplace(UPCGExProxyTestInterfaceSettings::SourcePinLabel, EPCGDataType::Any); // collides with the interface
	Proxy->ExtraInputPins.Emplace(PCGPinConstants::DefaultParamsLabel, EPCGDataType::Param);             // reserved
	Proxy->ExtraInputPins.Emplace(PCGPinConstants::DefaultExecutionDependencyLabel, EPCGDataType::Any);  // reserved
	Proxy->ExtraInputPins.Emplace(NAME_None, EPCGDataType::Any);                                          // empty
	Proxy->ExtraInputPins.Emplace(TEXT("Extra"), EPCGDataType::Point);
	Proxy->ExtraInputPins.Emplace(TEXT("extra"), EPCGDataType::Any);                                      // duplicate, labels are case-insensitive
	FPCGPinProperties& Looped = Proxy->ExtraInputPins.Emplace_GetRef(TEXT("Looped"), EPCGDataType::Any);
	Looped.Usage = EPCGPinUsage::Loop;
	Looped.bInvisiblePin = true;
	Looped.SetOverrideOrUserParamPin();

	const TArray<FPCGPinProperties> Inputs = Proxy->AllInputPinProperties();

	TestNotNull(TEXT("Interface pin kept"), FindPin(Inputs, UPCGExProxyTestInterfaceSettings::SourcePinLabel));
	TestNotNull(TEXT("Valid extra pin exposed"), FindPin(Inputs, TEXT("Extra")));
	TestEqual(TEXT("Duplicate extra pin dropped"), Inputs.FilterByPredicate([](const FPCGPinProperties& Pin) { return Pin.Label == TEXT("Extra"); }).Num(), 1);
	TestNull(TEXT("Empty label dropped"), FindPin(Inputs, NAME_None));

	const FPCGPinProperties* Overrides = FindPin(Inputs, PCGPinConstants::DefaultParamsLabel);
	TestTrue(TEXT("Reserved Overrides label yields PCG's own override pin"), Overrides && Overrides->IsOverrideOrUserParamPin());

	const FPCGPinProperties* LoopedPin = FindPin(Inputs, TEXT("Looped"));
	if (TestNotNull(TEXT("Looped extra pin exposed"), LoopedPin))
	{
		TestTrue(TEXT("Usage forced to Normal"), LoopedPin->Usage == EPCGPinUsage::Normal);
		TestFalse(TEXT("Visibility forced on"), LoopedPin->bInvisiblePin);
		TestTrue(TEXT("Status forced to Normal"), LoopedPin->IsNormalPin());
	}

	// The proxy's own overridable labels are reserved once the params are cached (never on the CDO).
	TestNotNull(TEXT("Own Settings override pin present"), FindPin(Inputs, GET_MEMBER_NAME_CHECKED(UPCGExProxySettings, Settings)));
	Proxy->ExtraInputPins.Emplace(GET_MEMBER_NAME_CHECKED(UPCGExProxySettings, Settings), EPCGDataType::Any);
	const FPCGPinProperties* SettingsPin = FindPin(Proxy->AllInputPinProperties(), GET_MEMBER_NAME_CHECKED(UPCGExProxySettings, Settings));
	TestTrue(TEXT("Extra pin named after an own override is ignored"), SettingsPin && SettingsPin->IsOverrideOrUserParamPin());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGExProxyTypeGateTest, "PCGEx.Proxy.Forwarding.TypeGate", PCGExProxyTests::Flags)

bool FPCGExProxyTypeGateTest::RunTest(const FString& Parameters)
{
	const FPCGDataTypeIdentifier Any(EPCGDataType::Any);
	const FPCGDataTypeIdentifier Point(EPCGDataType::Point);
	const FPCGDataTypeIdentifier Spatial(EPCGDataType::Spatial);
	const FPCGDataTypeIdentifier Param(EPCGDataType::Param);
	const FPCGDataTypeIdentifier Invalid;

	TestTrue(TEXT("Point into Any"), PCGExProxy::IsForwardable(Point, Any));
	TestTrue(TEXT("Point into Point"), PCGExProxy::IsForwardable(Point, Point));
	TestTrue(TEXT("Point into Spatial"), PCGExProxy::IsForwardable(Point, Spatial));
	TestFalse(TEXT("Spatial into Point (wider) rejected"), PCGExProxy::IsForwardable(Spatial, Point));
	TestFalse(TEXT("Param into Point rejected"), PCGExProxy::IsForwardable(Param, Point));
	TestFalse(TEXT("Invalid data type rejected"), PCGExProxy::IsForwardable(Invalid, Any));
	TestFalse(TEXT("Invalid pin type rejected"), PCGExProxy::IsForwardable(Point, Invalid));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGExProxyInnerInputTest, "PCGEx.Proxy.Forwarding.InnerInput", PCGExProxyTests::Flags)

bool FPCGExProxyInnerInputTest::RunTest(const FString& Parameters)
{
	using namespace PCGExProxyTests;

	const FName SettingsLabel = GET_MEMBER_NAME_CHECKED(UPCGExProxySettings, Settings);

	UPCGExProxySettings* Proxy = NewProxy(UPCGExProxyTestInterfaceSettings::StaticClass());
	Proxy->ExtraInputPins.Emplace(UPCGExInlineSettingsTestSettings::AnyPinLabel, EPCGDataType::Any);
	Proxy->ExtraInputPins.Emplace(UPCGExInlineSettingsTestSettings::PointPinLabel, EPCGDataType::Any);
	Proxy->ExtraInputPins.Emplace(TEXT("Unmatched"), EPCGDataType::Any);

	// The inner exposes an 'In' (Any), a single-data 'Points' (Point) and, through its inline member, a 'Settings' override.
	UPCGExInlineSettingsTestSettings* Inner = NewTransient<UPCGExInlineSettingsTestSettings>();

	const UPCGPointArrayData* PointsA = NewTransient<UPCGPointArrayData>();
	const UPCGPointArrayData* PointsB = NewTransient<UPCGPointArrayData>();
	const UPCGParamData* Params = NewParams(TEXT("Value"));
	const UPCGParamData* MatchingOverrides = NewParams(SettingsLabel);
	const UPCGParamData* OtherOverrides = NewParams(TEXT("Unrelated"));
	const UPCGParamData* PreTask = NewParams(TEXT("Param"));

	FPCGExProxyContext* Context = new FPCGExProxyContext();
	TArray<FPCGTaggedData>& Input = Context->InputData.TaggedData;
	Input.Add(MakeItem(PointsA, UPCGExProxyTestInterfaceSettings::SourcePinLabel)); // interface pin
	Input.Add(MakeItem(Params, UPCGExInlineSettingsTestSettings::AnyPinLabel));      // extra pin, Any target
	Input.Add(MakeItem(PointsA, UPCGExInlineSettingsTestSettings::PointPinLabel));   // extra pin, Point target
	Input.Add(MakeItem(PointsB, UPCGExInlineSettingsTestSettings::PointPinLabel));   // second data on a single-data target
	Input.Add(MakeItem(Params, UPCGExInlineSettingsTestSettings::PointPinLabel));    // wrong type for the target
	Input.Add(MakeItem(Params, TEXT("Unmatched")));                                   // no inner pin
	Input.Add(MakeItem(Params, SettingsLabel));                                       // proxy-owned override pin
	Input.Add(MakeItem(MatchingOverrides, PCGPinConstants::DefaultParamsLabel));     // names an inner param
	Input.Add(MakeItem(OtherOverrides, PCGPinConstants::DefaultParamsLabel));        // names nothing the inner has
	Input.Add(MakeItem(Proxy, TEXT("Stray")));                                        // another settings object
	FPCGTaggedData& Pinless = Input.Add_GetRef(MakeItem(PreTask, NAME_None));
	Pinless.bPinlessData = true;

	FPCGDataCollection InnerInput;
	PCGExProxy::BuildInnerInput(*Context, *Proxy, *Inner, InnerInput);
	FPCGContext::Release(Context);

	const TArray<FPCGTaggedData>& Result = InnerInput.TaggedData;
	auto Count = [&Result](const UPCGData* InData, const FName InPin)
	{
		return Result.FilterByPredicate([InData, InPin](const FPCGTaggedData& Item) { return Item.Data == InData && Item.Pin == InPin; }).Num();
	};

	if (!TestTrue(TEXT("Inner settings come first, pinless"), !Result.IsEmpty() && Result[0].Data == Inner && Result[0].bPinlessData))
	{
		return false;
	}

	TestEqual(TEXT("Interface pin data forwarded"), Count(PointsA, UPCGExProxyTestInterfaceSettings::SourcePinLabel), 1);
	TestEqual(TEXT("Extra pin data forwarded onto an Any target"), Count(Params, UPCGExInlineSettingsTestSettings::AnyPinLabel), 1);
	TestEqual(TEXT("Extra pin data forwarded onto a typed target"), Count(PointsA, UPCGExInlineSettingsTestSettings::PointPinLabel), 1);
	TestEqual(TEXT("Second data on a single-data target dropped"), Count(PointsB, UPCGExInlineSettingsTestSettings::PointPinLabel), 0);
	TestEqual(TEXT("Wrong type for the target dropped"), Count(Params, UPCGExInlineSettingsTestSettings::PointPinLabel), 0);
	TestEqual(TEXT("Extra pin without an inner pin dropped"), Count(Params, TEXT("Unmatched")), 0);
	TestEqual(TEXT("Proxy-owned override pin never forwarded"), Count(Params, SettingsLabel), 0);
	TestEqual(TEXT("Global overrides naming an inner param forwarded"), Count(MatchingOverrides, PCGPinConstants::DefaultParamsLabel), 1);
	TestEqual(TEXT("Global overrides naming nothing dropped"), Count(OtherOverrides, PCGPinConstants::DefaultParamsLabel), 0);
	TestEqual(TEXT("Other settings objects dropped"), Count(Proxy, TEXT("Stray")), 0);
	TestEqual(TEXT("Pinless pre-task data forwarded"), Count(PreTask, NAME_None), 1);
	TestTrue(TEXT("No CRCs on the inner input"), InnerInput.DataCrcs.IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGExProxyOutputRoutingTest, "PCGEx.Proxy.Forwarding.OutputRouting", PCGExProxyTests::Flags)

bool FPCGExProxyOutputRoutingTest::RunTest(const FString& Parameters)
{
	using namespace PCGExProxyTests;

	const UPCGParamData* DataA = NewParams(TEXT("A"));
	const UPCGParamData* DataB = NewParams(TEXT("B"));
	const UPCGSettings* Inner = NewTransient<UPCGExInlineSettingsTestSettings>();

	FPCGDataCollection InnerOutput;
	InnerOutput.TaggedData.Add(MakeItem(DataA, TEXT("A")));
	InnerOutput.TaggedData.Add(MakeItem(DataB, TEXT("B")));
	InnerOutput.TaggedData.Add_GetRef(MakeItem(Inner, NAME_None)).bPinlessData = true;
	InnerOutput.bCancelExecution = true;

	// One output pin: everything lands on it, tagged with where it came from.
	TArray<FPCGPinProperties> SinglePin;
	SinglePin.Emplace(TEXT("Out"), EPCGDataType::Any);

	FPCGDataCollection Output;
	TArray<FName> Dropped;
	PCGExProxy::RouteOutput(InnerOutput, SinglePin, /*bTagWithInnerPin=*/true, Output, Dropped);

	TestEqual(TEXT("Settings entry dropped, data kept"), Output.TaggedData.Num(), 2);
	TestTrue(TEXT("Nothing reported dropped"), Dropped.IsEmpty());
	TestTrue(TEXT("Cancel flag copied"), Output.bCancelExecution);
	for (const FPCGTaggedData& Item : Output.TaggedData)
	{
		TestTrue(TEXT("Relabelled to the single output"), Item.Pin == TEXT("Out"));
		TestTrue(TEXT("Tagged with the inner label"), Item.Tags.Contains(Item.Data == DataA ? TEXT("A") : TEXT("B")));
	}

	// Several output pins: only declared labels survive.
	TArray<FPCGPinProperties> TwoPins;
	TwoPins.Emplace(TEXT("A"), EPCGDataType::Any);
	TwoPins.Emplace(TEXT("C"), EPCGDataType::Any);

	Output.TaggedData.Reset();
	Dropped.Reset();
	PCGExProxy::RouteOutput(InnerOutput, TwoPins, /*bTagWithInnerPin=*/false, Output, Dropped);

	TestEqual(TEXT("Only the declared label kept"), Output.TaggedData.Num(), 1);
	TestTrue(TEXT("Kept item keeps its pin"), Output.TaggedData.Num() == 1 && Output.TaggedData[0].Pin == TEXT("A") && Output.TaggedData[0].Tags.IsEmpty());
	TestTrue(TEXT("Undeclared label reported"), Dropped.Num() == 1 && Dropped[0] == TEXT("B"));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
