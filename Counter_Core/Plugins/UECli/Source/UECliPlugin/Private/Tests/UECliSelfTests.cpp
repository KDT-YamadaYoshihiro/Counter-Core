// Copyright UE CLI. All rights reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

/**
 * Trivial automation tests under UECli.SelfTest.* so the `automation.run` job
 * has something deterministic to exercise. Not part of any real test suite —
 * they are here to prove the job pipeline end to end.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUECliSelfTestPass, "UECli.SelfTest.Pass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUECliSelfTestPass::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("arithmetic still works"), 2 + 2, 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUECliSelfTestFail, "UECli.SelfTest.Fail",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NegativeFilter)

bool FUECliSelfTestFail::RunTest(const FString& Parameters)
{
	AddError(TEXT("deliberate failure — UE CLI self-test for failure reporting"));
	return false;
}

#endif // WITH_AUTOMATION_TESTS
