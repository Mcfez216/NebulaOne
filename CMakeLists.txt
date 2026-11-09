cmake_minimum_required(VERSION 3.20)
project(NebulaOne VERSION 0.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(NEBULA_BUILD_GAME "Build the game (needs a windowing system)" ON)
option(NEBULA_STATIC_RUNTIME "Link the C/C++ runtime statically" ON)
if(NEBULA_STATIC_RUNTIME)
  if(MINGW)
    # one self-contained .exe: no libstdc++ / libgcc / winpthread DLLs to ship
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -static-libstdc++")
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND NOT APPLE)
    # older Linux distributions may ship an older libstdc++
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
  endif()
endif()

# ---------------------------------------------------------------- core ------
# The simulation. No UI, no OpenGL: a future server links this alone.
add_library(nebula_core STATIC
  src/core/items.cpp
  src/core/world.cpp
  src/core/market.cpp
  src/core/fleet.cpp
  src/core/economy.cpp
  src/core/ai.cpp
  src/core/law.cpp
  src/core/divisions.cpp
  src/core/projects.cpp
  src/core/colonies.cpp
  src/core/capital.cpp
  src/core/contracts.cpp
  src/core/territory.cpp
  src/core/corporate.cpp
  src/core/events.cpp
  src/core/intel.cpp
  src/core/explore.cpp
  src/core/diplomacy.cpp
  src/core/commands.cpp)
target_include_directories(nebula_core PUBLIC src)
if(MSVC)
  target_compile_options(nebula_core PRIVATE /W3 /utf-8)
  target_compile_definitions(nebula_core PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(nebula_core PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
endif()

add_executable(nebula_selftest tools/selftest.cpp)
target_link_libraries(nebula_selftest PRIVATE nebula_core)

if(NOT NEBULA_BUILD_GAME)
  return()
endif()

# --------------------------------------------------------- third party ------
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/glfw EXCLUDE_FROM_ALL)

add_library(imgui STATIC
  third_party/imgui/imgui.cpp
  third_party/imgui/imgui_draw.cpp
  third_party/imgui/imgui_tables.cpp
  third_party/imgui/imgui_widgets.cpp
  third_party/imgui/imgui_demo.cpp
  third_party/imgui/backends/imgui_impl_glfw.cpp
  third_party/imgui/backends/imgui_impl_opengl3.cpp
  third_party/implot/implot.cpp
  third_party/implot/implot_items.cpp)
target_include_directories(imgui PUBLIC third_party/imgui third_party/imgui/backends third_party/implot third_party/stb)
target_link_libraries(imgui PUBLIC glfw)
find_package(OpenGL REQUIRED)
target_link_libraries(imgui PUBLIC OpenGL::GL)

# ------------------------------------------------- embedded assets ----------
# Fonts and the logo are compiled into the executable, so an install is one
# file plus shortcuts and nothing can go missing.
set(EMBED_FILES
  assets/fonts/SairaSemiCondensed-Medium.ttf
  assets/fonts/SairaSemiCondensed-SemiBold.ttf
  assets/fonts/ShareTechMono-Regular.ttf
  assets/fonts/ChakraPetch-SemiBold.ttf
  assets/logo/nebulaone-mark-256.png
  assets/logo/nebulaone-logo-stacked-900.png
  assets/logo/icon-256.png
  assets/logo/icon-48.png
  assets/logo/icon-32.png
  assets/logo/icon-16.png)
set(EMBED_OUT ${CMAKE_CURRENT_BINARY_DIR}/embedded_assets.cpp)
set(EMBED_ABS)
foreach(f ${EMBED_FILES})
  list(APPEND EMBED_ABS ${CMAKE_CURRENT_SOURCE_DIR}/${f})
endforeach()
string(REPLACE ";" "," EMBED_ARG "${EMBED_ABS}")  # a raw list would be split into separate arguments
add_custom_command(
  OUTPUT ${EMBED_OUT}
  COMMAND ${CMAKE_COMMAND} -DOUT=${EMBED_OUT} "-DFILES=${EMBED_ARG}" -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
  DEPENDS ${EMBED_ABS} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
  COMMENT "Embedding fonts and logo"
  VERBATIM)

# ----------------------------------------------------------------- game -----
set(GAME_SOURCES
  src/main.cpp
  src/app/app.cpp
  src/app/theme.cpp
  src/app/widgets.cpp
  src/app/mapview.cpp
  src/app/title.cpp
  src/app/tutorial.cpp
  src/app/win_corp.cpp
  src/app/win_market.cpp
  src/app/win_fleets.cpp
  src/app/win_industry.cpp
  src/app/win_finance.cpp
  src/app/win_people.cpp
  src/app/win_misc.cpp
  src/app/win_ship.cpp
  src/app/win_ops.cpp
  src/app/win_board.cpp
  src/app/platform.cpp
  src/app/win_diplo.cpp
  src/app/layout.cpp
  src/app/win_divs.cpp
  src/app/win_build.cpp
  src/app/win_colonies.cpp
  src/app/win_capital.cpp
  src/app/backdrop.cpp
  ${EMBED_OUT})
if(WIN32)
  set(NEBULA_ICO ${CMAKE_CURRENT_SOURCE_DIR}/assets/logo/nebulaone.ico)
  configure_file(packaging/windows/nebulaone.rc.in ${CMAKE_CURRENT_BINARY_DIR}/nebulaone.rc @ONLY)
  list(APPEND GAME_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/nebulaone.rc)
endif()
add_executable(NebulaOne ${GAME_SOURCES})
target_include_directories(NebulaOne PRIVATE src)
target_link_libraries(NebulaOne PRIVATE nebula_core imgui)
target_compile_definitions(NebulaOne PRIVATE NEBULA_VERSION="${PROJECT_VERSION}")
if(WIN32)
  set_target_properties(NebulaOne PROPERTIES WIN32_EXECUTABLE ON)
  target_link_libraries(NebulaOne PRIVATE ole32 uuid shell32)  # desktop shortcut (IShellLink)
  if(MSVC)
    target_link_options(NebulaOne PRIVATE /ENTRY:mainCRTStartup)  # keep main() with the GUI subsystem
    target_compile_definitions(NebulaOne PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(NebulaOne PRIVATE /utf-8)
  endif()
else()
  set_target_properties(NebulaOne PROPERTIES OUTPUT_NAME nebulaone)
endif()

# -------------------------------------------------------------- install -----
include(GNUInstallDirs)
if(WIN32)
  install(TARGETS NebulaOne RUNTIME DESTINATION .)
  install(FILES assets/logo/nebulaone.ico README.md LICENSE THIRD_PARTY_LICENSES.md DESTINATION .)
else()
  install(TARGETS NebulaOne RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES packaging/linux/nebulaone.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  foreach(sz 16 32 48 64 128 256)
    install(FILES assets/logo/icon-${sz}.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${sz}x${sz}/apps RENAME nebulaone.png)
  endforeach()
  install(FILES assets/logo/nebulaone-icon.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps RENAME nebulaone.svg)
  install(FILES README.md LICENSE THIRD_PARTY_LICENSES.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
endif()

# ------------------------------------------------------------ packaging -----
set(CPACK_PACKAGE_NAME "NebulaOne")
set(CPACK_PACKAGE_VENDOR "Josh McFerran")
set(CPACK_PACKAGE_CONTACT "Josh McFerran")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "NebulaOne - run a far-future corporation in a player-driven economy")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "NebulaOne")
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE)
set(CPACK_STRIP_FILES ON)
if(WIN32)
  list(PREPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/cpack)  # 64-bit NSIS template
  set(CPACK_GENERATOR "NSIS;ZIP")
  set(CPACK_NSIS_MUI_ICON ${CMAKE_CURRENT_SOURCE_DIR}/assets/logo/nebulaone.ico)
  set(CPACK_NSIS_MUI_UNIICON ${CMAKE_CURRENT_SOURCE_DIR}/assets/logo/nebulaone.ico)
  set(CPACK_NSIS_INSTALLED_ICON_NAME "NebulaOne.exe")
  set(CPACK_NSIS_DISPLAY_NAME "NebulaOne")
  set(CPACK_NSIS_PACKAGE_NAME "NebulaOne")
  set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
  set(CPACK_PACKAGE_EXECUTABLES "NebulaOne" "NebulaOne")
  set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")             # the exe sits in the install root, not bin/
  set(CPACK_CREATE_DESKTOP_LINKS "NebulaOne")           # desktop shortcut, removed again on uninstall
  set(CPACK_NSIS_MUI_FINISHPAGE_RUN "NebulaOne.exe")
else()
  set(CPACK_GENERATOR "TGZ;DEB;RPM")
  set(CPACK_DEBIAN_PACKAGE_DEPENDS "libgl1, libx11-6")
  set(CPACK_DEBIAN_PACKAGE_SECTION "games")
  set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
  set(CPACK_RPM_PACKAGE_LICENSE "Proprietary")
  set(CPACK_RPM_PACKAGE_GROUP "Amusements/Games")
  set(CPACK_RPM_PACKAGE_REQUIRES "libglvnd-glx, libX11")
  set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
  # Put a launcher on the installing user's desktop (and take it away again on removal).
  configure_file(packaging/linux/postinst.in ${CMAKE_BINARY_DIR}/pkg/postinst @ONLY
                 FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
  configure_file(packaging/linux/postrm.in ${CMAKE_BINARY_DIR}/pkg/postrm @ONLY
                 FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
  set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_BINARY_DIR}/pkg/postinst;${CMAKE_BINARY_DIR}/pkg/postrm")
  set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION ON)
  set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE ${CMAKE_BINARY_DIR}/pkg/postinst)
  set(CPACK_RPM_POST_UNINSTALL_SCRIPT_FILE ${CMAKE_BINARY_DIR}/pkg/postrm)
  set(_icon_dirs /usr/share/applications /usr/share/icons /usr/share/icons/hicolor)
  foreach(sz 16x16 32x32 48x48 64x64 128x128 256x256 scalable)
    list(APPEND _icon_dirs /usr/share/icons/hicolor/${sz} /usr/share/icons/hicolor/${sz}/apps)
  endforeach()
  set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION ${_icon_dirs})  # these belong to hicolor-icon-theme
endif()
include(CPack)
