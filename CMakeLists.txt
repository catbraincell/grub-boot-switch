cmake_minimum_required(VERSION 3.16)

project(grub-boot-switch
    VERSION 0.1.0
    DESCRIPTION "Hardware boot selector for GRUB"
    LANGUAGES NONE)

# This is a scripts + data package (no compiled code). For packaging we install
# under /usr so that GNUInstallDirs maps SYSCONFDIR to /etc (its documented
# special case when the prefix is exactly /usr).
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "Install prefix" FORCE)
endif()

# Avoid the GNUInstallDirs "no target architecture" warning (LANGUAGES NONE):
# this is an arch-independent package, so a plain lib dir is correct.
set(CMAKE_INSTALL_LIBDIR "lib" CACHE PATH "Object code libraries")

include(GNUInstallDirs)

# `install` stages files into a rootfs (honours DESTDIR) and must stay usable on
# its own -- without dragging in the package targets that live in `all`.
set(CMAKE_SKIP_INSTALL_ALL_DEPENDENCY TRUE)

set(GBS_ETCDIR  "${CMAKE_INSTALL_FULL_SYSCONFDIR}")            # /etc  (prefix=/usr)
set(GBS_CONFDIR "${GBS_ETCDIR}/grub-boot-switch")

# 0755 for executables. install(FILES) stages 0644 by default, so anything that
# has to be runnable names these permissions explicitly. Both generators carry
# the staged mode through to the payload (RPM additionally pins it via %attr in
# CPACK_RPM_USER_FILELIST below).
set(GBS_EXE_PERMS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE
    GROUP_READ GROUP_EXECUTE
    WORLD_READ WORLD_EXECUTE)

# Pin directory perms to 0755 instead of inheriting the builder's umask: the
# payload carries /etc and /usr themselves, and dpkg applies packaged directory
# permissions to the existing ones on the target system.
set(CMAKE_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS ${GBS_EXE_PERMS})

# ---- install rules ----------------------------------------------------------
# Two components, one package each:
#   config -> grub-boot-switch-config   the boot-side half: generator, config,
#                                       bindings, udev rule. Needs GRUB only, so
#                                       it installs on a headless box.
#   gui    -> grub-boot-switch          the manager app. Needs the GTK stack, and
#                                       depends on -config: it edits its files.
# Neither carries firmware. The switch is standalone hardware -- flashed once,
# then it is just a USB drive to this machine (see firmware/README.md).

# grub.d generator script, must be executable.
# Deliberately NOT a conffile, will be remove on uninstall.
install(FILES
    src/grub.d/99_grub-boot-switch
    PERMISSIONS ${GBS_EXE_PERMS}
    DESTINATION "${GBS_ETCDIR}/grub.d"
    COMPONENT config)

# config + bindings
install(FILES
    src/config
    src/bindings
    DESTINATION "${GBS_CONFDIR}"
    COMPONENT config)

# udev rule: hide the switch device from the desktop (not a conffile).
# Boot-side: it is about the device, and applies with or without the GUI.
install(FILES src/99-grub-boot-switch.rules
    DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/udev/rules.d"
    COMPONENT config)

# libadwaita GUI (elevates itself via pkexec) -> /usr/bin
install(FILES src/grub-boot-switch-manager
    PERMISSIONS ${GBS_EXE_PERMS}
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
    COMPONENT gui)

# app-grid launcher
install(FILES src/org.catbraincell.GrubBootSwitch.desktop
    DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
    COMPONENT gui)

# ---- CPack: shared --------------------------------------------------------

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "grub-boot-switch")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "catbraincell <luoshumymail@gmail.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")

# One package per component, no lumping and no all-in-one extra.
set(CPACK_COMPONENTS_ALL config gui)
set(CPACK_COMPONENTS_GROUPING IGNORE)
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_RPM_COMPONENT_INSTALL ON)

set(GBS_PKG_CONFIG "grub-boot-switch-config")
set(GBS_PKG_GUI    "grub-boot-switch")

set(CPACK_COMPONENT_CONFIG_DESCRIPTION
    "Boot-side half of the hardware boot switch: a grub.d generator that turns the switch position into GRUB's default entry, its config and bindings files, and a udev rule that hides the device from the desktop. No GUI, no firmware -- the switch is standalone hardware.")
set(CPACK_COMPONENT_GUI_DESCRIPTION
    "libadwaita GUI that binds each switch position to a GRUB menu entry by writing /etc/grub-boot-switch/bindings. Needs grub-boot-switch-config, which owns that file and the boot-side generator.")

# ---- CPack: Debian packages -------------------------------------------------
# Produce <name>_<version>_all.deb, one per component.
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
set(CPACK_DEBIAN_PACKAGE_SECTION "admin")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "all")

set(CPACK_DEBIAN_CONFIG_PACKAGE_NAME "${GBS_PKG_CONFIG}")
set(CPACK_DEBIAN_GUI_PACKAGE_NAME "${GBS_PKG_GUI}")

# grub2-common, not grub-common: /etc/grub.d and update-grub/grub-mkconfig live
# there (grub-common is its lower half and carries neither). The generator is
# POSIX shell, so GRUB and udev are the whole list.
set(CPACK_DEBIAN_CONFIG_PACKAGE_DEPENDS "grub2-common, udev")
# pkexec is its own package since bookworm/23.04 and was part of policykit-1
# before that. util-linux is for lsblk. The version pin keeps the app and the
# files it writes from drifting apart.
set(CPACK_DEBIAN_GUI_PACKAGE_DEPENDS
    "${GBS_PKG_CONFIG} (= ${PROJECT_VERSION}), python3, python3-gi, gir1.2-gtk-4.0, gir1.2-adw-1, pkexec | policykit-1, util-linux")

# Maintainer scripts + conffiles list, all of them about /etc files this
# component owns. Order: conffiles, postinst, postrm. The GUI package ships no
# maintainer script: its payload already carries 0755 and it owns nothing in
# /etc, so there is nothing to assert or regenerate.
set(CPACK_DEBIAN_CONFIG_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/conffiles;${CMAKE_CURRENT_SOURCE_DIR}/packaging/postinst;${CMAKE_CURRENT_SOURCE_DIR}/packaging/postrm")

# ---- CPack: RPM packages (Fedora/RHEL/SUSE) ---------------------------------
# Built by the `rpm` target below (needs rpmbuild on the build host).
# noarch scripts packages; dependency names follow Fedora/RHEL conventions.
set(CPACK_RPM_PACKAGE_LICENSE "GPL3")
set(CPACK_RPM_PACKAGE_GROUP "System Environment/Base")
set(CPACK_RPM_PACKAGE_ARCHITECTURE "noarch")
set(CPACK_RPM_FILE_NAME "RPM-DEFAULT")
set(CPACK_RPM_PACKAGE_AUTOREQPROV "no")       # don't let rpmbuild invent deps

# Not relocatable: GRUB reads /etc/grub.d and udev reads /usr/lib/udev/rules.d,
# both by absolute path, so neither package can move. CPack assumes otherwise
# and derives the RPM prefixes from CMAKE_INSTALL_PREFIX (/usr), which leaves
# the /etc payload -- generator, config, bindings -- outside every prefix.
set(CPACK_PACKAGE_RELOCATABLE FALSE)

set(CPACK_RPM_CONFIG_PACKAGE_NAME "${GBS_PKG_CONFIG}")
set(CPACK_RPM_GUI_PACKAGE_NAME "${GBS_PKG_GUI}")

set(CPACK_RPM_CONFIG_PACKAGE_REQUIRES "grub2-tools, systemd-udev")
set(CPACK_RPM_GUI_PACKAGE_REQUIRES
    "${GBS_PKG_CONFIG} = ${PROJECT_VERSION}, python3, python3-gobject, gtk4, libadwaita, polkit, util-linux")

# Explicit attributes for all special files: %config(noreplace) so /etc files
# survive upgrades (like dpkg conffiles), and 0755 for the executables so RPM
# installs them runnable regardless of staged permissions.
set(CPACK_RPM_CONFIG_USER_FILELIST
    "%config(noreplace) /etc/grub-boot-switch/config"
    "%config(noreplace) /etc/grub-boot-switch/bindings"
    "%attr(755,root,root) /etc/grub.d/99_grub-boot-switch"
    "%attr(644,root,root) /usr/lib/udev/rules.d/99-grub-boot-switch.rules")
set(CPACK_RPM_GUI_USER_FILELIST
    "%attr(755,root,root) /usr/bin/grub-boot-switch-manager")

set(CPACK_RPM_CONFIG_POST_INSTALL_SCRIPT_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/rpm-post.sh")
set(CPACK_RPM_CONFIG_POST_UNINSTALL_SCRIPT_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/rpm-postun.sh")
# Avoid clashing with directories owned by the base system.
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
    "/etc/grub.d;/usr/lib/udev;/usr/lib/udev/rules.d;/usr/share/applications")

include(CPack)

# ---- package targets --------------------------------------------------------
# Two package targets. Each builds both components in one cpack run:
#     cmake --build build --target deb     -> build/grub-boot-switch{,-config}_<ver>_all.deb
#     cmake --build build --target rpm     -> build/grub-boot-switch{,-config}-<ver>-1.noarch.rpm
# Every target below joins the default target only when its tooling is present
# on this host; each stays available as an explicit target either way (and fails
# loudly if invoked without its tooling). Each one records its on/off state in
# GBS_STATE_<name> (+ a GBS_DETAIL_<name> artifact or reason); the report at the
# bottom turns those into configure-time status lines and a build-time summary.
# The firmware section appends the per-part target names to GBS_TARGETS.
set(GBS_TARGETS deb rpm)

find_program(RPMBUILD_EXECUTABLE rpmbuild)

add_custom_target(deb ALL
    COMMAND "${CMAKE_CPACK_COMMAND}" -G DEB
            --config "${CMAKE_BINARY_DIR}/CPackConfig.cmake"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "Packaging ${CPACK_PACKAGE_NAME} ${PROJECT_VERSION} (.deb)"
    VERBATIM)
set(GBS_STATE_deb ON)
set(GBS_DETAIL_deb
    "build/${GBS_PKG_CONFIG}_${PROJECT_VERSION}_all.deb + ${GBS_PKG_GUI}_${PROJECT_VERSION}_all.deb")

if(RPMBUILD_EXECUTABLE)
    set(GBS_RPM_IN_ALL ALL)
    set(GBS_STATE_rpm ON)
    set(GBS_DETAIL_rpm
        "build/${GBS_PKG_CONFIG}-${PROJECT_VERSION}-1.noarch.rpm + ${GBS_PKG_GUI}-${PROJECT_VERSION}-1.noarch.rpm")
else()
    set(GBS_RPM_IN_ALL "")
    set(GBS_STATE_rpm OFF)
    set(GBS_DETAIL_rpm "rpmbuild not found")
endif()

add_custom_target(rpm ${GBS_RPM_IN_ALL}
    COMMAND "${CMAKE_CPACK_COMMAND}" -G RPM
            --config "${CMAKE_BINARY_DIR}/CPackConfig.cmake"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "Packaging ${CPACK_PACKAGE_NAME} ${PROJECT_VERSION} (.rpm)"
    VERBATIM)

# ---- firmware targets -------------------------------------------------------
# The MCU firmware is cross-compiled and lives outside the packages. One target
# per exact part, named after it:
#     cmake --build build --target stm32f103c8t6  -> build/gbswitch-<part>.bin
#     cmake --build build --target rp2040         -> build/gbswitch-rp2040.uf2
# All of them need arm-none-eabi-gcc plus their SDK. The firmware directories
# hold sources only: the STM32 parts are compiled by the rules below, and the
# RP2040 project file is generated into the build tree (the pico-sdk insists on
# its own configure step, so it cannot be an add_subdirectory of this project).

set(GBS_FIRMWARE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/firmware")

find_program(GBS_ARM_GCC arm-none-eabi-gcc)
find_program(GBS_ARM_OBJCOPY arm-none-eabi-objcopy)
find_program(GBS_ARM_SIZE arm-none-eabi-size)

# Neither SDK has a find module, so probe the usual spots. An explicit -D or the
# SDK's own environment variable always wins; the first hit otherwise.
function(gbs_find_sdk out_var marker)
    set(fallback "")
    foreach(dir IN LISTS ARGN)
        if(NOT dir)
            continue()
        endif()
        if(NOT fallback)
            set(fallback "${dir}")   # first real candidate: names the miss
        endif()
        if(EXISTS "${dir}/${marker}")
            set(${out_var} "${dir}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${out_var} "${fallback}" PARENT_SCOPE)
endfunction()

# --- stm32 (libopencm3), one target per exact part ---
gbs_find_sdk(GBS_OPENCM3_GUESS "ld/devices.data"
    "$ENV{OPENCM3_PATH}"
    "/opt/libopencm3"
)
set(OPENCM3_PATH "${GBS_OPENCM3_GUESS}"
    CACHE PATH "Built libopencm3 tree used by the STM32 targets")

set(GBS_STM32_DEVICES "stm32f103c8t6;stm32f072rbt6;stm32f401ccu6;stm32f407vgt6"
    CACHE STRING "STM32 parts to build a firmware target for (target name = part)")

set(GBS_STM32_SRC "${GBS_FIRMWARE_DIR}/stm32")
set(GBS_STM32_BUILD "${CMAKE_BINARY_DIR}/firmware/stm32")
set(GBS_GENLINK "${OPENCM3_PATH}/scripts/genlink.py")
set(GBS_DEVICES_DATA "${OPENCM3_PATH}/ld/devices.data")

# libopencm3 keeps the per-part facts (core, FPU, memory map, defines) in
# ld/devices.data and reads them with genlink.py -- the same source its own
# build system uses. Ask it instead of hardcoding a table per family.
function(gbs_genlink out_var device query)
    execute_process(
        COMMAND "${GBS_GENLINK}" "${GBS_DEVICES_DATA}" "${device}" "${query}"
        OUTPUT_VARIABLE value OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE rc ERROR_QUIET)
    if(NOT rc EQUAL 0)
        set(value "")
    endif()
    set(${out_var} "${value}" PARENT_SCOPE)
endfunction()

# Mirrors libopencm3's mk/genlink-config.mk.
function(gbs_arch_flags out_var cpu fpu)
    set(flags "-mcpu=${cpu}")
    if(cpu MATCHES "^cortex-m(0|0plus|3|4|7)$")
        list(APPEND flags -mthumb)
    endif()
    if(fpu STREQUAL "soft")
        list(APPEND flags -msoft-float)
    elseif(fpu STREQUAL "hard-fpv4-sp-d16")
        list(APPEND flags -mfloat-abi=hard -mfpu=fpv4-sp-d16)
    elseif(fpu STREQUAL "hard-fpv5-d16")
        list(APPEND flags -mfloat-abi=hard -mfpu=fpv5-d16)
    elseif(fpu STREQUAL "hard-fpv5-sp-d16")
        list(APPEND flags -mfloat-abi=hard -mfpu=fpv5-sp-d16)
    endif()
    set(${out_var} "${flags}" PARENT_SCOPE)
endfunction()

foreach(dev IN LISTS GBS_STM32_DEVICES)
    list(APPEND GBS_TARGETS ${dev})
    set(GBS_STATE_${dev} OFF)
    set(bin "${CMAKE_BINARY_DIR}/gbswitch-${dev}.bin")
    set(elf "${GBS_STM32_BUILD}/${dev}/gbswitch.elf")
    set(ldscript "${GBS_STM32_BUILD}/${dev}/generated.${dev}.ld")

    if(NOT GBS_ARM_GCC)
        set(GBS_DETAIL_${dev} "arm-none-eabi-gcc not found")
    elseif(NOT EXISTS "${GBS_DEVICES_DATA}")
        set(GBS_DETAIL_${dev} "libopencm3 not found (-DOPENCM3_PATH=...)")
    else()
        gbs_genlink(family "${dev}" FAMILY)
        if(NOT family)
            set(GBS_DETAIL_${dev} "${dev} is not in ${GBS_DEVICES_DATA}")
        elseif(NOT EXISTS "${OPENCM3_PATH}/lib/libopencm3_${family}.a")
            set(GBS_DETAIL_${dev}
                "libopencm3_${family}.a not built (make -C ${OPENCM3_PATH} TARGETS=${family}/..)")
        else()
            set(GBS_STATE_${dev} ON)
            set(GBS_DETAIL_${dev} "build/gbswitch-${dev}.bin")
        endif()
    endif()

    if(NOT GBS_STATE_${dev})
        # Requested without its tooling: say why instead of failing at the link.
        add_custom_target(${dev}
            COMMAND "${CMAKE_COMMAND}" -E echo "${dev}: ${GBS_DETAIL_${dev}}"
            COMMAND "${CMAKE_COMMAND}" -E echo
                    "See firmware/README.md for how to install libopencm3."
            COMMAND "${CMAKE_COMMAND}" -E false
            VERBATIM)
        continue()
    endif()

    gbs_genlink(cpu "${dev}" CPU)
    gbs_genlink(fpu "${dev}" FPU)
    gbs_genlink(cppflags "${dev}" CPPFLAGS)   # -DSTM32F4 -DSTM32F407VGT6 ...
    gbs_genlink(lddefs "${dev}" DEFS)         # the above plus the memory map
    gbs_arch_flags(arch "${cpu}" "${fpu}")
    separate_arguments(cppflags UNIX_COMMAND "${cppflags}")
    separate_arguments(lddefs UNIX_COMMAND "${lddefs}")

    # openocd names its config per family: stm32f1 -> target/stm32f1x.cfg
    set(ocd "target/${family}x.cfg")

    file(MAKE_DIRECTORY "${GBS_STM32_BUILD}/${dev}")

    # The linker script is the device memory map preprocessed into linker.ld.S.
    add_custom_command(OUTPUT "${ldscript}"
        COMMAND "${GBS_ARM_GCC}" ${arch} ${lddefs} -P -E
                "${OPENCM3_PATH}/ld/linker.ld.S" -o "${ldscript}"
        DEPENDS "${OPENCM3_PATH}/ld/linker.ld.S" "${GBS_DEVICES_DATA}"
        COMMENT "Generating linker script for ${dev}"
        VERBATIM)

    # One translation unit, so compile and link in a single call -- same flags
    # the libopencm3 example Makefiles use.
    add_custom_command(OUTPUT "${elf}" "${bin}"
        COMMAND "${GBS_ARM_GCC}"
                -Os -g3 -std=c11 -Wall -Wextra
                -ffunction-sections -fdata-sections
                ${arch} ${cppflags} "-I${OPENCM3_PATH}/include"
                "${GBS_STM32_SRC}/gbswitch.c"
                --static -nostartfiles "-T${ldscript}"
                "-L${OPENCM3_PATH}/lib" -Wl,--gc-sections
                "-lopencm3_${family}"
                -Wl,--start-group -lc -lgcc -lnosys -Wl,--end-group
                -o "${elf}"
        COMMAND "${GBS_ARM_OBJCOPY}" -O binary "${elf}" "${bin}"
        DEPENDS "${GBS_STM32_SRC}/gbswitch.c" "${GBS_STM32_SRC}/ext2_image.h"
                "${ldscript}" "${OPENCM3_PATH}/lib/libopencm3_${family}.a"
        COMMENT "Building STM32 firmware for ${dev} (gbswitch-${dev}.bin)"
        VERBATIM)

    # No flash target -- the tool depends on your probe, so print the commands.
    add_custom_target(${dev} ALL
        DEPENDS "${bin}"
        COMMAND "${GBS_ARM_SIZE}" "${elf}"
        COMMAND "${CMAKE_COMMAND}" -E echo ""
        COMMAND "${CMAKE_COMMAND}" -E echo "  ${dev} firmware ready: ${bin}"
        COMMAND "${CMAKE_COMMAND}" -E echo "  Flash it with whichever probe you have:"
        COMMAND "${CMAKE_COMMAND}" -E echo
                "    ST-Link : st-flash write ${bin} 0x8000000"
        COMMAND "${CMAKE_COMMAND}" -E echo
                "    OpenOCD : openocd -f interface/stlink.cfg -f ${ocd} -c 'program ${bin} 0x8000000 verify reset exit'"
        COMMAND "${CMAKE_COMMAND}" -E echo
                "    DFU/USB : dfu-util -a 0 -s 0x08000000:leave -D ${bin}"
        COMMAND "${CMAKE_COMMAND}" -E echo
                "    Serial  : stm32flash -w ${bin} -v -g 0x0 /dev/ttyUSB0   (BOOT0 high)"
        COMMAND "${CMAKE_COMMAND}" -E echo ""
        VERBATIM)
endforeach()

# --- rp2040 (pico-sdk) ---
gbs_find_sdk(GBS_PICO_SDK_GUESS "external/pico_sdk_import.cmake"
    "$ENV{PICO_SDK_PATH}"
    "/opt/pico-sdk"
)
set(PICO_SDK_PATH "${GBS_PICO_SDK_GUESS}"
    CACHE PATH "pico-sdk checkout used by the 'rp2040' target")
list(APPEND GBS_TARGETS rp2040)

find_program(GBS_PICOTOOL picotool
    DOC "picotool executable used by 'rp2040' like targets (found on PATH)")

set(GBS_RP2040_SRC "${GBS_FIRMWARE_DIR}/rp2040")
set(GBS_RP2040_PROJECT "${CMAKE_BINARY_DIR}/firmware/rp2040-project")
set(GBS_RP2040_BUILD "${CMAKE_BINARY_DIR}/firmware/rp2040")

# The pico-sdk needs its own configure step with its own toolchain, so it cannot
# be an add_subdirectory() of this (LANGUAGES NONE, noarch) project. Generate a
# standalone project that compiles the sources in place.
string(CONFIGURE [==[
cmake_minimum_required(VERSION 3.13)

# Generated by the top-level grub-boot-switch CMakeLists.txt -- do not edit.
# The sources stay in @GBS_RP2040_SRC@.

if(NOT PICO_SDK_PATH AND NOT DEFINED ENV{PICO_SDK_PATH})
    message(FATAL_ERROR
        "PICO_SDK_PATH is not set. Configure the top-level project with "
        "-DPICO_SDK_PATH=/path/to/pico-sdk, or export it.")
endif()
if(NOT PICO_SDK_PATH)
    set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}")
endif()

include("${PICO_SDK_PATH}/external/pico_sdk_import.cmake")

project(gbswitch_rp2040 C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_executable(gbswitch-rp2040
    "@GBS_RP2040_SRC@/main.c"
    "@GBS_RP2040_SRC@/msc_disk.c"
    "@GBS_RP2040_SRC@/buttons.c"
    "@GBS_RP2040_SRC@/usb_descriptors.c"
)

# Let TinyUSB and our code find tusb_config.h / ext2_image.h.
target_include_directories(gbswitch-rp2040 PRIVATE "@GBS_RP2040_SRC@")

# Same warning level the STM32 Makefile uses -- our sources only, not the SDK.
target_compile_options(gbswitch-rp2040 PRIVATE -Wall -Wextra)

target_link_libraries(gbswitch-rp2040
    pico_stdlib
    tinyusb_device
    tinyusb_board
    hardware_gpio
    hardware_adc      # the potentiometer selection input (SEL_ADC in msc_disk.c)
)

pico_add_extra_outputs(gbswitch-rp2040)   # produces .uf2
]==] GBS_RP2040_CMAKELISTS @ONLY)

file(GENERATE OUTPUT "${GBS_RP2040_PROJECT}/CMakeLists.txt"
     CONTENT "${GBS_RP2040_CMAKELISTS}")

# The pico-sdk pins its path into its own cache and refuses to move. Drop that
# cache when PICO_SDK_PATH changes, so re-pointing it does not need a wipe.
set(GBS_RP2040_CACHE "${GBS_RP2040_BUILD}/CMakeCache.txt")
if(EXISTS "${GBS_RP2040_CACHE}")
    file(STRINGS "${GBS_RP2040_CACHE}" GBS_RP2040_OLD_SDK
         REGEX "^PICO_SDK_PATH:")
    if(GBS_RP2040_OLD_SDK AND
       NOT GBS_RP2040_OLD_SDK MATCHES "=${PICO_SDK_PATH}$")
        message(STATUS "PICO_SDK_PATH changed: clearing ${GBS_RP2040_BUILD}")
        file(REMOVE_RECURSE "${GBS_RP2040_BUILD}")
    endif()
endif()

set(GBS_STATE_rp2040 OFF)
if(NOT GBS_ARM_GCC)
    set(GBS_RP2040_IN_ALL "")
    set(GBS_DETAIL_rp2040 "arm-none-eabi-gcc not found")
elseif(NOT EXISTS "${PICO_SDK_PATH}/external/pico_sdk_import.cmake")
    set(GBS_RP2040_IN_ALL "")
    set(GBS_DETAIL_rp2040 "pico-sdk not found (-DPICO_SDK_PATH=...)")
elseif(NOT GBS_PICOTOOL)
    set(GBS_RP2040_IN_ALL "")
    set(GBS_DETAIL_rp2040 "picotool not found (install it)")
else()
    set(GBS_RP2040_IN_ALL ALL)
    set(GBS_STATE_rp2040 ON)
    set(GBS_DETAIL_rp2040 "build/gbswitch-rp2040.uf2")
endif()

# No flash target here either -- BOOTSEL flashing is a file copy, so print it.
if(NOT GBS_STATE_rp2040)
    add_custom_target(rp2040
        COMMAND "${CMAKE_COMMAND}" -E echo "rp2040: ${GBS_DETAIL_rp2040}"
        COMMAND "${CMAKE_COMMAND}" -E echo
                "See firmware/README.md for how to install the pico-sdk and picotool."
        COMMAND "${CMAKE_COMMAND}" -E false
        VERBATIM)
else()
add_custom_target(rp2040 ${GBS_RP2040_IN_ALL}
    COMMAND "${CMAKE_COMMAND}" -S "${GBS_RP2040_PROJECT}" -B "${GBS_RP2040_BUILD}"
            -DCMAKE_BUILD_TYPE=Release "-DPICO_SDK_PATH=${PICO_SDK_PATH}"
    COMMAND "${CMAKE_COMMAND}" --build "${GBS_RP2040_BUILD}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${GBS_RP2040_BUILD}/gbswitch-rp2040.uf2"
            "${CMAKE_BINARY_DIR}/gbswitch-rp2040.uf2"
    COMMAND "${CMAKE_COMMAND}" -E echo ""
    COMMAND "${CMAKE_COMMAND}" -E echo
            "  RP2040 firmware ready: ${CMAKE_BINARY_DIR}/gbswitch-rp2040.uf2"
    COMMAND "${CMAKE_COMMAND}" -E echo
            "  Flash it by holding BOOTSEL while plugging the board in, then:"
    COMMAND "${CMAKE_COMMAND}" -E echo
            "    cp ${CMAKE_BINARY_DIR}/gbswitch-rp2040.uf2 /media/$ENV{USER}/RPI-RP2/"
    COMMAND "${CMAKE_COMMAND}" -E echo
            "  Or, without touching BOOTSEL (board already running this firmware):"
    COMMAND "${CMAKE_COMMAND}" -E echo
            "    picotool load -x ${CMAKE_BINARY_DIR}/gbswitch-rp2040.uf2"
    COMMAND "${CMAKE_COMMAND}" -E echo ""
    COMMENT "Building RP2040 firmware (gbswitch-rp2040.uf2)"
    USES_TERMINAL VERBATIM)
endif()

# ---- target report ----------------------------------------------------------
# Same data twice: an on/off list at configure time (so a missing SDK is visible
# before you build) and a summary at the end of the default build (so a skip
# never passes for silence). An OFF target is still buildable on request.

set(GBS_DEFAULT_TARGETS "")
set(GBS_ANY_OFF FALSE)

set(GBS_SUMMARY_CMDS
    COMMAND "${CMAKE_COMMAND}" -E echo ""
    COMMAND "${CMAKE_COMMAND}" -E echo
            "  ${CPACK_PACKAGE_NAME} ${PROJECT_VERSION} -- default build"
    COMMAND "${CMAKE_COMMAND}" -E echo
            "  ---------------------------------------------------------")

message(STATUS "${CPACK_PACKAGE_NAME} ${PROJECT_VERSION} -- default build targets:")

# Column width follows the longest target name (parts are named in full).
set(GBS_NAME_WIDTH 0)
foreach(t IN LISTS GBS_TARGETS)
    string(LENGTH "${t}" len)
    if(len GREATER GBS_NAME_WIDTH)
        set(GBS_NAME_WIDTH ${len})
    endif()
endforeach()
math(EXPR GBS_NAME_WIDTH "${GBS_NAME_WIDTH} + 2")

foreach(t IN LISTS GBS_TARGETS)
    string(LENGTH "${t}" len)
    math(EXPR pad "${GBS_NAME_WIDTH} - ${len}")
    string(REPEAT " " ${pad} gap)

    if(GBS_STATE_${t})
        list(APPEND GBS_DEFAULT_TARGETS ${t})
        message(STATUS "  ${t}${gap}ON   -> ${GBS_DETAIL_${t}}")
        list(APPEND GBS_SUMMARY_CMDS
             COMMAND "${CMAKE_COMMAND}" -E echo "  built    ${t}${gap}${GBS_DETAIL_${t}}")
    else()
        set(GBS_ANY_OFF TRUE)
        message(STATUS "  ${t}${gap}OFF  -- ${GBS_DETAIL_${t}}")
        list(APPEND GBS_SUMMARY_CMDS
             COMMAND "${CMAKE_COMMAND}" -E echo "  skipped  ${t}${gap}${GBS_DETAIL_${t}}")
    endif()
endforeach()

if(GBS_ANY_OFF)
    message(STATUS "  (an OFF target still builds on request: "
                   "cmake --build <dir> --target <name>)")
    list(APPEND GBS_SUMMARY_CMDS
         COMMAND "${CMAKE_COMMAND}" -E echo ""
         COMMAND "${CMAKE_COMMAND}" -E echo
                 "  A skipped target still builds on request:"
         COMMAND "${CMAKE_COMMAND}" -E echo
                 "      cmake --build ${CMAKE_BINARY_DIR} --target <name>")
endif()

list(APPEND GBS_SUMMARY_CMDS
     COMMAND "${CMAKE_COMMAND}" -E echo
             "  Other targets: install, package, clean"
     COMMAND "${CMAKE_COMMAND}" -E echo "")

add_custom_target(summary ALL ${GBS_SUMMARY_CMDS} VERBATIM)
if(GBS_DEFAULT_TARGETS)
    add_dependencies(summary ${GBS_DEFAULT_TARGETS})
endif()
