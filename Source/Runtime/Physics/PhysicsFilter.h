#pragma once
#include <PxPhysicsAPI.h>

// UE ECollisionChannel의 축약판. 비트 하나가 타입 하나다.
enum class EPhysicsObjectType : uint32
{
	WorldStatic  = 1u << 0,   // 바닥, 벽
	WorldDynamic = 1u << 1,   // 움직이는 소품
	Pawn         = 1u << 2,   // 캐릭터 캡슐
	PhysicsBody  = 1u << 3,   // 래그돌 본, 떨어지는 박스
	Vehicle      = 1u << 4,   // 차체
};

// PxFilterData::word3에 넣는 옵션 비트
namespace EPhysicsFilterFlag
{
	constexpr uint32 NotifyContact = 1u << 0;   // OnComponentHit 받기
}

// word0 = 내 타입, word1 = 내가 막는 타입들, word2 = 예약(래그돌 자기 충돌), word3 = EPhysicsFilterFlag
inline physx::PxFilterData MakeFilterData(EPhysicsObjectType Type, uint32 BlockMask, uint32 Flags = 0)
{
	return physx::PxFilterData(static_cast<uint32>(Type), BlockMask, 0, Flags);
}

// PxSceneDesc::filterShader에 등록한다.
physx::PxFilterFlags HitoriFilterShader(
	physx::PxFilterObjectAttributes Attr0, physx::PxFilterData Data0,
	physx::PxFilterObjectAttributes Attr1, physx::PxFilterData Data1,
	physx::PxPairFlags& PairFlags, const void* ConstantBlock, physx::PxU32 ConstantBlockSize);
