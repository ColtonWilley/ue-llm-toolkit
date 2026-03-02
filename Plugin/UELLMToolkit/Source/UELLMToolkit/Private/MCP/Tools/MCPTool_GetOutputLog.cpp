// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_GetOutputLog.h"
#include "UnrealClaudeModule.h"
#include "UnrealClaudeConstants.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"

namespace
{

struct FParsedLogLine
{
	FString RawLine;
	FString Timestamp;
	FString Category;
	FString Verbosity;
	FString Message;
	bool bIsStructured;
};

int32 VerbosityToLevel(const FString& Verbosity)
{
	if (Verbosity.Equals(TEXT("VeryVerbose"), ESearchCase::IgnoreCase)) return 0;
	if (Verbosity.Equals(TEXT("Verbose"), ESearchCase::IgnoreCase)) return 1;
	if (Verbosity.Equals(TEXT("Log"), ESearchCase::IgnoreCase)) return 2;
	if (Verbosity.Equals(TEXT("Display"), ESearchCase::IgnoreCase)) return 2;
	if (Verbosity.Equals(TEXT("Warning"), ESearchCase::IgnoreCase)) return 3;
	if (Verbosity.Equals(TEXT("Error"), ESearchCase::IgnoreCase)) return 4;
	if (Verbosity.Equals(TEXT("Fatal"), ESearchCase::IgnoreCase)) return 5;
	return -1;
}

FParsedLogLine ParseLogLine(const FString& Line)
{
	FParsedLogLine Parsed;
	Parsed.RawLine = Line;
	Parsed.bIsStructured = false;

	if (Line.Len() == 0 || Line[0] != TEXT('['))
	{
		return Parsed;
	}

	int32 BracketCount = 0;
	int32 TimestampEnd = -1;
	for (int32 i = 0; i < Line.Len(); ++i)
	{
		if (Line[i] == TEXT(']'))
		{
			BracketCount++;
			if (BracketCount == 2)
			{
				TimestampEnd = i + 1;
				break;
			}
		}
	}

	if (TimestampEnd < 0)
	{
		return Parsed;
	}

	Parsed.Timestamp = Line.Left(TimestampEnd);

	int32 FirstColon = Line.Find(TEXT(":"), ESearchCase::CaseSensitive, ESearchDir::FromStart, TimestampEnd);
	if (FirstColon == INDEX_NONE)
	{
		return Parsed;
	}

	Parsed.Category = Line.Mid(TimestampEnd, FirstColon - TimestampEnd).TrimStartAndEnd();

	if (Parsed.Category.IsEmpty())
	{
		return Parsed;
	}

	Parsed.bIsStructured = true;

	FString AfterCategory = Line.Mid(FirstColon + 1).TrimStart();

	static const TArray<FString> KnownVerbosities = {
		TEXT("Fatal"), TEXT("Error"), TEXT("Warning"), TEXT("Display"),
		TEXT("Log"), TEXT("Verbose"), TEXT("VeryVerbose")
	};

	bool bFoundVerbosity = false;
	for (const FString& V : KnownVerbosities)
	{
		if (AfterCategory.StartsWith(V + TEXT(":"), ESearchCase::IgnoreCase))
		{
			Parsed.Verbosity = V;
			Parsed.Message = AfterCategory.Mid(V.Len() + 1).TrimStart();
			bFoundVerbosity = true;
			break;
		}
	}

	if (!bFoundVerbosity)
	{
		Parsed.Verbosity = TEXT("Log");
		Parsed.Message = AfterCategory;
	}

	return Parsed;
}

} // anonymous namespace

FMCPToolResult FMCPTool_GetOutputLog::ResolveLogFilePath(FString& OutPath) const
{
	FString ProjectLogDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectLogDir());
	FString EngineLogDir = FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Saved/Logs"));

	TArray<FString> SearchedPaths;

	// 1. Project-named log in project log directory
	{
		FString Candidate = ProjectLogDir / FApp::GetProjectName() + TEXT(".log");
		SearchedPaths.Add(Candidate);
		if (FPaths::FileExists(Candidate))
		{
			OutPath = Candidate;
			return FMCPToolResult::Success(FString());
		}
	}

	// 2. UnrealEditor.log in project log directory
	{
		FString Candidate = ProjectLogDir / TEXT("UnrealEditor.log");
		SearchedPaths.Add(Candidate);
		if (FPaths::FileExists(Candidate))
		{
			OutPath = Candidate;
			return FMCPToolResult::Success(FString());
		}
	}

	// 3. Any .log file in project log directory
	{
		TArray<FString> LogFiles;
		IFileManager::Get().FindFiles(LogFiles, *ProjectLogDir, TEXT("*.log"));
		if (LogFiles.Num() > 0)
		{
			OutPath = ProjectLogDir / LogFiles[0];
			return FMCPToolResult::Success(FString());
		}
	}

	// 4. UnrealEditor.log in engine saved logs
	{
		FString Candidate = EngineLogDir / TEXT("UnrealEditor.log");
		SearchedPaths.Add(Candidate);
		if (FPaths::FileExists(Candidate))
		{
			OutPath = Candidate;
			return FMCPToolResult::Success(FString());
		}
	}

	// 5. Any .log file in engine saved logs
	{
		TArray<FString> LogFiles;
		IFileManager::Get().FindFiles(LogFiles, *EngineLogDir, TEXT("*.log"));
		if (LogFiles.Num() > 0)
		{
			OutPath = EngineLogDir / LogFiles[0];
			return FMCPToolResult::Success(FString());
		}
	}

	FString AllPaths = FString::Join(SearchedPaths, TEXT(", "));
	return FMCPToolResult::Error(
		FString::Printf(TEXT("No log file found. Searched paths: %s. Also scanned directories: %s, %s"),
			*AllPaths, *ProjectLogDir, *EngineLogDir));
}

FMCPToolResult FMCPTool_GetOutputLog::Execute(const TSharedRef<FJsonObject>& Params)
{
	// --- Extract parameters ---
	int32 NumLines = ExtractOptionalNumber<int32>(Params, TEXT("lines"), UnrealClaudeConstants::MCPServer::DefaultOutputLogLines);
	NumLines = FMath::Clamp(NumLines, 1, UnrealClaudeConstants::MCPServer::MaxOutputLogLines);

	FString Filter = ExtractOptionalString(Params, TEXT("filter"));
	bool bSince = ExtractOptionalBool(Params, TEXT("since"), false);
	bool bCompact = ExtractOptionalBool(Params, TEXT("compact"), false);
	bool bStripTimestamps = ExtractOptionalBool(Params, TEXT("strip_timestamps"), false);
	FString MinVerbosity = ExtractOptionalString(Params, TEXT("min_verbosity"));

	TArray<FString> Categories;
	const TArray<TSharedPtr<FJsonValue>>* CategoriesArray;
	if (Params->TryGetArrayField(TEXT("categories"), CategoriesArray) && CategoriesArray)
	{
		for (const TSharedPtr<FJsonValue>& Val : *CategoriesArray)
		{
			FString S;
			if (Val.IsValid() && Val->TryGetString(S) && !S.IsEmpty())
			{
				Categories.Add(S);
			}
		}
	}

	TArray<FString> ExcludeCategories;
	const TArray<TSharedPtr<FJsonValue>>* ExcludeCategoriesArray;
	if (Params->TryGetArrayField(TEXT("exclude_categories"), ExcludeCategoriesArray) && ExcludeCategoriesArray)
	{
		for (const TSharedPtr<FJsonValue>& Val : *ExcludeCategoriesArray)
		{
			FString S;
			if (Val.IsValid() && Val->TryGetString(S) && !S.IsEmpty())
			{
				ExcludeCategories.Add(S);
			}
		}
	}

	// --- Validate min_verbosity ---
	int32 MinVerbosityLevel = -1;
	if (!MinVerbosity.IsEmpty())
	{
		MinVerbosityLevel = VerbosityToLevel(MinVerbosity);
		if (MinVerbosityLevel < 0)
		{
			return FMCPToolResult::Error(
				FString::Printf(TEXT("Invalid min_verbosity '%s'. Valid values: VeryVerbose, Verbose, Log, Display, Warning, Error, Fatal"),
					*MinVerbosity));
		}
	}

	// --- Resolve log file ---
	FString LogFilePath;
	FMCPToolResult ResolveResult = ResolveLogFilePath(LogFilePath);
	if (!ResolveResult.bSuccess)
	{
		return ResolveResult;
	}

	// --- Read content ---
	FString LogContent;
	int64 SinceOffset = 0;
	int64 NewBytes = 0;

	if (!bSince)
	{
		// Full read
		if (!FFileHelper::LoadFileToString(LogContent, *LogFilePath, FFileHelper::EHashOptions::None, FILEREAD_AllowWrite))
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Failed to read log file: %s"), *LogFilePath));
		}

		LastReadOffset = IFileManager::Get().FileSize(*LogFilePath);
		LastLogFilePath = LogFilePath;
	}
	else
	{
		// Incremental read
		if (LastLogFilePath != LogFilePath)
		{
			LastReadOffset = 0;
		}

		int64 FileSize = IFileManager::Get().FileSize(*LogFilePath);
		if (FileSize < 0)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Cannot stat log file: %s"), *LogFilePath));
		}

		if (LastReadOffset > FileSize)
		{
			LastReadOffset = 0;
		}

		SinceOffset = LastReadOffset;

		if (LastReadOffset >= FileSize)
		{
			// No new content
			TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
			ResultData->SetStringField(TEXT("log_file"), LogFilePath);
			ResultData->SetNumberField(TEXT("total_lines"), 0);
			ResultData->SetNumberField(TEXT("returned_lines"), 0);
			ResultData->SetNumberField(TEXT("cursor"), static_cast<double>(LastReadOffset));
			ResultData->SetNumberField(TEXT("since_offset"), static_cast<double>(SinceOffset));
			ResultData->SetNumberField(TEXT("new_bytes"), 0);
			ResultData->SetStringField(TEXT("content"), TEXT(""));
			return FMCPToolResult::Success(TEXT("No new log content since last read"), ResultData);
		}

		TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*LogFilePath, FILEREAD_AllowWrite));
		if (!Reader)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Failed to open log file for incremental read: %s"), *LogFilePath));
		}

		int64 BytesToRead = FileSize - LastReadOffset;
		Reader->Seek(LastReadOffset);

		TArray<uint8> Buffer;
		Buffer.SetNumUninitialized(BytesToRead);
		Reader->Serialize(Buffer.GetData(), BytesToRead);
		Reader->Close();

		// Convert to FString (UTF-8 log file)
		FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Buffer.GetData()), Buffer.Num());
		LogContent = FString(Converter.Length(), Converter.Get());

		// Discard partial first line if we're continuing from a previous read
		if (LastReadOffset > 0)
		{
			int32 FirstNewline = INDEX_NONE;
			LogContent.FindChar(TEXT('\n'), FirstNewline);
			if (FirstNewline != INDEX_NONE)
			{
				LogContent.RightChopInline(FirstNewline + 1);
			}
			else
			{
				// Entire buffer is a partial line — nothing complete to return
				LastReadOffset = FileSize;
				LastLogFilePath = LogFilePath;

				TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
				ResultData->SetStringField(TEXT("log_file"), LogFilePath);
				ResultData->SetNumberField(TEXT("total_lines"), 0);
				ResultData->SetNumberField(TEXT("returned_lines"), 0);
				ResultData->SetNumberField(TEXT("cursor"), static_cast<double>(LastReadOffset));
				ResultData->SetNumberField(TEXT("since_offset"), static_cast<double>(SinceOffset));
				ResultData->SetNumberField(TEXT("new_bytes"), BytesToRead);
				ResultData->SetStringField(TEXT("content"), TEXT(""));
				return FMCPToolResult::Success(TEXT("No complete new lines since last read"), ResultData);
			}
		}

		// Truncate at last complete line
		int32 LastNewline = INDEX_NONE;
		for (int32 i = LogContent.Len() - 1; i >= 0; --i)
		{
			if (LogContent[i] == TEXT('\n'))
			{
				LastNewline = i;
				break;
			}
		}

		if (LastNewline != INDEX_NONE)
		{
			// Calculate byte offset for cursor: we need to figure out how many bytes correspond
			// to the content up to and including the last newline. Since we may have discarded
			// the first partial line, compute from the full buffer perspective.
			FString CompleteContent = LogContent.Left(LastNewline + 1);
			FTCHARToUTF8 BackConverter(*LogContent);
			int32 TotalConvertedBytes = BackConverter.Length();

			FString TruncatedPart = LogContent.Mid(LastNewline + 1);
			FTCHARToUTF8 TruncatedConverter(*TruncatedPart);
			int32 TruncatedBytes = TruncatedConverter.Length();

			LastReadOffset = FileSize - TruncatedBytes;
			LogContent = CompleteContent;
		}
		else
		{
			// No newline at all — entire content is a partial line
			LastReadOffset = SinceOffset;
			LastLogFilePath = LogFilePath;

			TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
			ResultData->SetStringField(TEXT("log_file"), LogFilePath);
			ResultData->SetNumberField(TEXT("total_lines"), 0);
			ResultData->SetNumberField(TEXT("returned_lines"), 0);
			ResultData->SetNumberField(TEXT("cursor"), static_cast<double>(LastReadOffset));
			ResultData->SetNumberField(TEXT("since_offset"), static_cast<double>(SinceOffset));
			ResultData->SetNumberField(TEXT("new_bytes"), BytesToRead);
			ResultData->SetStringField(TEXT("content"), TEXT(""));
			return FMCPToolResult::Success(TEXT("No complete new lines since last read"), ResultData);
		}

		NewBytes = BytesToRead;
		LastLogFilePath = LogFilePath;
	}

	// --- Parse lines ---
	TArray<FString> RawLines;
	LogContent.ParseIntoArrayLines(RawLines);

	int32 TotalLinesParsed = RawLines.Num();

	TArray<FParsedLogLine> ParsedLines;
	ParsedLines.Reserve(RawLines.Num());
	for (const FString& RawLine : RawLines)
	{
		ParsedLines.Add(ParseLogLine(RawLine));
	}

	// --- Filter pipeline ---
	// Per-stage inheritance tracking for continuation lines.
	// Include filters default to false (conservative: orphan continuations excluded).
	// Exclude filters default to true (orphan continuations included).
	// No-filter defaults to true (everything passes).
	bool bTextPassed = true;
	bool bCategoryPassed = (Categories.Num() > 0) ? false : true;
	bool bVerbosityPassed = (MinVerbosityLevel >= 0) ? false : true;

	TArray<FParsedLogLine> FilteredLines;
	FilteredLines.Reserve(ParsedLines.Num());
	int32 TextFilteredCount = 0;

	for (const FParsedLogLine& Line : ParsedLines)
	{
		// (a) Text filter
		if (!Filter.IsEmpty())
		{
			if (Line.bIsStructured)
			{
				bTextPassed = Line.RawLine.Contains(Filter, ESearchCase::IgnoreCase);
			}
		}
		if (!bTextPassed) continue;
		TextFilteredCount++;

		// (b) Category include/exclude
		if (Categories.Num() > 0)
		{
			if (Line.bIsStructured)
			{
				bCategoryPassed = false;
				for (const FString& Cat : Categories)
				{
					if (Line.Category.Equals(Cat, ESearchCase::IgnoreCase))
					{
						bCategoryPassed = true;
						break;
					}
				}
			}
		}
		else if (ExcludeCategories.Num() > 0)
		{
			if (Line.bIsStructured)
			{
				bCategoryPassed = true;
				for (const FString& Cat : ExcludeCategories)
				{
					if (Line.Category.Equals(Cat, ESearchCase::IgnoreCase))
					{
						bCategoryPassed = false;
						break;
					}
				}
			}
		}
		if (!bCategoryPassed) continue;

		// (c) Verbosity level
		if (MinVerbosityLevel >= 0)
		{
			if (Line.bIsStructured)
			{
				int32 LineLevel = VerbosityToLevel(Line.Verbosity);
				bVerbosityPassed = (LineLevel >= MinVerbosityLevel);
			}
		}
		if (!bVerbosityPassed) continue;

		FilteredLines.Add(Line);
	}

	// --- Compact mode ---
	int32 DuplicatesCollapsed = 0;
	if (bCompact && FilteredLines.Num() > 0)
	{
		TArray<FParsedLogLine> CompactedLines;
		CompactedLines.Reserve(FilteredLines.Num());

		int32 i = 0;
		while (i < FilteredLines.Num())
		{
			if (!FilteredLines[i].bIsStructured)
			{
				CompactedLines.Add(FilteredLines[i]);
				i++;
				continue;
			}

			FParsedLogLine Current = FilteredLines[i];
			int32 Count = 1;

			// Collect continuation lines for the current structured line
			TArray<FParsedLogLine> CurrentContinuations;
			int32 j = i + 1;
			while (j < FilteredLines.Num() && !FilteredLines[j].bIsStructured)
			{
				CurrentContinuations.Add(FilteredLines[j]);
				j++;
			}

			// Check for consecutive duplicates
			while (j < FilteredLines.Num() && FilteredLines[j].bIsStructured
				&& FilteredLines[j].Message == Current.Message)
			{
				Count++;
				j++;
				// Skip continuation lines of duplicates
				while (j < FilteredLines.Num() && !FilteredLines[j].bIsStructured)
				{
					j++;
				}
			}

			if (Count > 1)
			{
				Current.Message += FString::Printf(TEXT(" (x%d)"), Count);
				Current.RawLine += FString::Printf(TEXT(" (x%d)"), Count);
				DuplicatesCollapsed += (Count - 1);
			}

			CompactedLines.Add(Current);
			CompactedLines.Append(CurrentContinuations);
			i = j;
		}

		FilteredLines = MoveTemp(CompactedLines);
	}

	// --- Take last N lines ---
	int32 StartIndex = FMath::Max(0, FilteredLines.Num() - NumLines);
	TArray<FParsedLogLine> ResultLines;
	ResultLines.Reserve(FilteredLines.Num() - StartIndex);
	for (int32 i = StartIndex; i < FilteredLines.Num(); ++i)
	{
		ResultLines.Add(FilteredLines[i]);
	}

	// --- Build output strings ---
	TArray<FString> OutputStrings;
	OutputStrings.Reserve(ResultLines.Num());

	for (const FParsedLogLine& Line : ResultLines)
	{
		if (bStripTimestamps && Line.bIsStructured)
		{
			if (Line.Verbosity.Equals(TEXT("Log"), ESearchCase::IgnoreCase))
			{
				OutputStrings.Add(FString::Printf(TEXT("%s: %s"), *Line.Category, *Line.Message));
			}
			else
			{
				OutputStrings.Add(FString::Printf(TEXT("%s: %s: %s"), *Line.Category, *Line.Verbosity, *Line.Message));
			}
		}
		else
		{
			OutputStrings.Add(Line.RawLine);
		}
	}

	FString LogOutput = FString::Join(OutputStrings, TEXT("\n"));

	// --- Build result JSON ---
	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("log_file"), LogFilePath);
	ResultData->SetNumberField(TEXT("total_lines"), TotalLinesParsed);
	ResultData->SetNumberField(TEXT("returned_lines"), ResultLines.Num());
	ResultData->SetNumberField(TEXT("cursor"), static_cast<double>(LastReadOffset));

	if (!Filter.IsEmpty())
	{
		ResultData->SetStringField(TEXT("filter"), Filter);
		ResultData->SetNumberField(TEXT("filtered_lines"), TextFilteredCount);
	}

	if (bSince)
	{
		ResultData->SetNumberField(TEXT("since_offset"), static_cast<double>(SinceOffset));
		ResultData->SetNumberField(TEXT("new_bytes"), static_cast<double>(NewBytes));
	}

	if (Categories.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> CatArray;
		for (const FString& Cat : Categories)
		{
			CatArray.Add(MakeShared<FJsonValueString>(Cat));
		}
		ResultData->SetArrayField(TEXT("categories"), CatArray);
	}

	if (ExcludeCategories.Num() > 0 && Categories.Num() == 0)
	{
		TArray<TSharedPtr<FJsonValue>> ExcArray;
		for (const FString& Cat : ExcludeCategories)
		{
			ExcArray.Add(MakeShared<FJsonValueString>(Cat));
		}
		ResultData->SetArrayField(TEXT("exclude_categories"), ExcArray);
	}

	if (!MinVerbosity.IsEmpty())
	{
		ResultData->SetStringField(TEXT("min_verbosity"), MinVerbosity);
	}

	if (bCompact)
	{
		ResultData->SetBoolField(TEXT("compact"), true);
		ResultData->SetNumberField(TEXT("duplicates_collapsed"), DuplicatesCollapsed);
	}

	ResultData->SetStringField(TEXT("content"), LogOutput);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Retrieved %d log lines from %s"), ResultLines.Num(), *FPaths::GetCleanFilename(LogFilePath)),
		ResultData
	);
}
