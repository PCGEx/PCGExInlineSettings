// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "PCGGraph.h"
#include "PCGSettings.h"
#include "StructUtils/PropertyBag.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPtr.h"

#include "PCGExInlineSettingsLibrary.h"
#include "PCGExInlineSettingsTypes.h"
#include "Tests/PCGExInlineSettingsTestSettings.h"

namespace PCGExInlineSettingsLibraryTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	/** Owner outside the transient package, so Resolve reaches the owned instance rather than the Settings path. */
	UPCGExInlineSettingsTestSettings* NewOwner()
	{
		return NewObject<UPCGExInlineSettingsTestSettings>(CreatePackage(TEXT("/Temp/PCGExInlineSettingsLibraryTests")), NAME_None, RF_Transient);
	}

	/** One call's worth of output pins. */
	struct FRead
	{
		UPCGSettings* Settings = nullptr;
		bool bIsValid = false;
		bool bIsExternal = false;
	};

	/** An empty class picker, as the node presents it until the user picks a class. */
	const TSubclassOf<UPCGSettings> AnyClass;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGExInlineSettingsLibraryGetTest, "PCGEx.InlineSettings.Blueprint.Get", PCGExInlineSettingsLibraryTests::Flags)

bool FPCGExInlineSettingsLibraryGetTest::RunTest(const FString& Parameters)
{
	using namespace PCGExInlineSettingsLibraryTests;

	UPCGExInlineSettingsTestSettings* Owner = NewOwner();
	Owner->Inline.SetInstance(PCGExInlineSettings::CreateInstance(Owner, UPCGExInlineSettingsTestSettings::StaticClass()));

	FRead Read;
	UPCGExInlineSettingsLibrary::GetInlineSettings(Owner->Inline, UPCGExInlineSettingsTestSettings::StaticClass(), Read.Settings, Read.bIsValid, Read.bIsExternal);
	TestTrue(TEXT("Outputs the inline instance"), Read.Settings == Owner->Inline.Instance);
	TestTrue(TEXT("Reported valid"), Read.bIsValid);
	TestFalse(TEXT("Not reported external"), Read.bIsExternal);

	FRead Unfiltered;
	UPCGExInlineSettingsLibrary::GetInlineSettings(Owner->Inline, AnyClass, Unfiltered.Settings, Unfiltered.bIsValid, Unfiltered.bIsExternal);
	TestTrue(TEXT("An empty class picker accepts any settings class"), Unfiltered.Settings == Owner->Inline.Instance);

	// A mismatched picker nulls the pin rather than handing back an object of the wrong type.
	FRead Mismatched;
	UPCGExInlineSettingsLibrary::GetInlineSettings(Owner->Inline, UPCGExInlineSettingsTestSiblingSettings::StaticClass(), Mismatched.Settings, Mismatched.bIsValid, Mismatched.bIsExternal);
	TestNull(TEXT("A mismatched class picker outputs null"), Mismatched.Settings);
	TestFalse(TEXT("A mismatched class picker reports invalid"), Mismatched.bIsValid);

	FRead Unset;
	const FPCGExInlineSettings Empty;
	UPCGExInlineSettingsLibrary::GetInlineSettings(Empty, AnyClass, Unset.Settings, Unset.bIsValid, Unset.bIsExternal);
	TestNull(TEXT("An unset value outputs null"), Unset.Settings);
	TestFalse(TEXT("An unset value reports invalid"), Unset.bIsValid);
	TestFalse(TEXT("An unset value is not external"), Unset.bIsExternal);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGExInlineSettingsLibraryExternalTest, "PCGEx.InlineSettings.Blueprint.External", PCGExInlineSettingsLibraryTests::Flags)

bool FPCGExInlineSettingsLibraryExternalTest::RunTest(const FString& Parameters)
{
	using namespace PCGExInlineSettingsLibraryTests;

	UPCGExInlineSettingsTestSettings* Owner = NewOwner();
	UPCGSettings* Referenced = PCGExInlineSettings::CreateInstance(Owner, UPCGExInlineSettingsTestSettings::StaticClass());

	FPCGExInlineSettings Value;
	Value.SetInstance(PCGExInlineSettings::CreateInstance(Owner, UPCGExInlineSettingsTestSettings::StaticClass()));
	Value.SetExternal(TSoftObjectPtr<UPCGSettings>(Referenced));

	FRead Read;
	UPCGExInlineSettingsLibrary::GetInlineSettings(Value, UPCGExInlineSettingsTestSettings::StaticClass(), Read.Settings, Read.bIsValid, Read.bIsExternal);

	TestTrue(TEXT("Outputs the referenced asset over the leftover instance"), Read.Settings == Referenced);
	TestTrue(TEXT("Reported valid"), Read.bIsValid);
	TestTrue(TEXT("Reported external"), Read.bIsExternal);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGExInlineSettingsLibraryParameterTest, "PCGEx.InlineSettings.Blueprint.Parameter", PCGExInlineSettingsLibraryTests::Flags)

bool FPCGExInlineSettingsLibraryParameterTest::RunTest(const FString& Parameters)
{
	using namespace PCGExInlineSettingsLibraryTests;

	const FName ParameterName(TEXT("Param"));
	const FName OtherName(TEXT("Other"));

	UPCGExInlineSettingsTestSettings* Owner = NewOwner();

	FPCGExInlineSettings Value(UPCGExInlineSettingsTestSettings::StaticClass());
	Value.SetInstance(PCGExInlineSettings::CreateInstance(Owner, UPCGExInlineSettingsTestSettings::StaticClass()));

	UPCGGraph* Graph = NewObject<UPCGGraph>(GetTransientPackage(), NAME_None, RF_Transient);
	Graph->AddUserParameters({
		FPropertyBagPropertyDesc(ParameterName, EPropertyBagPropertyType::Struct, FPCGExInlineSettings::StaticStruct()),
		FPropertyBagPropertyDesc(OtherName, EPropertyBagPropertyType::Int32)});
	Graph->GetMutableUserParametersStruct_Unsafe()->SetValueStruct(ParameterName, Value);

	FRead Read;
	UPCGExInlineSettingsLibrary::GetInlineSettingsParameter(Graph, ParameterName, UPCGExInlineSettingsTestSettings::StaticClass(), Read.Settings, Read.bIsValid, Read.bIsExternal);
	TestTrue(TEXT("Outputs the parameter's inline instance"), Read.Settings == Value.Instance);
	TestTrue(TEXT("Reported valid"), Read.bIsValid);
	TestFalse(TEXT("Not reported external"), Read.bIsExternal);

	FRead Absent;
	UPCGExInlineSettingsLibrary::GetInlineSettingsParameter(Graph, TEXT("Absent"), AnyClass, Absent.Settings, Absent.bIsValid, Absent.bIsExternal);
	TestNull(TEXT("An unknown parameter name outputs null"), Absent.Settings);
	TestFalse(TEXT("An unknown parameter name reports invalid"), Absent.bIsValid);

	FRead WrongType;
	UPCGExInlineSettingsLibrary::GetInlineSettingsParameter(Graph, OtherName, AnyClass, WrongType.Settings, WrongType.bIsValid, WrongType.bIsExternal);
	TestNull(TEXT("A parameter of another type outputs null"), WrongType.Settings);
	TestFalse(TEXT("A parameter of another type reports invalid"), WrongType.bIsValid);

	FRead NoGraph;
	UPCGExInlineSettingsLibrary::GetInlineSettingsParameter(nullptr, ParameterName, AnyClass, NoGraph.Settings, NoGraph.bIsValid, NoGraph.bIsExternal);
	TestNull(TEXT("A null graph outputs null"), NoGraph.Settings);
	TestFalse(TEXT("A null graph reports invalid"), NoGraph.bIsValid);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
