#pragma once

#include "Math/Vector.h"
#include "Render/Vertex.h"

// 시뮬레이션 정점 하나가 따라가는 뼈와 가중치. 렌더 정점의 FSkinWeight에서 복사한다.
// 고정된 입자(InverseMass 0)는 이 가중치로 스키닝한 위치에 붙어 있다. UE 4.27의 FClothVertBoneData 자리다.
struct FClothVertBoneData
{
	int32  NumInfluences = 0;
	uint16 BoneIndices[MaxBoneInfluences] = {};   // 전체 뼈 배열(RefSkeleton) 번호
	float  BoneWeights[MaxBoneInfluences] = {};
};

// 천 하나의 시뮬레이션용 메시. 렌더 메시에서 UV·노멀 경계로 갈라진 정점을 하나로 합친(용접한) 것이다.
// 모든 위치는 바인드 포즈의 컴포넌트 공간이다. UE 4.27의 FClothPhysicalMeshData 자리다.
struct FClothPhysicalMeshData
{
	TArray<FVector> Vertices;
	TArray<FVector> Normals;
	TArray<uint32>  Indices;          // 삼각형 3개씩

	// 입자가 스키닝 위치에서 벗어날 수 있는 최대 거리. 0이면 완전히 고정된다.
	// UE에서는 붓으로 칠하는 WeightMaps[MaxDistance]다. 여기서는 FClothingAssetFactory가 자동으로 정한다.
	TArray<float> MaxDistances;

	TArray<float>              InverseMasses;   // 0이면 고정 입자. CalculateInverseMasses가 채운다
	TArray<FClothVertBoneData> BoneData;

	int32 MaxBoneWeights = 0;   // 정점 하나의 최대 뼈 영향 개수
	int32 NumFixedVerts = 0;    // InverseMass가 0인 정점 수

	// MaxDistance가 이보다 작으면 고정 입자로 본다. (UE와 같은 판정)
	static constexpr float FixedMaxDistance = 1.0e-4f;

	void Reset(int32 NumVerts);

	int32 GetNumVerts() const { return Vertices.Num(); }
	bool IsFixed(int32 VertexIndex) const { return MaxDistances[VertexIndex] < FixedMaxDistance; }

	// 삼각형 넓이로 정점 질량을 나눠 갖고, 평균 질량이 1이 되게 맞춘 뒤 역수를 저장한다. 고정 정점은 0.
	// UE의 FClothPhysicalMeshData::CalculateInverseMasses와 같은 방식이다.
	void CalculateInverseMasses();
};
