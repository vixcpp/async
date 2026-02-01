#pragma once

namespace cnerium
{

  // Semantic versioning
  inline constexpr int version_major = 0;
  inline constexpr int version_minor = 1;
  inline constexpr int version_patch = 0;

  // pre-release / metadata
  inline constexpr const char *version_prerelease = "";
  inline constexpr const char *version_metadata = "";

  // "0.1.0"
  inline constexpr const char *version_string = "0.1.0";

  // ABI version (bump when breaking ABI)
  inline constexpr int abi_version = 0;

} // namespace cnerium
