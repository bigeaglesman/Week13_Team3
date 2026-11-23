#include "EnginePCH.h"
#include "Physics/PhysicalMaterial.h"
#include "Physics/PhysicsCore.h"
#include "UObject/UObjectGlobals.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/StructuredArchive.h"
#include "Serialization/StructuredArchiveSlots.h"
#include "Serialization/Formatter/JsonArchiveInputFormatter.h"
#include "Serialization/Formatter/JsonArchiveOutputFormatter.h"

#include <PxPhysicsAPI.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <unordered_map>
#include <unordered_set>

using namespace physx;

namespace
{
	std::unordered_set<UPhysicalMaterial*>& GetActivePhysicalMaterials()
	{
		static std::unordered_set<UPhysicalMaterial*> ActiveMaterials;
		return ActiveMaterials;
	}

	std::unordered_map<FString, UPhysicalMaterial*>& GetPhysicalMaterialCache()
	{
		static std::unordered_map<FString, UPhysicalMaterial*> Cache;
		return Cache;
	}

	PxCombineMode::Enum ToPxCombineMode(EPhysicsCombineMode Mode)
	{
		switch (Mode)
		{
		case EPhysicsCombineMode::Min:      return PxCombineMode::eMIN;
		case EPhysicsCombineMode::Multiply: return PxCombineMode::eMULTIPLY;
		case EPhysicsCombineMode::Max:      return PxCombineMode::eMAX;
		case EPhysicsCombineMode::Average:
		default:                            return PxCombineMode::eAVERAGE;
		}
	}

	FString CombineModeToString(EPhysicsCombineMode Mode)
	{
		switch (Mode)
		{
		case EPhysicsCombineMode::Min:      return "Min";
		case EPhysicsCombineMode::Multiply: return "Multiply";
		case EPhysicsCombineMode::Max:      return "Max";
		case EPhysicsCombineMode::Average:
		default:                            return "Average";
		}
	}

	EPhysicsCombineMode StringToCombineMode(const FString& Str)
	{
		if (Str == "Min")      return EPhysicsCombineMode::Min;
		if (Str == "Multiply") return EPhysicsCombineMode::Multiply;
		if (Str == "Max")      return EPhysicsCombineMode::Max;
		return EPhysicsCombineMode::Average;
	}

	FString NormalizeAssetKey(const FString& InPath)
	{
		FString Key = InPath;
		std::replace(Key.begin(), Key.end(), '\\', '/');
		while (!Key.empty() && Key.front() == '/')
		{
			Key.erase(Key.begin());
		}
		if (Key.rfind("Assets/", 0) == 0)
		{
			Key.erase(0, 7);
		}
		return Key;
	}

	FString ResolvePhysicalMaterialFilePath(const FString& InPath)
	{
		namespace fs = std::filesystem;
		if (fs::exists(InPath))
		{
			return InPath;
		}

		const FString Normalized = NormalizeAssetKey(InPath);
		const FString AssetsPrefixed = "Assets/" + Normalized;
		if (fs::exists(AssetsPrefixed))
		{
			return AssetsPrefixed;
		}

		if (fs::exists(Normalized))
		{
			return Normalized;
		}

		return InPath;
	}

	void SerializePhysicalMaterialRecord(FStructuredArchive::FRecord Record, UPhysicalMaterial& Mat)
	{
		FArchive& Ar = Record.GetUnderlyingArchive();

		int32 Version = Ar.IsSaving() ? 1 : 0;
		Record << SA_VALUE("Version", Version);
		if (Version != 1)
		{
			Ar.SetError();
			return;
		}

		Record << SA_VALUE("StaticFriction", Mat.StaticFriction);
		Record << SA_VALUE("Friction", Mat.Friction);
		Record << SA_VALUE("Restitution", Mat.Restitution);
		Record << SA_VALUE("Density", Mat.Density);

		FString FrictionCombineStr = Ar.IsSaving() ? CombineModeToString(Mat.FrictionCombineMode) : FString("Average");
		FString RestitutionCombineStr = Ar.IsSaving() ? CombineModeToString(Mat.RestitutionCombineMode) : FString("Average");

		Record << SA_VALUE("FrictionCombineMode", FrictionCombineStr);
		Record << SA_VALUE("RestitutionCombineMode", RestitutionCombineStr);

		if (Ar.IsLoading())
		{
			Mat.FrictionCombineMode = StringToCombineMode(FrictionCombineStr);
			Mat.RestitutionCombineMode = StringToCombineMode(RestitutionCombineStr);
		}
	}
}

UPhysicalMaterial::~UPhysicalMaterial()
{
	ReleasePhysXMaterial();
}

PxMaterial* UPhysicalMaterial::GetPxMaterial()
{
	if (PhysXMaterial)
	{
		return PhysXMaterial;
	}

	if (!GPhysXSDK)
	{
		return nullptr;
	}

	const float SafeStaticFriction = std::max(StaticFriction, 0.0f);
	const float SafeDynamicFriction = std::max(Friction, 0.0f);
	const float SafeRestitution = std::clamp(Restitution, 0.0f, 1.0f);

	PhysXMaterial = GPhysXSDK->createMaterial(SafeStaticFriction, SafeDynamicFriction, SafeRestitution);
	if (PhysXMaterial)
	{
		PhysXMaterial->setFrictionCombineMode(ToPxCombineMode(FrictionCombineMode));
		PhysXMaterial->setRestitutionCombineMode(ToPxCombineMode(RestitutionCombineMode));
		GetActivePhysicalMaterials().insert(this);
	}
	else
	{
		HTR_LOG(Error, "[PhysX] UPhysicalMaterial::GetPxMaterial failed to create PxMaterial");
	}

	return PhysXMaterial;
}

void UPhysicalMaterial::UpdatePhysXMaterial()
{
	if (!PhysXMaterial)
	{
		return;
	}

	PhysXMaterial->setStaticFriction(std::max(StaticFriction, 0.0f));
	PhysXMaterial->setDynamicFriction(std::max(Friction, 0.0f));
	PhysXMaterial->setRestitution(std::clamp(Restitution, 0.0f, 1.0f));
	PhysXMaterial->setFrictionCombineMode(ToPxCombineMode(FrictionCombineMode));
	PhysXMaterial->setRestitutionCombineMode(ToPxCombineMode(RestitutionCombineMode));
}

void UPhysicalMaterial::ReleasePhysXMaterial()
{
	GetActivePhysicalMaterials().erase(this);
	if (PhysXMaterial)
	{
		PhysXMaterial->release();
		PhysXMaterial = nullptr;
	}
}

void UPhysicalMaterial::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);
	if (Property.Name == "Friction" ||
		Property.Name == "StaticFriction" ||
		Property.Name == "Restitution" ||
		Property.Name == "Density")
	{
		UpdatePhysXMaterial();
	}
}

bool UPhysicalMaterial::SaveToFile(const FString& FilePath, FString& OutError) const
{
	OutError.clear();

	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	{
		FJsonArchiveOutputFormatter Formatter(Writer);
		FStructuredArchive Archive(Formatter);
		SerializePhysicalMaterialRecord(Archive.Open().EnterRecord(), const_cast<UPhysicalMaterial&>(*this));
	}

	if (Writer.IsError())
	{
		OutError = "PhysicalMaterial: failed to serialize JSON";
		return false;
	}

	std::ofstream File(FilePath, std::ios::binary | std::ios::trunc);
	if (!File)
	{
		OutError = "PhysicalMaterial: cannot open output file";
		return false;
	}

	File.write(reinterpret_cast<const char*>(Bytes.GetData()), Bytes.Num());
	File.close();
	if (!File)
	{
		OutError = "PhysicalMaterial: failed to write file";
		return false;
	}

	return true;
}

bool UPhysicalMaterial::LoadFromFile(const FString& FilePath, FString& OutError)
{
	OutError.clear();

	std::ifstream File(FilePath, std::ios::binary | std::ios::ate);
	const auto Size = static_cast<std::streamoff>(File.tellg());
	if (!File || Size <= 0 || Size > (std::numeric_limits<int32>::max)())
	{
		OutError = "PhysicalMaterial: cannot open file or invalid size";
		return false;
	}

	TArray<uint8> Bytes;
	Bytes.SetNum(static_cast<int32>(Size));
	File.seekg(0);
	if (!File.read(reinterpret_cast<char*>(Bytes.GetData()), Size))
	{
		OutError = "PhysicalMaterial: cannot read file";
		return false;
	}

	FMemoryReader Reader(Bytes, true);
	FJsonArchiveInputFormatter Formatter(Reader);
	if (Reader.IsError())
	{
		OutError = "PhysicalMaterial: invalid JSON";
		return false;
	}

	FStructuredArchive Archive(Formatter);
	SerializePhysicalMaterialRecord(Archive.Open().EnterRecord(), *this);

	if (Reader.IsError())
	{
		OutError = "PhysicalMaterial: unsupported version or corrupt data";
		return false;
	}

	UpdatePhysXMaterial();
	return true;
}

UPhysicalMaterial* UPhysicalMaterial::LoadOrGet(const FString& AssetPath)
{
	if (AssetPath.empty())
	{
		return nullptr;
	}

	const FString Key = NormalizeAssetKey(AssetPath);
	auto& Cache = GetPhysicalMaterialCache();
	auto It = Cache.find(Key);
	if (It != Cache.end() && It->second != nullptr)
	{
		return It->second;
	}

	const FString ResolvedPath = ResolvePhysicalMaterialFilePath(AssetPath);
	UPhysicalMaterial* NewMat = NewObject<UPhysicalMaterial>();
	if (!NewMat)
	{
		return nullptr;
	}

	FString Error;
	if (!NewMat->LoadFromFile(ResolvedPath, Error))
	{
		HTR_LOG(Warning, "[PhysX] Failed to load PhysicalMaterial '{}': {}", ResolvedPath, Error);
		return nullptr;
	}

	Cache[Key] = NewMat;
	HTR_LOG(Info, "[PhysX] Loaded PhysicalMaterial '{}' (StaticFriction={}, Friction={}, Restitution={})",
		ResolvedPath, NewMat->StaticFriction, NewMat->Friction, NewMat->Restitution);
	return NewMat;
}

void UPhysicalMaterial::ReleaseAllPhysXMaterials()
{
	auto& Active = GetActivePhysicalMaterials();
	TArray<UPhysicalMaterial*> Snapshot;
	Snapshot.Reserve(static_cast<int32>(Active.size()));
	for (UPhysicalMaterial* Mat : Active)
	{
		Snapshot.Add(Mat);
	}

	for (UPhysicalMaterial* Mat : Snapshot)
	{
		if (Mat)
		{
			Mat->ReleasePhysXMaterial();
		}
	}

	Active.clear();
	GetPhysicalMaterialCache().clear();
}
