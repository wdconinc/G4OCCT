// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCTAssemblyRegistry.hh
/// @brief Declaration of G4OCCTAssemblyRegistry.

#ifndef G4OCCT_G4OCCTAssemblyRegistry_hh
#define G4OCCT_G4OCCTAssemblyRegistry_hh

#include <cstddef>
#include <map>
#include <string>

class G4OCCTAssemblyVolume;

/**
 * @brief Singleton registry for named G4OCCTAssemblyVolume instances.
 *
 * Stores heap-allocated `G4OCCTAssemblyVolume` objects by name and takes
 * ownership of them.  This is primarily used by the DD4hep plugin to keep
 * assemblies alive past the plugin build phase, where local variables would
 * otherwise be destroyed too early.
 *
 * ### Ownership
 *
 * The registry owns all registered assemblies.  They are deleted when
 * `Release()` is called or when the registry is destroyed.
 *
 * ### Usage
 * ```cpp
 * auto* assembly = G4OCCTAssemblyVolume::FromSTEP("detector.step", matMap);
 * G4OCCTAssemblyRegistry::Instance().Register("myDetector", assembly);
 *
 * // Later:
 * G4OCCTAssemblyVolume* a = G4OCCTAssemblyRegistry::Instance().Get("myDetector");
 * ```
 */
class G4OCCTAssemblyRegistry {
public:
  /// Return the singleton instance.
  static G4OCCTAssemblyRegistry& Instance();

  /**
   * Register a named assembly (takes ownership).
   *
   * @param name     Unique name for this assembly.
   * @param assembly Non-null pointer to the assembly to register.
   * @throws std::runtime_error if @p name is already registered.
   */
  void Register(const std::string& name, G4OCCTAssemblyVolume* assembly);

  /**
   * Retrieve an assembly by name.
   *
   * @param name Assembly name as passed to `Register()`.
   * @return Pointer to the assembly, or `nullptr` if not found.
   */
  G4OCCTAssemblyVolume* Get(const std::string& name) const;

  /**
   * Remove an assembly from the registry and return ownership to the caller.
   *
   * @param name Assembly name to release.
   * @return The previously registered pointer, or `nullptr` if not found.
   *         The caller is responsible for deleting the returned object.
   */
  G4OCCTAssemblyVolume* Release(const std::string& name);

  /**
   * Return the number of registered assemblies.
   */
  std::size_t Size() const;

private:
  G4OCCTAssemblyRegistry()  = default;
  ~G4OCCTAssemblyRegistry();

  std::map<std::string, G4OCCTAssemblyVolume*> fAssemblies;
};

#endif // G4OCCT_G4OCCTAssemblyRegistry_hh
