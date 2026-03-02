// Copyright Natali Caggiano. All Rights Reserved.

#include "PIEFrameGrabber.h"
#include "UnrealClaudeModule.h"
#include "FrameGrabber.h"
#include "Slate/SceneViewport.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/CoreDelegates.h"
#include "HAL/PlatformFileManager.h"
#include "Async/Async.h"
#include "Editor.h"

FPIEFrameGrabber::FPIEFrameGrabber()
{
}

FPIEFrameGrabber::~FPIEFrameGrabber()
{
	StopCapture();
}

void FPIEFrameGrabber::StartCapture(TSharedRef<FSceneViewport> InViewport, int32 InCaptureEveryNFrames, const FString& InOutputDir)
{
	if (bCapturing)
	{
		StopCapture();
	}

	CaptureEveryNFrames = FMath::Max(1, InCaptureEveryNFrames);
	OutputDir = InOutputDir;
	FrameCounter = 0;
	CapturedCount.Reset();
	{
		FScopeLock Lock(&CapturedLock);
		CapturedFilePaths.Empty();
	}
	bCapturing = true;

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*OutputDir))
	{
		PlatformFile.CreateDirectoryTree(*OutputDir);
	}

	FIntPoint DesiredSize(1024, 576);
	EngineGrabber = MakeUnique<FFrameGrabber>(InViewport, DesiredSize, PF_B8G8R8A8, 3);
	EngineGrabber->StartCapturingFrames();

	EndFrameHandle = FCoreDelegates::OnEndFrame.AddRaw(this, &FPIEFrameGrabber::OnEndFrame);

	UE_LOG(LogUnrealClaude, Log, TEXT("PIEFrameGrabber: Started async capture (every %d frames, output: %s)"),
		CaptureEveryNFrames, *OutputDir);
}

void FPIEFrameGrabber::StopCapture()
{
	if (!bCapturing)
	{
		return;
	}

	bCapturing = false;

	if (EndFrameHandle.IsValid())
	{
		FCoreDelegates::OnEndFrame.Remove(EndFrameHandle);
		EndFrameHandle.Reset();
	}

	if (EngineGrabber)
	{
		EngineGrabber->StopCapturingFrames();

		TArray<FCapturedFrameData> Remaining = EngineGrabber->GetCapturedFrames();
		for (FCapturedFrameData& Frame : Remaining)
		{
			double GameTime = 0.0;
			if (GEngine)
			{
				for (const FWorldContext& Context : GEngine->GetWorldContexts())
				{
					if (Context.WorldType == EWorldType::PIE && Context.World())
					{
						GameTime = Context.World()->GetTimeSeconds();
						break;
					}
				}
			}

			OutstandingTasks.Increment();
			CompressAndSave(MoveTemp(Frame.ColorBuffer), Frame.BufferSize, GFrameNumber, GameTime,
				OutputDir, &OutstandingTasks, &CapturedLock, &CapturedFilePaths, &CapturedCount);
		}

		EngineGrabber->Shutdown();
		EngineGrabber.Reset();
	}

	while (OutstandingTasks.GetValue() > 0)
	{
		FPlatformProcess::Sleep(0.01f);
	}

	UE_LOG(LogUnrealClaude, Log, TEXT("PIEFrameGrabber: Stopped capture (%d frames captured)"), CapturedCount.GetValue());
}

int32 FPIEFrameGrabber::GetCapturedFrameCount() const
{
	return CapturedCount.GetValue();
}

TArray<FString> FPIEFrameGrabber::GetCapturedFilePaths() const
{
	FScopeLock Lock(const_cast<FCriticalSection*>(&CapturedLock));
	return CapturedFilePaths;
}

void FPIEFrameGrabber::OnEndFrame()
{
	if (!bCapturing || !EngineGrabber)
	{
		return;
	}

	FrameCounter++;
	if ((FrameCounter % CaptureEveryNFrames) == 0)
	{
		EngineGrabber->CaptureThisFrame(FFramePayloadPtr());
	}

	TArray<FCapturedFrameData> Frames = EngineGrabber->GetCapturedFrames();
	for (FCapturedFrameData& Frame : Frames)
	{
		double GameTime = 0.0;
		if (GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType == EWorldType::PIE && Context.World())
				{
					GameTime = Context.World()->GetTimeSeconds();
					break;
				}
			}
		}

		uint64 CaptureFrame = GFrameNumber;

		OutstandingTasks.Increment();
		CompressAndSave(MoveTemp(Frame.ColorBuffer), Frame.BufferSize, CaptureFrame, GameTime,
			OutputDir, &OutstandingTasks, &CapturedLock, &CapturedFilePaths, &CapturedCount);
	}
}

void FPIEFrameGrabber::CompressAndSave(TArray<FColor> ColorBuffer, FIntPoint BufferSize, uint64 FrameNumber, double GameTime, FString InOutputDir, FThreadSafeCounter* InOutstandingTasks, FCriticalSection* InPathLock, TArray<FString>* InPathList, FThreadSafeCounter* InCountPtr)
{
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [ColorBuffer = MoveTemp(ColorBuffer), BufferSize, FrameNumber, GameTime, InOutputDir, InOutstandingTasks, InPathLock, InPathList, InCountPtr]()
	{
		constexpr int32 JPEGQuality = 70;

		IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
		TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);

		if (ImageWrapper.IsValid() && ImageWrapper->SetRaw(ColorBuffer.GetData(), ColorBuffer.Num() * sizeof(FColor),
			BufferSize.X, BufferSize.Y, ERGBFormat::BGRA, 8))
		{
			TArray64<uint8> CompressedData = ImageWrapper->GetCompressed(JPEGQuality);
			if (CompressedData.Num() > 0)
			{
				FString Filename = FString::Printf(TEXT("f%06llu_t%.3f.jpg"), FrameNumber, GameTime);
				FString FullPath = InOutputDir / Filename;

				FFileHelper::SaveArrayToFile(
					TArrayView64<const uint8>(CompressedData.GetData(), CompressedData.Num()),
					*FullPath);

				FScopeLock Lock(InPathLock);
				InPathList->Add(FullPath);
				InCountPtr->Increment();
			}
		}

		InOutstandingTasks->Decrement();
	});
}
