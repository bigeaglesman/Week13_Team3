#pragma once

#include "Core/EngineString.h"
#include <filesystem>

class UPhysicsAsset;

namespace PhysicsAssetFile
{
    bool Read(const std::filesystem::path& Path, UPhysicsAsset& Asset, FString& OutError);
    bool Write(const std::filesystem::path& Path, UPhysicsAsset& Asset, FString& OutError);
    bool IsPhysicsAssetPath(const FString& Path);
}
