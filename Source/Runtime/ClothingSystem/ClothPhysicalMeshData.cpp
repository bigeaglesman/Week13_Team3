#include "EnginePCH.h"
#include "ClothingSystem/ClothPhysicalMeshData.h"

void FClothPhysicalMeshData::Reset(int32 NumVerts)
{
	Vertices.Init(FVector::ZeroVector, NumVerts);
	Normals.Init(FVector::ZeroVector, NumVerts);
	MaxDistances.Init(0.0f, NumVerts);
	InverseMasses.Init(0.0f, NumVerts);
	BoneData.Init(FClothVertBoneData(), NumVerts);
	Indices.Reset();
	MaxBoneWeights = 0;
	NumFixedVerts = 0;
}

void FClothPhysicalMeshData::CalculateInverseMasses()
{
	const int32 NumVerts = GetNumVerts();
	InverseMasses.Init(0.0f, NumVerts);

	// 1) 삼각형 넓이의 1/3씩을 세 꼭짓점에 질량으로 나눠 준다. 넓은 곳의 입자가 더 무겁다.
	for (int32 I = 0; I + 2 < Indices.Num(); I += 3)
	{
		const uint32 A = Indices[I], B = Indices[I + 1], C = Indices[I + 2];
		const float TriArea = 0.5f * FVector::Cross(Vertices[B] - Vertices[A], Vertices[C] - Vertices[A]).Size();
		const float ThirdArea = TriArea / 3.0f;
		InverseMasses[A] += ThirdArea;
		InverseMasses[B] += ThirdArea;
		InverseMasses[C] += ThirdArea;
	}

	// 2) 고정 정점은 0으로 만들고, 나머지 질량의 합을 구한다.
	NumFixedVerts = 0;
	float MassSum = 0.0f;
	for (int32 V = 0; V < NumVerts; ++V)
	{
		if (IsFixed(V))
		{
			InverseMasses[V] = 0.0f;
			++NumFixedVerts;
		}
		else
		{
			MassSum += InverseMasses[V];
		}
	}

	// 3) 평균 질량이 1이 되게 맞춘 뒤 역수로 바꾼다. 메시 크기(단위)와 상관없이 같은 설정값이 통하게 하려는 것.
	if (MassSum > 0.0f)
	{
		const float MassScale = static_cast<float>(NumVerts - NumFixedVerts) / MassSum;
		for (int32 V = 0; V < NumVerts; ++V)
		{
			if (InverseMasses[V] > 0.0f)
			{
				InverseMasses[V] = 1.0f / (InverseMasses[V] * MassScale);
			}
		}
	}
}
