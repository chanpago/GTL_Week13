#pragma once
#include "Object.h"

class USkeletalMeshComponent;
class UAnimSequenceBase;

class UAnimNotify : public UObject
{
public:
	DECLARE_CLASS(UAnimNotify, UObject)

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation);   
};
