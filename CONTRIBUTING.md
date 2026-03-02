# Contributing to UE LLM Toolkit

## Adding a New Tool

Every tool in the plugin follows the same pattern. Here's how to add one.

### 1. Create the Tool Header

Create a new file in `Plugin/UELLMToolkit/Source/UELLMToolkit/Private/MCP/Tools/`:

```cpp
// MCPTool_MyNewTool.h
#pragma once

#include "MCP/MCPToolBase.h"

class FMCPTool_MyNewTool : public FMCPToolBase
{
public:
    FMCPTool_MyNewTool()
        : FMCPToolBase(
            TEXT("my_new_tool"),                           // Tool name (used in HTTP endpoint)
            TEXT("Description of what this tool does.\n"   // Help text shown by ue-tool.sh help
                 "Supports operations:\n"
                 "- 'do_thing': Does a thing\n"
                 "- 'do_other': Does another thing"),
            {
                // Required parameters
                FMCPToolParameter(TEXT("operation"), TEXT("string"), TEXT("Operation to perform [do_thing, do_other]")),
                // Optional parameters
                FMCPToolParameter(TEXT("name"), TEXT("string"), TEXT("Optional name"), false, TEXT("")),
            })
    {}

protected:
    virtual FMCPToolResult ExecuteInternal(const TSharedPtr<FJsonObject>& Params) override;
};
```

### 2. Implement the Tool

Create the corresponding `.cpp` file:

```cpp
// MCPTool_MyNewTool.cpp
#include "MCP/Tools/MCPTool_MyNewTool.h"

FMCPToolResult FMCPTool_MyNewTool::ExecuteInternal(const TSharedPtr<FJsonObject>& Params)
{
    FString Operation = Params->GetStringField(TEXT("operation"));

    if (Operation == TEXT("do_thing"))
    {
        // Your implementation here
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("status"), TEXT("done"));
        return FMCPToolResult::Success(Result);
    }

    return FMCPToolResult::Error(TEXT("Unknown operation: ") + Operation);
}
```

### 3. Register the Tool

Add your tool to `MCPToolRegistry.cpp`:

```cpp
#include "MCP/Tools/MCPTool_MyNewTool.h"

// In RegisterDefaultTools():
RegisterTool(MakeShared<FMCPTool_MyNewTool>());
```

### 4. Build and Test

Build the project. Your tool is immediately available:

```bash
bash Scripts/ue-tool.sh help my_new_tool
bash Scripts/ue-tool.sh call my_new_tool '{"operation":"do_thing"}'
```

## Key Patterns

### Game Thread Execution

All UObject operations must run on the game thread. The base class handles this automatically when you use `ExecuteInternal` — it dispatches to the game thread and blocks until completion.

### Parameter Validation

Use `FMCPParamValidator` for common validation:

```cpp
FString AssetPath = Params->GetStringField(TEXT("asset_path"));
if (!FMCPParamValidator::IsValidAssetPath(AssetPath))
{
    return FMCPToolResult::Error(TEXT("Invalid asset path"));
}
```

### Error Handling

Return `FMCPToolResult::Error()` with a descriptive message. The HTTP layer returns appropriate status codes automatically.

### Async Operations

For long-running operations, users can submit via the task queue (`task_submit` tool) which wraps any tool in async execution with status polling.

## Code Style

- Follow UE5 coding standards (F/U/A/E prefixes, UPROPERTY macros)
- Keep copyright headers intact on existing files
- No `#pragma once` — it's already standard in UE
- Use `TEXT()` for all string literals passed to UE APIs

## Reporting Issues

Open an issue with:
1. UE version
2. Tool name and operation
3. JSON input that caused the issue
4. Error message or unexpected output
