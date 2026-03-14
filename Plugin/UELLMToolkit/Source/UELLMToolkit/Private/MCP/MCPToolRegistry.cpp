// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPToolRegistry.h"
#include "MCPTaskQueue.h"
#include "UnrealClaudeModule.h"
#include "UnrealClaudeConstants.h"

// Include all tool implementations
#include "Tools/MCPTool_SpawnActor.h"
#include "Tools/MCPTool_GetLevelActors.h"
#include "Tools/MCPTool_SetProperty.h"
#include "Tools/MCPTool_RunConsoleCommand.h"
#include "Tools/MCPTool_DeleteActors.h"
#include "Tools/MCPTool_MoveActor.h"
#include "Tools/MCPTool_GetOutputLog.h"
#include "Tools/MCPTool_ExecuteScript.h"
#include "Tools/MCPTool_CleanupScripts.h"
#include "Tools/MCPTool_GetScriptHistory.h"
#include "Tools/MCPTool_CaptureViewport.h"
#include "Tools/MCPTool_BlueprintQuery.h"
#include "Tools/MCPTool_BlueprintModify.h"
#include "Tools/MCPTool_AnimBlueprintModify.h"
#include "Tools/MCPTool_MontageModify.h"
#include "Tools/MCPTool_AssetSearch.h"
#include "Tools/MCPTool_AssetDependencies.h"
#include "Tools/MCPTool_AssetReferencers.h"
#include "Tools/MCPTool_EnhancedInput.h"
#include "Tools/MCPTool_Character.h"
#include "Tools/MCPTool_CharacterData.h"
#include "Tools/MCPTool_Material.h"
#include "Tools/MCPTool_Asset.h"
#include "Tools/MCPTool_OpenLevel.h"
#include "Tools/MCPTool_LevelQuery.h"
#include "Tools/MCPTool_ControlRig.h"
#include "Tools/MCPTool_Retarget.h"
#include "Tools/MCPTool_BlendSpace.h"
#include "Tools/MCPTool_AssetImport.h"
#include "Tools/MCPTool_Widget.h"
#include "Tools/MCPTool_AnimEdit.h"
#include "Tools/MCPTool_GameplayDebug.h"
#include "Tools/MCPTool_Sequencer.h"

// Task queue tools
#include "Tools/MCPTool_TaskSubmit.h"
#include "Tools/MCPTool_TaskStatus.h"
#include "Tools/MCPTool_TaskResult.h"
#include "Tools/MCPTool_TaskList.h"
#include "Tools/MCPTool_TaskCancel.h"

FMCPToolRegistry::FMCPToolRegistry()
{
	RegisterBuiltinTools();
}

FMCPToolRegistry::~FMCPToolRegistry()
{
	StopTaskQueue();
	Tools.Empty();
}

void FMCPToolRegistry::StartTaskQueue()
{
	if (TaskQueue.IsValid())
	{
		TaskQueue->Start();
	}
}

void FMCPToolRegistry::StopTaskQueue()
{
	if (TaskQueue.IsValid())
	{
		TaskQueue->Shutdown();
	}
}

void FMCPToolRegistry::RegisterBuiltinTools()
{
	UE_LOG(LogUnrealClaude, Log, TEXT("Registering MCP tools..."));

	// Register all built-in tools
	RegisterTool(MakeShared<FMCPTool_SpawnActor>());
	RegisterTool(MakeShared<FMCPTool_GetLevelActors>());
	RegisterTool(MakeShared<FMCPTool_SetProperty>());
	RegisterTool(MakeShared<FMCPTool_RunConsoleCommand>());
	RegisterTool(MakeShared<FMCPTool_DeleteActors>());
	RegisterTool(MakeShared<FMCPTool_MoveActor>());
	RegisterTool(MakeShared<FMCPTool_GetOutputLog>());

	// Script execution tools
	RegisterTool(MakeShared<FMCPTool_ExecuteScript>());
	RegisterTool(MakeShared<FMCPTool_CleanupScripts>());
	RegisterTool(MakeShared<FMCPTool_GetScriptHistory>());

	// Viewport capture
	RegisterTool(MakeShared<FMCPTool_CaptureViewport>());

	// Blueprint tools
	RegisterTool(MakeShared<FMCPTool_BlueprintQuery>());
	RegisterTool(MakeShared<FMCPTool_BlueprintModify>());
	RegisterTool(MakeShared<FMCPTool_AnimBlueprintModify>());
	RegisterTool(MakeShared<FMCPTool_MontageModify>());

	// Asset tools
	RegisterTool(MakeShared<FMCPTool_AssetSearch>());
	RegisterTool(MakeShared<FMCPTool_AssetDependencies>());
	RegisterTool(MakeShared<FMCPTool_AssetReferencers>());

	// Enhanced Input tools
	RegisterTool(MakeShared<FMCPTool_EnhancedInput>());

	// Character tools
	RegisterTool(MakeShared<FMCPTool_Character>());
	RegisterTool(MakeShared<FMCPTool_CharacterData>());

	// Material and Asset tools
	RegisterTool(MakeShared<FMCPTool_Material>());
	RegisterTool(MakeShared<FMCPTool_Asset>());

	// Level management tools
	RegisterTool(MakeShared<FMCPTool_OpenLevel>());
	RegisterTool(MakeShared<FMCPTool_LevelQuery>());

	// Control Rig tools
	RegisterTool(MakeShared<FMCPTool_ControlRig>());

	// Retargeting tools
	RegisterTool(MakeShared<FMCPTool_Retarget>());

	// Blend space tools
	RegisterTool(MakeShared<FMCPTool_BlendSpace>());

	// Asset import/export tools
	RegisterTool(MakeShared<FMCPTool_AssetImport>());

	// Widget Blueprint tools
	RegisterTool(MakeShared<FMCPTool_Widget>());

	// Animation track editing tools
	RegisterTool(MakeShared<FMCPTool_AnimEdit>());

	// Gameplay debug tools (PIE automation, input injection, capture)
	RegisterTool(MakeShared<FMCPTool_GameplayDebug>());

	// Sequencer / Take Recorder tools
	RegisterTool(MakeShared<FMCPTool_Sequencer>());

	// Create and register async task queue tools
	// Task queue takes a raw pointer since the registry always outlives it
	TaskQueue = MakeShared<FMCPTaskQueue>(this);

	// Wire up execute_script to use the task queue for async execution
	// This allows script execution to handle permission dialogs without timing out
	if (TSharedPtr<IMCPTool>* ExecuteScriptToolPtr = Tools.Find(TEXT("execute_script")))
	{
		// Safe: we registered this exact type above as "execute_script"
		if (FMCPTool_ExecuteScript* ExecuteScriptTool = static_cast<FMCPTool_ExecuteScript*>(ExecuteScriptToolPtr->Get()))
		{
			ExecuteScriptTool->SetTaskQueue(TaskQueue);
			UE_LOG(LogUnrealClaude, Log, TEXT("  Wired up execute_script to task queue for async execution"));
		}
	}

	RegisterTool(MakeShared<FMCPTool_TaskSubmit>(TaskQueue));
	RegisterTool(MakeShared<FMCPTool_TaskStatus>(TaskQueue));
	RegisterTool(MakeShared<FMCPTool_TaskResult>(TaskQueue));
	RegisterTool(MakeShared<FMCPTool_TaskList>(TaskQueue));
	RegisterTool(MakeShared<FMCPTool_TaskCancel>(TaskQueue));

	UE_LOG(LogUnrealClaude, Log, TEXT("Registered %d MCP tools"), Tools.Num());
}

void FMCPToolRegistry::RegisterTool(TSharedPtr<IMCPTool> Tool)
{
	if (!Tool.IsValid())
	{
		return;
	}

	FMCPToolInfo Info = Tool->GetInfo();
	if (Info.Name.IsEmpty())
	{
		UE_LOG(LogUnrealClaude, Warning, TEXT("Cannot register tool with empty name"));
		return;
	}

	if (Tools.Contains(Info.Name))
	{
		UE_LOG(LogUnrealClaude, Warning, TEXT("Tool '%s' is already registered, replacing"), *Info.Name);
	}

	Tools.Add(Info.Name, Tool);
	UE_LOG(LogUnrealClaude, Log, TEXT("  Registered tool: %s"), *Info.Name);
}

void FMCPToolRegistry::UnregisterTool(const FString& ToolName)
{
	if (Tools.Remove(ToolName) > 0)
	{
		InvalidateToolCache();
		UE_LOG(LogUnrealClaude, Log, TEXT("Unregistered tool: %s"), *ToolName);
	}
}

void FMCPToolRegistry::InvalidateToolCache()
{
	bCacheValid = false;
	CachedToolInfo.Empty();
}

TArray<FMCPToolInfo> FMCPToolRegistry::GetAllTools() const
{
	// Return cached result if valid
	if (bCacheValid)
	{
		return CachedToolInfo;
	}

	// Rebuild cache
	CachedToolInfo.Empty(Tools.Num());
	for (const auto& Pair : Tools)
	{
		if (Pair.Value.IsValid())
		{
			CachedToolInfo.Add(Pair.Value->GetInfo());
		}
	}
	bCacheValid = true;

	return CachedToolInfo;
}

FMCPToolResult FMCPToolRegistry::ExecuteTool(const FString& ToolName, const TSharedRef<FJsonObject>& Params)
{
	TSharedPtr<IMCPTool>* FoundTool = Tools.Find(ToolName);
	if (!FoundTool || !FoundTool->IsValid())
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Tool '%s' not found"), *ToolName));
	}

	UE_LOG(LogUnrealClaude, Log, TEXT("Executing MCP tool: %s"), *ToolName);

	// Execute on game thread to ensure safe access to engine objects
	FMCPToolResult Result;

	if (IsInGameThread())
	{
		Result = (*FoundTool)->Execute(Params);
	}
	else
	{
		// If called from non-game thread, dispatch to game thread and wait with timeout
		// Use shared pointers for all state to avoid use-after-free if timeout occurs
		TSharedPtr<FMCPToolResult> SharedResult = MakeShared<FMCPToolResult>();
		TSharedPtr<FEvent, ESPMode::ThreadSafe> CompletionEvent = MakeShareable(FPlatformProcess::GetSynchEventFromPool(),
			[](FEvent* Event) { FPlatformProcess::ReturnSynchEventToPool(Event); });
		TSharedPtr<TAtomic<bool>, ESPMode::ThreadSafe> bTaskCompleted = MakeShared<TAtomic<bool>, ESPMode::ThreadSafe>(false);

		// Copy the TSharedPtr by value to avoid capturing a raw pointer into the TMap
		TSharedPtr<IMCPTool> ToolPtr = *FoundTool;
		AsyncTask(ENamedThreads::GameThread, [SharedResult, ToolPtr, Params, CompletionEvent, bTaskCompleted]()
		{
			*SharedResult = ToolPtr->Execute(Params);
			*bTaskCompleted = true;
			CompletionEvent->Trigger();
		});

		// Wait with timeout to prevent indefinite hangs
		const uint32 TimeoutMs = UnrealClaudeConstants::MCPServer::GameThreadTimeoutMs;
		const bool bSignaled = CompletionEvent->Wait(TimeoutMs);

		if (!bSignaled || !(*bTaskCompleted))
		{
			UE_LOG(LogUnrealClaude, Error, TEXT("Tool '%s' execution timed out after %d ms"), *ToolName, TimeoutMs);
			return FMCPToolResult::Error(FString::Printf(TEXT("Tool execution timed out after %d seconds"), TimeoutMs / 1000));
		}

		// Copy result from shared storage
		Result = *SharedResult;
	}

	// Check for unknown parameters and append warnings
	{
		FMCPToolInfo ToolInfo = (*FoundTool)->GetInfo();
		TSet<FString> DeclaredParams;
		for (const FMCPToolParameter& P : ToolInfo.Parameters)
		{
			DeclaredParams.Add(P.Name);
		}

		TArray<FString> UnknownParams;
		for (const auto& KV : Params->Values)
		{
			if (!DeclaredParams.Contains(KV.Key))
			{
				UnknownParams.Add(KV.Key);
			}
		}

		if (UnknownParams.Num() > 0)
		{
			FString Warning = FString::Printf(TEXT("\nWARNING: Unknown parameter%s ignored: %s. Valid params: "),
				UnknownParams.Num() > 1 ? TEXT("s") : TEXT(""),
				*FString::Join(UnknownParams, TEXT(", ")));

			TArray<FString> ValidNames;
			for (const FMCPToolParameter& P : ToolInfo.Parameters)
			{
				ValidNames.Add(P.Name);
			}
			Warning += FString::Join(ValidNames, TEXT(", "));

			Result.Message += Warning;
			UE_LOG(LogUnrealClaude, Warning, TEXT("Tool '%s': unknown params: %s"),
				*ToolName, *FString::Join(UnknownParams, TEXT(", ")));
		}
	}

	UE_LOG(LogUnrealClaude, Log, TEXT("Tool '%s' execution %s: %s"),
		*ToolName,
		Result.bSuccess ? TEXT("succeeded") : TEXT("failed"),
		*Result.Message);

	return Result;
}

bool FMCPToolRegistry::HasTool(const FString& ToolName) const
{
	return Tools.Contains(ToolName);
}
