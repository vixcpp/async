/**
 *
 *  @file version.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.
 *  All rights reserved.
 *  https://github.com/vixcpp/vix
 *
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#ifndef VIX_ASYNC_VERSION_HPP
#define VIX_ASYNC_VERSION_HPP

namespace vix::async
{
  /**
   * @brief Major version number.
   *
   * Incremented when incompatible API changes are introduced.
   */
  inline constexpr int version_major = 1;

  /**
   * @brief Minor version number.
   *
   * Incremented when functionality is added in a backward-compatible manner.
   */
  inline constexpr int version_minor = 2;

  /**
   * @brief Patch version number.
   *
   * Incremented when backward-compatible bug fixes are made.
   */
  inline constexpr int version_patch = 1;

  /**
   * @brief Optional pre-release identifier.
   *
   * Examples: "alpha", "beta", "rc.1".
   * Empty string indicates a stable release.
   */
  inline constexpr const char *version_prerelease = "";

  /**
   * @brief Optional build metadata.
   *
   * Examples: git commit hash, build timestamp, distribution tag.
   * Empty string indicates no additional metadata.
   */
  inline constexpr const char *version_metadata = "";

  /**
   * @brief Human-readable semantic version string.
   *
   * Format: MAJOR.MINOR.PATCH[-PRERELEASE][+METADATA]
   *
   * Example: "0.1.0", "1.2.0-rc.1+githash".
   */
  inline constexpr const char *version_string = "1.2.1";

  /**
   * @brief ABI version number.
   *
   * Incremented whenever a binary-incompatible change is introduced.
   * This allows consumers to detect ABI mismatches at compile or link time.
   */
  inline constexpr int abi_version = 1;

} // namespace vix::async

#endif // VIX_ASYNC_VERSION_HPP
