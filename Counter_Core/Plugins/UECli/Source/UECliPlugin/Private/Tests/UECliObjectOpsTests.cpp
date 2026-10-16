// Copyright UE CLI. All rights reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Components/PointLightComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Object/UECliObjectOps.h"
#include "UObject/Package.h"

/**
 * UECli.Unit.ObjectOps.* — the generic reflection get/set (ue_get_properties /
 * ue_set_property) against a transient point light component: value parsing and
 * rejection, nested struct members, array elements, enums, bitfield bools.
 * Run them with `uecli test UECli.Unit` (also part of the live contract tests).
 */
namespace UECli::Tests
{
	namespace
	{
		struct FSetResult
		{
			bool bOk = false;
			FString Value;
			FString Error;
		};

		FSetResult Set(UObject& Object, const FString& Property, const FString& Value)
		{
			TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
			FSetResult Result;
			bool bNotFound = false;
			Result.bOk = UECli::ObjectOps::SetProperty(Object.GetPathName(), Property, Value, Out, Result.Error, bNotFound);
			Out->TryGetStringField(TEXT("value"), Result.Value);
			return Result;
		}

		FString Get(UObject& Object, const FString& Property)
		{
			TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
			FString Error, Code, Value;
			if (UECli::ObjectOps::GetProperties(Object.GetPathName(), FString(), true, Property, Out, Error, Code))
			{
				const TArray<TSharedPtr<FJsonValue>>* Props = nullptr;
				if (Out->TryGetArrayField(TEXT("properties"), Props) && Props->Num() == 1)
				{
					(*Props)[0]->AsObject()->TryGetStringField(TEXT("value"), Value);
				}
			}
			return Value;
		}

		UPointLightComponent* MakeLight()
		{
			return NewObject<UPointLightComponent>(GetTransientPackage(), NAME_None, RF_Transient);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUECliObjectOpsNumbers, "UECli.Unit.ObjectOps.Numbers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUECliObjectOpsNumbers::RunTest(const FString& Parameters)
{
	using namespace UECli::Tests;
	UPointLightComponent* Light = MakeLight();

	TestTrue(TEXT("float accepted"), Set(*Light, TEXT("Intensity"), TEXT("250")).bOk);
	TestEqual(TEXT("float stored"), Light->Intensity, 250.0f);
	const FSetResult Fraction = Set(*Light, TEXT("Intensity"), TEXT("0.25"));
	TestTrue(TEXT("fraction accepted"), Fraction.bOk);
	TestEqual(TEXT("fraction stored"), Light->Intensity, 0.25f);
	TestEqual(TEXT("response reports the stored value"), FCString::Atof(*Fraction.Value), 0.25f);
	// The engine clamps Intensity to >= 0 on PostEditChangeProperty; the response shows the clamped value.
	const FSetResult Clamped = Set(*Light, TEXT("Intensity"), TEXT("-5"));
	TestTrue(TEXT("negative accepted by the parser"), Clamped.bOk);
	TestEqual(TEXT("response shows the engine-clamped value"), FCString::Atof(*Clamped.Value), Light->Intensity);
	TestTrue(TEXT("float accepted again"), Set(*Light, TEXT("Intensity"), TEXT("150")).bOk);

	const FSetResult Bad = Set(*Light, TEXT("Intensity"), TEXT("abc"));
	TestFalse(TEXT("text rejected for a float"), Bad.bOk);
	TestEqual(TEXT("rejected value leaves the property alone"), Light->Intensity, 150.0f);
	TestFalse(TEXT("trailing garbage rejected"), Set(*Light, TEXT("Intensity"), TEXT("5 apples")).bOk);
	// UE's float import stops at 'e', so exponent notation is refused (not misread as 1.5).
	TestFalse(TEXT("exponent notation refused"), Set(*Light, TEXT("Intensity"), TEXT("1.5e2")).bOk);
	TestEqual(TEXT("still untouched"), Light->Intensity, 150.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUECliObjectOpsStructsAndArrays, "UECli.Unit.ObjectOps.StructsAndArrays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUECliObjectOpsStructsAndArrays::RunTest(const FString& Parameters)
{
	using namespace UECli::Tests;
	UPointLightComponent* Light = MakeLight();

	TestTrue(TEXT("struct member set"), Set(*Light, TEXT("LightColor.R"), TEXT("10")).bOk);
	TestEqual(TEXT("struct member stored"), static_cast<int32>(Light->LightColor.R), 10);
	TestEqual(TEXT("struct member read alone"), Get(*Light, TEXT("LightColor.R")), FString(TEXT("10")));
	TestFalse(TEXT("member of a non-struct refused"), Set(*Light, TEXT("Intensity.X"), TEXT("1")).bOk);

	TestTrue(TEXT("whole array set"), Set(*Light, TEXT("ComponentTags"), TEXT("(first,second)")).bOk);
	TestEqual(TEXT("array length"), Light->ComponentTags.Num(), 2);
	TestTrue(TEXT("element set"), Set(*Light, TEXT("ComponentTags[1]"), TEXT("changed")).bOk);
	TestEqual(TEXT("element stored"), Light->ComponentTags[1], FName(TEXT("changed")));
	TestEqual(TEXT("element read alone"), Get(*Light, TEXT("ComponentTags[1]")), FString(TEXT("changed")));
	TestFalse(TEXT("out-of-range index refused"), Set(*Light, TEXT("ComponentTags[5]"), TEXT("x")).bOk);
	TestFalse(TEXT("negative index refused"), Set(*Light, TEXT("ComponentTags[-1]"), TEXT("x")).bOk);
	TestFalse(TEXT("index on a non-array refused"), Set(*Light, TEXT("Intensity[0]"), TEXT("1")).bOk);
	TestEqual(TEXT("array untouched by refused writes"), Light->ComponentTags.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUECliObjectOpsEnumsAndBools, "UECli.Unit.ObjectOps.EnumsAndBools",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUECliObjectOpsEnumsAndBools::RunTest(const FString& Parameters)
{
	using namespace UECli::Tests;
	UPointLightComponent* Light = MakeLight();

	TestTrue(TEXT("enum by name"), Set(*Light, TEXT("Mobility"), TEXT("Movable")).bOk);
	TestEqual(TEXT("enum stored"), static_cast<int32>(Light->Mobility), static_cast<int32>(EComponentMobility::Movable));
	TestFalse(TEXT("unknown enum name refused"), Set(*Light, TEXT("Mobility"), TEXT("Bogus")).bOk);

	TestTrue(TEXT("bitfield bool false"), Set(*Light, TEXT("bAffectsWorld"), TEXT("false")).bOk);
	TestFalse(TEXT("bitfield bool stored false"), static_cast<bool>(Light->bAffectsWorld));
	TestTrue(TEXT("bitfield bool true"), Set(*Light, TEXT("bAffectsWorld"), TEXT("True")).bOk);
	TestTrue(TEXT("bitfield bool stored true"), static_cast<bool>(Light->bAffectsWorld));
	TestFalse(TEXT("non-bool refused"), Set(*Light, TEXT("bAffectsWorld"), TEXT("maybe")).bOk);

	TestFalse(TEXT("unknown property refused"), Set(*Light, TEXT("NoSuchProperty"), TEXT("1")).bOk);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
