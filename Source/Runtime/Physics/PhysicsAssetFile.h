#pragma once

#include "Core/EngineString.h"

class UPhysicsAsset;
class USkeletalMesh;

// UPhysicsAsset ↔ .physicsasset 파일 (JSON). Physics Asset Editor의 저장·불러오기가 이걸 쓴다.
namespace FPhysicsAssetFile
{
	inline constexpr const char* Extension = ".physicsasset";

	// 파일 경로는 Assets 상대 키나 절대 경로 둘 다 된다. 실패하면 false와 이유를 돌려준다.
	bool Read(const FString& Path, UPhysicsAsset& Asset, FString& OutError);
	bool Write(const FString& Path, UPhysicsAsset& Asset, FString& OutError);

	bool IsPhysicsAssetPath(const FString& Path);

	// 스켈레탈 메시 옆에 두는 기본 파일 경로. Foo.fbx → Foo.physicsasset (절대 경로). 메시 경로가 없으면 빈 문자열
	FString GetDefaultPathForMesh(const USkeletalMesh& Mesh);

	// 메시 옆의 .physicsasset을 읽어 새 에셋을 만든다. 파일이 없거나 읽기에 실패하면 nullptr (호출한 쪽이 소유)
	UPhysicsAsset* LoadForMesh(const USkeletalMesh& Mesh);
}
