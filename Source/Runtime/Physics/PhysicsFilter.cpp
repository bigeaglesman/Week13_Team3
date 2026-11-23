#include "EnginePCH.h"
#include "Physics/PhysicsFilter.h"

using namespace physx;

PxFilterFlags HitoriFilterShader(
	PxFilterObjectAttributes Attr0, PxFilterData Data0,
	PxFilterObjectAttributes Attr1, PxFilterData Data1,
	PxPairFlags& PairFlags, const void*, PxU32)
{
	if (PxFilterObjectIsTrigger(Attr0) || PxFilterObjectIsTrigger(Attr1))
	{
		PairFlags = PxPairFlag::eTRIGGER_DEFAULT;
		return PxFilterFlag::eDEFAULT;
	}

	// 양쪽 모두 상대를 막아야 충돌한다 (UE: 두 응답 중 약한 쪽을 따른다)
	const bool bABlocksB = (Data0.word1 & Data1.word0) != 0;
	const bool bBBlocksA = (Data1.word1 & Data0.word0) != 0;
	if (!bABlocksB || !bBBlocksA)
	{
		return PxFilterFlag::eSUPPRESS;
	}

	PairFlags = PxPairFlag::eCONTACT_DEFAULT;
	if ((Data0.word3 | Data1.word3) & EPhysicsFilterFlag::NotifyContact)
	{
		PairFlags |= PxPairFlag::eNOTIFY_TOUCH_FOUND | PxPairFlag::eNOTIFY_CONTACT_POINTS;   // onContact 호출 조건
	}
	return PxFilterFlag::eDEFAULT;
}
