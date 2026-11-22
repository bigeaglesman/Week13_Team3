#include "EnginePCH.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/BodySetup.h"

UPhysicsAsset::~UPhysicsAsset()
{
	for (UBodySetup* Setup : BodySetups)
	{
		delete Setup;
	}
	BodySetups.Empty();
}

int32 UPhysicsAsset::FindBodyIndex(const FString& BoneName) const
{
	for (int32 i = 0; i < BodySetups.Num(); ++i)
	{
		if (BodySetups[i] && BodySetups[i]->BoneName == BoneName)
		{
			return i;
		}
	}
	return -1;
}
