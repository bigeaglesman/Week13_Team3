#pragma once

#include "Core/Types.h"

// 충돌 오브젝트 채널 (언리얼 ECollisionChannel 대응)
enum class ECollisionChannel : uint8
{
	ECC_WorldStatic = 0,
	ECC_WorldDynamic,
	ECC_Pawn,
	ECC_PhysicsBody,
	ECC_MAX
};

// 채널 간 충돌 응답 (언리얼 ECollisionResponse 대응)
enum class ECollisionResponse : uint8
{
	ECR_Ignore = 0,
	ECR_Overlap,
	ECR_Block
};

// PxFilterData 비트마스크용 헬퍼
inline constexpr uint32 CollisionChannelToBit(ECollisionChannel Channel)
{
	return 1u << static_cast<uint32>(Channel);
}

inline constexpr uint32 ECC_AllChannelsMask =
CollisionChannelToBit(ECollisionChannel::ECC_WorldStatic) |
CollisionChannelToBit(ECollisionChannel::ECC_WorldDynamic) |
CollisionChannelToBit(ECollisionChannel::ECC_Pawn) |
CollisionChannelToBit(ECollisionChannel::ECC_PhysicsBody);