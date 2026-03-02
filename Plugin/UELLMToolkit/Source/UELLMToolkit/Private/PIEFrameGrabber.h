// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeCounter.h"

class FFrameGrabber;
class FSceneViewport;

class FPIEFrameGrabber
{
public:
	FPIEFrameGrabber();
	~FPIEFrameGrabber();

	void StartCapture(TSharedRef<FSceneViewport> InViewport, int32 InCaptureEveryNFrames, const FString& InOutputDir);
	void StopCapture();
	bool IsCapturing() const { return bCapturing; }

	int32 GetCapturedFrameCount() const;
	TArray<FString> GetCapturedFilePaths() const;

private:
	void OnEndFrame();
	static void CompressAndSave(TArray<FColor> ColorBuffer, FIntPoint BufferSize, uint64 FrameNumber, double GameTime, FString OutputDir, FThreadSafeCounter* OutstandingTasks, FCriticalSection* PathLock, TArray<FString>* PathList, FThreadSafeCounter* CountPtr);

	bool bCapturing = false;
	int32 CaptureEveryNFrames = 2;
	uint64 FrameCounter = 0;
	FString OutputDir;

	TUniquePtr<FFrameGrabber> EngineGrabber;
	FDelegateHandle EndFrameHandle;

	FThreadSafeCounter OutstandingTasks;
	FThreadSafeCounter CapturedCount;
	TArray<FString> CapturedFilePaths;
	FCriticalSection CapturedLock;
};
