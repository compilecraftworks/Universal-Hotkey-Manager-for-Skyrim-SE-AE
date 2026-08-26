# Compatibility target explicitly requested for this project.  The release tag
# and verified archive hash make normal builds reproducible; do not use --head
# or a floating branch for this dependency.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO alandtse/CommonLibSSE-NG
    REF v6.7.0
    SHA512 49e9c373f13710c2b613fe548920e934d4cd0f23fae63458edae47ae71d8ce1cf1dd8e862a37b508039e1210ebc6296f8e606daf41aae7c4e2d9ac45055eba4b
)

vcpkg_configure_cmake(
    SOURCE_PATH "${SOURCE_PATH}"
    PREFER_NINJA
    OPTIONS
        -DBUILD_TESTS=OFF
        -DENABLE_SKYRIM_SE=ON
        -DENABLE_SKYRIM_AE=ON
        -DENABLE_SKYRIM_VR=OFF
        -DSKSE_SUPPORT_XBYAK=ON
        -DSKSE_SUPPORT_PATCH_SAFETY=OFF
        -DCOMMONLIB_ENABLE_IPO=OFF
)

vcpkg_install_cmake()
vcpkg_fixup_cmake_targets(CONFIG_PATH lib/cmake/CommonLibSSE)
vcpkg_copy_pdbs()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

# CommonLibSSE v6.7.0's generated config includes this public plugin helper,
# but the upstream install rules do not currently stage it.
file(INSTALL "${SOURCE_PATH}/cmake/CommonLibSSE.cmake"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
# Its export also exposes DirectXTK, which must be found before consumers load
# the imported CommonLibSSE target.
file(WRITE "${CURRENT_PACKAGES_DIR}/share/${PORT}/CommonLibSSEConfig.cmake" [=[
include(CMakeFindDependencyMacro)
find_dependency(spdlog CONFIG)
find_dependency(directxtk CONFIG)
include("${CMAKE_CURRENT_LIST_DIR}/CommonLibSSE-targets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CommonLibSSE.cmake")
]=])
file(INSTALL "${SOURCE_PATH}/COPYING" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
