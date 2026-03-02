// Copyright Natali Caggiano. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "MCP/Tools/MCPTool_Material.h"
#include "MCP/Tools/MCPTool_Asset.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

/**
 * Tests for MCPTool_Material and MCPTool_Asset
 */

// ============================================================================
// Material Tool Tests
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPToolMaterialInfo,
	"UnrealClaude.MCP.Tools.Material.GetInfo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FMCPToolMaterialInfo::RunTest(const FString& Parameters)
{
	FMCPTool_Material Tool;
	FMCPToolInfo Info = Tool.GetInfo();

	TestEqual("Tool name should be 'material'", Info.Name, TEXT("material"));
	TestTrue("Should have description", !Info.Description.IsEmpty());
	TestTrue("Should have parameters", Info.Parameters.Num() > 0);
	TestTrue("First param should be operation", Info.Parameters[0].Name == TEXT("operation"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPToolMaterialMissingOperation,
	"UnrealClaude.MCP.Tools.Material.MissingOperation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FMCPToolMaterialMissingOperation::RunTest(const FString& Parameters)
{
	FMCPTool_Material Tool;

	TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
	// No operation specified

	FMCPToolResult Result = Tool.Execute(Params);

	TestFalse("Should fail without operation", Result.bSuccess);
	TestTrue("Error should mention 'operation'", Result.Message.Contains(TEXT("operation")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPToolMaterialInvalidOperation,
	"UnrealClaude.MCP.Tools.Material.InvalidOperation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FMCPToolMaterialInvalidOperation::RunTest(const FString& Parameters)
{
	FMCPTool_Material Tool;

	TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("operation"), TEXT("invalid_operation"));

	FMCPToolResult Result = Tool.Execute(Params);

	TestFalse("Should fail with invalid operation", Result.bSuccess);
	TestTrue("Error should mention valid operations", Result.Message.Contains(TEXT("create_material_instance")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPToolMaterialCreateMissingParams,
	"UnrealClaude.MCP.Tools.Material.CreateMaterialInstanceMissingParams",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FMCPToolMaterialCreateMissingParams::RunTest(const FString& Parameters)
{
	FMCPTool_Material Tool;

	TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("operation"), TEXT("create_material_instance"));
	// Missing asset_name and parent_material

	FMCPToolResult Result = Tool.Execute(Params);

	TestFalse("Should fail without required params", Result.bSuccess);
	TestTrue("Error should mention missing param", Result.Message.Contains(TEXT("asset_name")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPToolMaterialCreateInvalidParent,
	"UnrealClaude.MCP.Tools.Material.CreateMaterialInstanceInvalidParent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FMCPToolMaterialCreateInvalidParent::RunTest(const FString& Parameters)
{
	FMCPTool_Material Tool;

	TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("operation"), TEXT("create_material_instance"));
	Params->SetStringField(TEXT("asset_name"), TEXT("MI_Test"));
	Params->SetStringField(TEXT("parent_material"), TEXT("/Game/NonExistent/Material"));

	FMCPToolResult Result = Tool.Execute(Params);

	TestFalse("Should fail with non-existent parent", Result.bSuccess);
	TestTrue("Error should mention failed to load", Result.Message.Contains(TEXT("Failed to load")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPToolMaterialSetSkeletalMeshMissingParams,
	"UnrealClaude.MCP.Tools.Material.SetSkeletalMeshMaterialMissingParams",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FMCPToolMaterialSetSkeletalMeshMissingParams::RunTest(const FString& Parameters)
{
	FMCPTool_Material Tool;

	TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("operation"), TEXT("set_skeletal_mesh_material"));
	// Missing skeletal_mesh_path and material_path

	FMCPToolResult Result = Tool.Execute(Params);

	TestFalse("Should fail without required params", Result.bSuccess);
	TestTrue("Error should mention missing param",
		Result.Message.Contains(TEXT("skeletal_mesh_path")) || Result.Message.Contains(TEXT("required")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPToolMaterialGetInfoMissingPath,
	"UnrealClaude.MCP.Tools.Material.GetMaterialInfoMissingPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FMCPToolMaterialGetInfoMissingPath::RunTest(const FString& Parameters)
{
	FMCPTool_Material Tool;

	TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("operation"), TEXT("get_material_info"));
	// Missing asset_path

	FMCPToolResult Result = Tool.Execute(Params);

	TestFalse("Should fail without asset_path", Result.bSuccess);
	TestTrue("Error should mention asset_path", Result.Message.Contains(TEXT("asset_path")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPToolMaterialPathValidation,
	"UnrealClaude.MCP.Tools.Material.PathValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FMCPToolMaterialPathValidation::RunTest(const FString& Parameters)
{
	FMCPTool_Material Tool;

	// Test path traversal rejection
	TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("operation"), TEXT("get_material_info"));
	Params->SetStringField(TEXT("asset_path"), TEXT("/Game/../Engine/Materials/BadPath"));

	FMCPToolResult Result = Tool.Execute(Params);

	TestFalse("Should reject path traversal", Result.bSuccess);

	// Test engine path rejection
	Params->SetStringField(TEXT("asset_path"), TEXT("/Engine/Materials/EngineMaterial"));
	Result = Tool.Execute(Params);

	TestFalse("Should reject engine paths", Result.bSuccess);

	return true;
}

// ============================================================================
// Asset Tool Tests
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPToolAssetInfo,
	"UnrealClaude.MCP.Tools.Asset.GetInfo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FMCPToolAssetInfo::RunTest(const FString& Parameters)
{
	FMCPTool_Asset Tool;
	FMCPToolInfo Info = Tool.GetInfo();

	TestEqual("Tool name should be 'asset'", Info.Name, TEXT("asset"));
	TestTrue("Should have description", !Info.Description.IsEmpty());
	TestTrue("Should have parameters", Info.Parameters.Num() > 0);
	TestTrue("First param should be operation", Info.Parameters[0].Name == TEXT("operation"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPToolAssetMissingOperation,
	"UnrealClaude.MCP.Tools.Asset.MissingOperation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FMCPToolAssetMissingOperation::RunTest(const FString& Parameters)
{
	FMCPTool_Asset Tool;

	TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
	// No operation specified

	FMCPToolResult Result = Tool.Execute(Params);

	TestFalse("Should fail without operation", Result.bSuccess);
	TestTrue("Error should mention 'operation'", Result.Message.Contains(TEXT("operation")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPToolAssetInvalidOperation,
	"UnrealClaude.MCP.Tools.Asset.InvalidOperation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FMCPToolAssetInvalidOperation::RunTest(const FString& Parameters)
{
	FMCPTool_Asset Tool;

	TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("operation"), TEXT("invalid_operation"));

	FMCPToolResult Result = Tool.Execute(Params);

	TestFalse("Should fail with invalid operation", Result.bSuccess);
	TestTrue("Error should mention valid operations", Result.Message.Contains(TEXT("set_asset_property")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPToolAssetSetPropertyMissingParams,
	"UnrealClaude.MCP.Tools.Asset.SetPropertyMissingParams",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FMCPToolAssetSetPropertyMissingParams::RunTest(const FString& Parameters)
{
	FMCPTool_Asset Tool;

	TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("operation"), TEXT("set_asset_property"));
	// Missing asset_path, property, value

	FMCPToolResult Result = Tool.Execute(Params);

	TestFalse("Should fail without required params", Result.bSuccess);
	TestTrue("Error should mention missing param",
		Result.Message.Contains(TEXT("asset_path")) || Result.Message.Contains(TEXT("required")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPToolAssetSetPropertyMissingValue,
	"UnrealClaude.MCP.Tools.Asset.SetPropertyMissingValue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FMCPToolAssetSetPropertyMissingValue::RunTest(const FString& Parameters)
{
	FMCPTool_Asset Tool;

	TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("operation"), TEXT("set_asset_property"));
	Params->SetStringField(TEXT("asset_path"), TEXT("/Game/Test/Asset"));
	Params->SetStringField(TEXT("property"), TEXT("SomeProperty"));
	// Missing value

	FMCPToolResult Result = Tool.Execute(Params);

	TestFalse("Should fail without value", Result.bSuccess);
	TestTrue("Error should mention value", Result.Message.Contains(TEXT("value")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPToolAssetSaveAssetMissingPath,
	"UnrealClaude.MCP.Tools.Asset.SaveAssetMissingPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FMCPToolAssetSaveAssetMissingPath::RunTest(const FString& Parameters)
{
	FMCPTool_Asset Tool;

	TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("operation"), TEXT("save_asset"));
	// Missing asset_path

	FMCPToolResult Result = Tool.Execute(Params);

	TestFalse("Should fail without asset_path", Result.bSuccess);
	TestTrue("Error should mention asset_path", Result.Message.Contains(TEXT("asset_path")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPToolAssetGetInfoMissingPath,
	"UnrealClaude.MCP.Tools.Asset.GetAssetInfoMissingPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FMCPToolAssetGetInfoMissingPath::RunTest(const FString& Parameters)
{
	FMCPTool_Asset Tool;

	TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("operation"), TEXT("get_asset_info"));
	// Missing asset_path

	FMCPToolResult Result = Tool.Execute(Params);

	TestFalse("Should fail without asset_path", Result.bSuccess);
	TestTrue("Error should mention asset_path", Result.Message.Contains(TEXT("asset_path")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPToolAssetListAssetsDefault,
	"UnrealClaude.MCP.Tools.Asset.ListAssetsDefault",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FMCPToolAssetListAssetsDefault::RunTest(const FString& Parameters)
{
	FMCPTool_Asset Tool;

	TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("operation"), TEXT("list_assets"));
	// Uses default directory /Game/

	FMCPToolResult Result = Tool.Execute(Params);

	// This should succeed even if no assets exist (returns empty list)
	TestTrue("Should succeed with defaults", Result.bSuccess);
	TestTrue("Should have result data", Result.Data.IsValid());
	TestTrue("Should have count field", Result.Data->HasField(TEXT("count")));
	TestTrue("Should have assets array", Result.Data->HasField(TEXT("assets")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPToolAssetPathValidation,
	"UnrealClaude.MCP.Tools.Asset.PathValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FMCPToolAssetPathValidation::RunTest(const FString& Parameters)
{
	FMCPTool_Asset Tool;

	// Test path traversal rejection
	TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("operation"), TEXT("get_asset_info"));
	Params->SetStringField(TEXT("asset_path"), TEXT("/Game/../Engine/BadPath"));

	FMCPToolResult Result = Tool.Execute(Params);

	TestFalse("Should reject path traversal", Result.bSuccess);

	// Test engine path rejection
	Params->SetStringField(TEXT("asset_path"), TEXT("/Engine/SomeAsset"));
	Result = Tool.Execute(Params);

	TestFalse("Should reject engine paths", Result.bSuccess);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPToolAssetPropertyPathValidation,
	"UnrealClaude.MCP.Tools.Asset.PropertyPathValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FMCPToolAssetPropertyPathValidation::RunTest(const FString& Parameters)
{
	FMCPTool_Asset Tool;

	// Test dangerous property path rejection
	TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("operation"), TEXT("set_asset_property"));
	Params->SetStringField(TEXT("asset_path"), TEXT("/Game/Test/Asset"));
	Params->SetStringField(TEXT("property"), TEXT("PropertyWith;Semicolon"));
	Params->SetBoolField(TEXT("value"), true);

	FMCPToolResult Result = Tool.Execute(Params);

	TestFalse("Should reject dangerous property path", Result.bSuccess);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPToolAssetNonExistentAsset,
	"UnrealClaude.MCP.Tools.Asset.NonExistentAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FMCPToolAssetNonExistentAsset::RunTest(const FString& Parameters)
{
	FMCPTool_Asset Tool;

	TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("operation"), TEXT("get_asset_info"));
	Params->SetStringField(TEXT("asset_path"), TEXT("/Game/NonExistent/TestAsset12345"));

	FMCPToolResult Result = Tool.Execute(Params);

	TestFalse("Should fail for non-existent asset", Result.bSuccess);
	TestTrue("Error should mention failed to load", Result.Message.Contains(TEXT("Failed to load")));

	return true;
}
