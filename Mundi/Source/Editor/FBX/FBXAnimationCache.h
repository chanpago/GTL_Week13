#pragma once
#include "String.h"

class UAnimSequence;

class FBXAnimationCache
{
public:
	// Try to load animations from cache directory
	static bool TryLoadAnimationsFromCache(const FString& NormalizedPath, TArray<UAnimSequence*>& OutAnimations);

	// Save a single animation to cache file
	static bool SaveAnimationToCache(UAnimSequence* Animation, const FString& CachePath);

	// Load a single animation from cache file
	static UAnimSequence* LoadAnimationFromCache(const FString& CachePath);
};
