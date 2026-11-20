
#include "EnginePCH.h"
#include "Physics/PhysicsAssetFile.h"

#include "Physics/PhysicsAsset.h"
#include "Asset/AssetUtils.h"

#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/StructuredArchive.h"
#include "Serialization/Formatter/JsonArchiveInputFormatter.h"
#include "Serialization/Formatter/JsonArchiveOutputFormatter.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>

bool PhysicsAssetFile::Read(const std::filesystem::path& Path, UPhysicsAsset& Asset, FString& OutError)
{
    OutError.clear();

    std::ifstream File(Path, std::ios::binary | std::ios::ate);
    const auto Size = static_cast<std::streamoff>(File.tellg());

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

    Asset.SetAssetPath(FAssetUtils::GetAssetKey(Path.generic_string()));
    return true;
}

bool PhysicsAssetFile::Write(const std::filesystem::path& Path, UPhysicsAsset& Asset, FString& OutError)
{
    OutError.clear();

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

    std::ofstream File(Path, std::ios::binary | std::ios::trunc);

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

    Asset.SetAssetPath(FAssetUtils::GetAssetKey(Path.generic_string()));
    return true;
}

bool PhysicsAssetFile::IsPhysicsAssetPath(const FString& Path)
{
    FString Extension = std::filesystem::path(Path).extension().string();

    std::transform(Extension.begin(), Extension.end(), Extension.begin(),
        [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });

    return Extension == ".physicsasset";
}
