#include "pch.h"
#include "FBXAnimationCache.h"
#include "Source/Runtime/Engine/Animation/AnimSequence.h"
#include "Source/Runtime/Engine/Animation/AnimDateModel.h"
#include "ObjectFactory.h"
#include "WindowsBinReader.h"
#include "WindowsBinWriter.h"
#include "PathUtils.h"
#include <filesystem>

bool FBXAnimationCache::TryLoadAnimationsFromCache(const FString& NormalizedPath, TArray<UAnimSequence*>& OutAnimations)
{
#ifdef USE_OBJ_CACHE
	OutAnimations.Empty();

	FString CacheBasePath = ConvertDataPathToCachePath(NormalizedPath);
	FString AnimCacheDir = CacheBasePath + "_Anims";

	if (!std::filesystem::exists(AnimCacheDir) || !std::filesystem::is_directory(AnimCacheDir))
	{
		return false;
	}

	bool bLoadedAny = false;
	for (const auto& Entry : std::filesystem::directory_iterator(AnimCacheDir))
	{
		if (!Entry.is_regular_file())
		{
			continue;
		}

		if (Entry.path().extension() != ".bin")
		{
			continue;
		}

		FString CachePath = NormalizePath(Entry.path().string());
		if (UAnimSequence* CachedAnim = LoadAnimationFromCache(CachePath))
		{
			FString AnimStackName = CachedAnim->ObjectName.ToString();
			FString AnimKey = NormalizedPath + "_" + AnimStackName;

			if (!RESOURCE.Add<UAnimSequence>(AnimKey, CachedAnim))
			{
				UE_LOG("Animation cache already registered: %s", AnimKey.c_str());
			}

			OutAnimations.Add(CachedAnim);
			bLoadedAny = true;
		}
	}

	if (!bLoadedAny)
	{
		UE_LOG("Animation cache directory '%s' did not contain valid .anim.bin files", AnimCacheDir.c_str());
	}

	return bLoadedAny;
#else
	(void)NormalizedPath;
	(void)OutAnimations;
	return false;
#endif
}

bool FBXAnimationCache::SaveAnimationToCache(UAnimSequence* Animation, const FString& CachePath)
{
	if (!Animation)
	{
		return false;
	}

	try
	{
		FWindowsBinWriter Writer(CachePath);

		UAnimDataModel* DataModel = Animation->GetDataModel();
		if (!DataModel)
		{
			return false;
		}

		// Write animation name first
		FString AnimName = Animation->ObjectName.ToString();
		Serialization::WriteString(Writer, AnimName);

		// Write metadata
		float PlayLength = DataModel->GetPlayLength();
		int32 FrameRate = DataModel->GetFrameRate();
		int32 NumberOfFrames = DataModel->GetNumberOfFrames();
		int32 NumberOfKeys = DataModel->GetNumberOfKeys();

		Writer << PlayLength;
		Writer << FrameRate;
		Writer << NumberOfFrames;
		Writer << NumberOfKeys;

		// Write Non-skeleton correction transform
		FTransform CachedNonSkeletonCorrection = Animation->GetNonSkeletonCorrection();
		FVector ACTranslation = CachedNonSkeletonCorrection.Translation;
		FQuat ACRotation = CachedNonSkeletonCorrection.Rotation;
		FVector ACScale = CachedNonSkeletonCorrection.Scale3D;
		Writer << ACTranslation.X;
		Writer << ACTranslation.Y;
		Writer << ACTranslation.Z;
		Writer << ACRotation.X;
		Writer << ACRotation.Y;
		Writer << ACRotation.Z;
		Writer << ACRotation.W;
		Writer << ACScale.X;
		Writer << ACScale.Y;
		Writer << ACScale.Z;

		// Write bone tracks
		TArray<FBoneAnimationTrack>& Tracks = DataModel->GetBoneAnimationTracks();
		uint32 NumTracks = (uint32)Tracks.Num();
		Writer << NumTracks;

		for (FBoneAnimationTrack& Track : Tracks)
		{
			// Write bone name
			FString BoneName = Track.Name.ToString();
			Serialization::WriteString(Writer, BoneName);

			// Write position keys
			Serialization::WriteArray(Writer, Track.InternalTrack.PosKeys);

			// Write rotation keys (FQuat는 직접 직렬화)
			TArray<FQuat>& RotKeys = Track.InternalTrack.RotKeys;
			uint32 NumRotKeys = (uint32)RotKeys.Num();
			Writer << NumRotKeys;
			for (FQuat& Rot : RotKeys)
			{
				Writer << Rot.X << Rot.Y << Rot.Z << Rot.W;
			}

			// Write scale keys
			Serialization::WriteArray(Writer, Track.InternalTrack.ScaleKeys);
		}

		Writer.Close();
		UE_LOG("Animation saved to cache: %s", CachePath.c_str());
		return true;
	}
	catch (const std::exception& e)
	{
		UE_LOG("Failed to save animation cache: %s", e.what());
		return false;
	}
}

UAnimSequence* FBXAnimationCache::LoadAnimationFromCache(const FString& CachePath)
{
	try
	{
		FWindowsBinReader Reader(CachePath);
		if (!Reader.IsOpen())
		{
			return nullptr;
		}

		// Create new animation sequence
		UAnimSequence* Animation = NewObject<UAnimSequence>();

		// Read animation name first
		FString AnimName;
		Serialization::ReadString(Reader, AnimName);
		Animation->ObjectName = FName(AnimName);

		UAnimDataModel* DataModel = Animation->GetDataModel();
		if (!DataModel)
		{
			return nullptr;
		}

		// Read metadata
		float PlayLength;
		int32 FrameRate;
		int32 NumberOfFrames;
		int32 NumberOfKeys;

		Reader << PlayLength;
		Reader << FrameRate;
		Reader << NumberOfFrames;
		Reader << NumberOfKeys;

		FTransform NonSkeletonCorrection;
		Reader << NonSkeletonCorrection.Translation.X;
		Reader << NonSkeletonCorrection.Translation.Y;
		Reader << NonSkeletonCorrection.Translation.Z;
		Reader << NonSkeletonCorrection.Rotation.X;
		Reader << NonSkeletonCorrection.Rotation.Y;
		Reader << NonSkeletonCorrection.Rotation.Z;
		Reader << NonSkeletonCorrection.Rotation.W;
		Reader << NonSkeletonCorrection.Scale3D.X;
		Reader << NonSkeletonCorrection.Scale3D.Y;
		Reader << NonSkeletonCorrection.Scale3D.Z;

		Animation->SetNonSkeletonCorrection(NonSkeletonCorrection);

		DataModel->SetPlayLength(PlayLength);
		DataModel->SetFrameRate(FrameRate);
		DataModel->SetNumberOfFrames(NumberOfFrames);
		DataModel->SetNumberOfKeys(NumberOfKeys);

		// Read bone tracks
		uint32 NumTracks;
		Reader << NumTracks;

		for (uint32 i = 0; i < NumTracks; ++i)
		{
			// Read bone name
			FString BoneNameStr;
			Serialization::ReadString(Reader, BoneNameStr);
			FName BoneName(BoneNameStr);

			// Add bone track
			DataModel->AddBoneTrack(BoneName);

			// Read position keys
			TArray<FVector> PosKeys;
			Serialization::ReadArray(Reader, PosKeys);

			// Read rotation keys
			uint32 NumRotKeys;
			Reader << NumRotKeys;
			TArray<FQuat> RotKeys;
			RotKeys.resize(NumRotKeys);
			for (uint32 j = 0; j < NumRotKeys; ++j)
			{
				float X, Y, Z, W;
				Reader << X << Y << Z << W;
				RotKeys[j] = FQuat(X, Y, Z, W);
			}

			// Read scale keys
			TArray<FVector> ScaleKeys;
			Serialization::ReadArray(Reader, ScaleKeys);

			// Set keys in data model
			DataModel->SetBoneTrackKeys(BoneName, PosKeys, RotKeys, ScaleKeys);
		}

		Reader.Close();
		UE_LOG("Animation loaded from cache: %s (Name: %s)", CachePath.c_str(), AnimName.c_str());
		return Animation;
	}
	catch (const std::exception& e)
	{
		UE_LOG("Failed to load animation from cache: %s", e.what());
		return nullptr;
	}
}
