#include "EnginePCH.h"
#include "Physics/PhysicsAssetFile.h"
#include "Physics/PhysicsAsset.h"
#include "Render/SkeletalMesh.h"
#include "Asset/AssetUtils.h"

#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/StructuredArchive.h"
#include "Serialization/Formatter/JsonArchiveInputFormatter.h"
#include "Serialization/Formatter/JsonArchiveOutputFormatter.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <limits>

namespace
{
	std::filesystem::path ToFilePath(const FString& Path)
	{
		const FString Resolved = FAssetUtils::ResolveAssetFilePath(Path);
		return std::filesystem::path(Resolved.empty() ? Path : Resolved);
	}
}

bool FPhysicsAssetFile::Read(const FString& Path, UPhysicsAsset& Asset, FString& OutError)
{
	OutError.clear();
	const std::filesystem::path FilePath = ToFilePath(Path);

	std::ifstream File(FilePath, std::ios::binary | std::ios::ate);
	const std::streamoff Size = File ? static_cast<std::streamoff>(File.tellg()) : 0;
	if (!File || Size <= 0 || Size > (std::numeric_limits<int32>::max)())
	{
		OutError = "PhysicsAsset: cannot open file or invalid size";
		return false;
	}

	TArray<uint8> Bytes;
	Bytes.SetNum(static_cast<int32>(Size));
	File.seekg(0);
	if (!File.read(reinterpret_cast<char*>(Bytes.GetData()), Size))
	{
		OutError = "PhysicsAsset: cannot read file";
		return false;
	}

	FMemoryReader Reader(Bytes, true);
	FJsonArchiveInputFormatter Formatter(Reader);
	if (Reader.IsError())
	{
		OutError = "PhysicsAsset: invalid JSON";
		return false;
	}

	FStructuredArchive Archive(Formatter);
	Asset.Serialize(Archive.Open().EnterRecord());
	if (Reader.IsError())
	{
		OutError = "PhysicsAsset: unsupported version or broken data";
		return false;
	}

	Asset.AssetPath = FAssetUtils::GetAssetKey(FilePath.generic_string());
	Asset.bHasUnsavedChanges = false;
	return true;
}

bool FPhysicsAssetFile::Write(const FString& Path, UPhysicsAsset& Asset, FString& OutError)
{
	OutError.clear();
	const std::filesystem::path FilePath = ToFilePath(Path);

	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	{
		FJsonArchiveOutputFormatter Formatter(Writer);
		FStructuredArchive Archive(Formatter);
		Asset.Serialize(Archive.Open().EnterRecord());
	}
	if (Writer.IsError())
	{
		OutError = "PhysicsAsset: cannot serialize data";
		return false;
	}

	std::ofstream File(FilePath, std::ios::binary | std::ios::trunc);
	if (!File)
	{
		OutError = "PhysicsAsset: cannot open output file";
		return false;
	}

	File.write(reinterpret_cast<const char*>(Bytes.GetData()), Bytes.Num());
	File.close();
	if (!File)
	{
		OutError = "PhysicsAsset: cannot write file";
		return false;
	}

	Asset.AssetPath = FAssetUtils::GetAssetKey(FilePath.generic_string());
	Asset.bHasUnsavedChanges = false;
	return true;
}

bool FPhysicsAssetFile::IsPhysicsAssetPath(const FString& Path)
{
	FString Ext = std::filesystem::path(Path).extension().string();
	std::transform(Ext.begin(), Ext.end(), Ext.begin(), [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
	return Ext == Extension;
}

FString FPhysicsAssetFile::GetDefaultPathForMesh(const USkeletalMesh& Mesh)
{
	const FString MeshFile = FAssetUtils::ResolveAssetFilePath(Mesh.GetPath());
	if (MeshFile.empty())
	{
		return FString();
	}

	std::filesystem::path Path(MeshFile);
	Path.replace_extension(Extension);
	return Path.generic_string();
}

UPhysicsAsset* FPhysicsAssetFile::LoadForMesh(const USkeletalMesh& Mesh)
{
	const FString Path = GetDefaultPathForMesh(Mesh);
	std::error_code Ec;
	if (Path.empty() || !std::filesystem::exists(Path, Ec))
	{
		return nullptr;
	}

	UPhysicsAsset* Asset = NewObject<UPhysicsAsset>();
	FString Error;
	if (!Read(Path, *Asset, Error))
	{
		HTR_LOG(Error, "[PhysX] Failed to load physics asset '{}': {}", Path, Error);
		delete Asset;
		return nullptr;
	}
	return Asset;
}
