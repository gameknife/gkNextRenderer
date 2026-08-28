include_guard(GLOBAL)

# .NET scripting layer configuration.
#
# Two things have to be resolved at configure time: which backend the managed code runs under, and
# where the two hostfxr headers live. Nothing is linked from the .NET installation — the CoreCLR
# host loads hostfxr by path at runtime (see the note at the top of CoreClrHost.cpp) — so a missing
# SDK degrades to "scripting disabled" instead of failing the build for everyone.

set(GK_DOTNET_BACKEND "CoreCLR" CACHE STRING "Managed scripting backend: CoreCLR or AOT")
set_property(CACHE GK_DOTNET_BACKEND PROPERTY STRINGS CoreCLR AOT)

set(GK_DOTNET_ROOT "" CACHE PATH "Explicit .NET installation root (defaults to external/dotnet or the system install)")
set(GK_DOTNET_HOSTPACK "" CACHE PATH "Directory holding hostfxr.h and coreclr_delegates.h")

set(GK_DOTNET_ENABLED OFF)

# How loudly to report that the scripting layer turned itself off. On desktop that is a normal
# state for a machine without the SDK. On Android it is not: the APK is built around a C# game, so
# a silent opt-out would ship an application with nothing to run — and Gradle drops CMake's STATUS
# messages, so anything quieter than a warning is invisible there.
set(gkDotNetOffLevel STATUS)
if(ANDROID)
    set(gkDotNetOffLevel WARNING)
endif()

# Two runtime identifiers are in play, and conflating them is the usual way an Android configure
# goes wrong. gkHostRid names the machine running the build: it selects the ILCompiler that has to
# execute here, and the host pack that carries hostfxr.h. gkTargetRid names what the managed code
# is compiled *for*, which is the same thing on desktop and something else entirely on Android.
if(CMAKE_HOST_WIN32)
    set(gkHostRid "win-x64")
elseif(CMAKE_HOST_APPLE)
    set(gkHostRid "osx-arm64")
else()
    set(gkHostRid "linux-x64")
endif()
set(gkTargetRid "${gkHostRid}")

if(IOS)
    # iOS needs a static NativeAOT library linked into the app bundle and a signing story to go with
    # it; neither is done yet, so the module is simply absent there.
    message(${gkDotNetOffLevel} ".NET scripting disabled on this platform")
    return()
endif()

if(ANDROID)
    # Android runs on bionic, not glibc, and has no CoreCLR host to load — NativeAOT is the only
    # backend. `linux-bionic-arm64` is the RID .NET publishes NativeAOT runtime packs for; the SDK
    # does not know it as a first-class target, which is why GkNext.Bootstrap.csproj has to opt out
    # of the cross-OS guard and name the host compiler package itself.
    #
    # The NDK's own toolchain file and CMake's built-in Android support disagree about which of
    # these it defines, and which one is set depends on the NDK release. Read both.
    set(gkAndroidAbi "${CMAKE_ANDROID_ARCH_ABI}")
    if(NOT gkAndroidAbi)
        set(gkAndroidAbi "${ANDROID_ABI}")
    endif()
    set(gkAndroidNdk "${CMAKE_ANDROID_NDK}")
    if(NOT gkAndroidNdk)
        set(gkAndroidNdk "${ANDROID_NDK}")
    endif()

    if(NOT gkAndroidAbi STREQUAL "arm64-v8a")
        message(${gkDotNetOffLevel} ".NET scripting disabled: only arm64-v8a has a NativeAOT runtime pack here")
        return()
    endif()
    set(gkTargetRid "linux-bionic-arm64")

    # ILC emits an object file and then hands the link to a platform compiler driver. On Android
    # that has to be the NDK's, targeting the same API level as the rest of the build — the default
    # `clang` on PATH would produce a glibc x86-64 binary.
    if(NOT gkAndroidNdk)
        message(${gkDotNetOffLevel} ".NET scripting disabled: the Android NDK root is not set")
        return()
    endif()
    # The NDK root arrives with Windows separators, and file(GLOB) matches nothing against those.
    file(TO_CMAKE_PATH "${gkAndroidNdk}" gkAndroidNdk)
    file(GLOB gkNdkToolchainBins LIST_DIRECTORIES true
        "${gkAndroidNdk}/toolchains/llvm/prebuilt/*/bin")
    set(gkNdkBinDir "")
    foreach(candidate IN LISTS gkNdkToolchainBins)
        if(IS_DIRECTORY "${candidate}")
            set(gkNdkBinDir "${candidate}")
            break()
        endif()
    endforeach()
    if(NOT gkNdkBinDir)
        message(${gkDotNetOffLevel} ".NET scripting disabled: no LLVM toolchain under ${gkAndroidNdk}")
        return()
    endif()

    # The managed library must be linked for the same API level as the rest of the build, and no
    # single variable reliably holds it: the NDK toolchain leaves CMAKE_SYSTEM_VERSION at 1, and
    # which of the rest is populated depends on how the toolchain was entered. Take the first that
    # looks like a real level, oldest supported being 21.
    set(gkAndroidApiLevel "")
    foreach(candidate IN ITEMS
        "${ANDROID_PLATFORM_LEVEL}"
        "${CMAKE_ANDROID_API}"
        "${ANDROID_NATIVE_API_LEVEL}"
        "${CMAKE_SYSTEM_VERSION}")
        if(candidate MATCHES "^[0-9]+$" AND candidate GREATER_EQUAL 21)
            set(gkAndroidApiLevel "${candidate}")
            break()
        endif()
    endforeach()
    if(NOT gkAndroidApiLevel AND ANDROID_PLATFORM MATCHES "^android-([0-9]+)$")
        set(gkAndroidApiLevel "${CMAKE_MATCH_1}")
    endif()
    if(NOT gkAndroidApiLevel)
        message(${gkDotNetOffLevel} ".NET scripting disabled: no Android API level could be resolved")
        return()
    endif()
    set(gkClangDriver "aarch64-linux-android${gkAndroidApiLevel}-clang")
    if(CMAKE_HOST_WIN32)
        set(gkClangDriver "${gkClangDriver}.cmd")
    endif()
    if(NOT EXISTS "${gkNdkBinDir}/${gkClangDriver}")
        message(${gkDotNetOffLevel} ".NET scripting disabled: ${gkNdkBinDir}/${gkClangDriver} not found")
        return()
    endif()

    set(GK_DOTNET_BACKEND "AOT" CACHE STRING "Managed scripting backend: CoreCLR or AOT" FORCE)
    set(GK_DOTNET_ANDROID_LINKER_DIR "${gkNdkBinDir}")
    set(GK_DOTNET_ANDROID_LINKER "${gkClangDriver}")
    set(GK_DOTNET_ANDROID_ABI "${gkAndroidAbi}")

    # Where the published runtime library is staged for Gradle to package. Set by the Android
    # driver in tools/android, the same way GK_ANDROID_ASSET_OUTPUT_DIR is: the build tree owns it
    # so a stale library from a previous game cannot survive into a later APK.
    if(NOT GK_ANDROID_JNILIBS_DIR)
        message(FATAL_ERROR
            "GK_ANDROID_JNILIBS_DIR is required for Android builds with .NET scripting. "
            "Configure Android through tools/android so the managed runtime stays in the build tree.")
    endif()
    get_filename_component(GK_ANDROID_JNILIBS_DIR "${GK_ANDROID_JNILIBS_DIR}" ABSOLUTE)
    file(TO_CMAKE_PATH "${GK_ANDROID_JNILIBS_DIR}" GK_ANDROID_JNILIBS_DIR)
endif()

# --- locate the .NET root -------------------------------------------------------------------
if(GK_DOTNET_ROOT AND EXISTS "${GK_DOTNET_ROOT}")
    set(gkDotNetRoot "${GK_DOTNET_ROOT}")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/external/dotnet")
    set(gkDotNetRoot "${CMAKE_SOURCE_DIR}/external/dotnet")
elseif(DEFINED ENV{DOTNET_ROOT} AND EXISTS "$ENV{DOTNET_ROOT}")
    set(gkDotNetRoot "$ENV{DOTNET_ROOT}")
elseif(CMAKE_HOST_WIN32 AND EXISTS "$ENV{ProgramFiles}/dotnet")
    set(gkDotNetRoot "$ENV{ProgramFiles}/dotnet")
elseif(CMAKE_HOST_APPLE AND EXISTS "/usr/local/share/dotnet")
    set(gkDotNetRoot "/usr/local/share/dotnet")
elseif(EXISTS "/usr/share/dotnet")
    set(gkDotNetRoot "/usr/share/dotnet")
endif()

if(NOT gkDotNetRoot)
    message(${gkDotNetOffLevel} ".NET scripting disabled: no .NET installation found. Run 'gnb dotnet setup'.")
    return()
endif()

# --- locate the host pack headers ------------------------------------------------------------
# CoreCLR only. NativeAOT never loads hostfxr, and no host pack is published for a cross target
# like linux-bionic-arm64, so demanding one here would disable Android over an unused header.
if(GK_DOTNET_BACKEND STREQUAL "AOT")
    set(gkDotNetHostPack "")
elseif(GK_DOTNET_HOSTPACK AND EXISTS "${GK_DOTNET_HOSTPACK}/hostfxr.h")
    set(gkDotNetHostPack "${GK_DOTNET_HOSTPACK}")
else()
    file(GLOB gkHostPackCandidates
        "${gkDotNetRoot}/packs/Microsoft.NETCore.App.Host.${gkHostRid}/*/runtimes/${gkHostRid}/native")
    # NATURAL ordering matters: a plain string sort ranks 8.0.27 above 10.0.8 and would silently
    # build against an older host pack than the pinned SDK.
    list(SORT gkHostPackCandidates COMPARE NATURAL ORDER DESCENDING)
    foreach(candidate IN LISTS gkHostPackCandidates)
        if(EXISTS "${candidate}/hostfxr.h")
            set(gkDotNetHostPack "${candidate}")
            break()
        endif()
    endforeach()
endif()

if(NOT gkDotNetHostPack AND NOT GK_DOTNET_BACKEND STREQUAL "AOT")
    message(${gkDotNetOffLevel} ".NET scripting disabled: hostfxr.h not found under ${gkDotNetRoot}/packs")
    return()
endif()

if(CMAKE_HOST_WIN32)
    set(gkDotNetExe "${gkDotNetRoot}/dotnet.exe")
else()
    set(gkDotNetExe "${gkDotNetRoot}/dotnet")
endif()
if(NOT EXISTS "${gkDotNetExe}")
    message(${gkDotNetOffLevel} ".NET scripting disabled: ${gkDotNetExe} not found")
    return()
endif()

# Forward slashes throughout: this path is baked into a C++ string literal, where a Windows
# backslash would start an escape sequence.
file(TO_CMAKE_PATH "${gkDotNetRoot}" gkDotNetRoot)
if(gkDotNetHostPack)
    file(TO_CMAKE_PATH "${gkDotNetHostPack}" gkDotNetHostPack)
endif()

set(GK_DOTNET_ENABLED ON)
set(GK_DOTNET_RESOLVED_ROOT "${gkDotNetRoot}")
set(GK_DOTNET_RESOLVED_HOSTPACK "${gkDotNetHostPack}")
set(GK_DOTNET_EXE "${gkDotNetExe}")
set(GK_DOTNET_RID "${gkTargetRid}")
set(GK_DOTNET_HOST_RID "${gkHostRid}")

if(GK_DOTNET_BACKEND STREQUAL "AOT")
    set(GK_DOTNET_USE_AOT 1)
else()
    set(GK_DOTNET_USE_AOT 0)
endif()

message(STATUS ".NET scripting enabled (${GK_DOTNET_BACKEND}) rid=${gkTargetRid} root=${gkDotNetRoot}")

# Managed sources shared by every publish rule. obj/ and bin/ hold generated files whose timestamps
# would make the stamps permanently dirty.
set(GK_DOTNET_MANAGED_ROOT "${CMAKE_SOURCE_DIR}/assets/csharp")
file(GLOB_RECURSE GK_DOTNET_MANAGED_SOURCES CONFIGURE_DEPENDS
    "${GK_DOTNET_MANAGED_ROOT}/*.cs"
    "${GK_DOTNET_MANAGED_ROOT}/*.csproj"
    "${GK_DOTNET_MANAGED_ROOT}/*.props"
)
list(FILTER GK_DOTNET_MANAGED_SOURCES EXCLUDE REGEX "/(obj|bin)/")

# Gives an application its C# game.
#
#   gk_dotnet_managed_game(<target> PROJECT <csproj> DIR <subdir>)
#
# Under CoreCLR the game is published to <bin>/csharp/<subdir> and loaded from there at runtime, so
# it can be rebuilt and hot reloaded without touching the executable. Under NativeAOT there is no
# loading: the game is compiled into a native library named after the target and linked in, which
# is why each application needs its own — two applications cannot share one bootstrap binary.
# Serialises managed publishes.
#
# Every publish rule builds some of the same projects — the bootstrap, GkNext.Engine, the source
# generator — so two running at once fight over the same obj/ directories and output files. The
# symptoms are unhelpful (a locked GkNext.SourceGen.dll, or ILC crashing), so the rules are chained
# instead. Publishing is a few seconds; the lost parallelism does not matter.
function(gk_dotnet_chain_publish target)
    get_property(previous GLOBAL PROPERTY GK_DOTNET_LAST_PUBLISH)
    if(previous)
        add_dependencies(${target} ${previous})
    endif()
    set_property(GLOBAL PROPERTY GK_DOTNET_LAST_PUBLISH ${target})
endfunction()

# Lets a target link the NextDotNet module without hosting any managed code. Under NativeAOT the
# linker needs GkNext_Bootstrap to exist; this provides a version that reports "no managed code
# here" rather than leaving an unresolved symbol.
function(gk_dotnet_stub_game target)
    if(GK_DOTNET_USE_AOT)
        target_sources(${target} PRIVATE "${CMAKE_SOURCE_DIR}/src/Modules/NextDotNet/Stub/AotStubBootstrap.cpp")
    endif()
endfunction()

function(gk_dotnet_managed_game target)
    cmake_parse_arguments(ARG "" "PROJECT;DIR" "" ${ARGN})
    if(NOT ARG_PROJECT OR NOT ARG_DIR)
        message(FATAL_ERROR "gk_dotnet_managed_game(${target}) requires PROJECT and DIR")
    endif()

    set(managedOutputDir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/csharp")
    set(stamp "${CMAKE_CURRENT_BINARY_DIR}/managed-game-${target}.stamp")

    if(GK_DOTNET_USE_AOT)
        set(nativeName "GkNext.Bootstrap.${target}")
        set(stageDir "${CMAKE_CURRENT_BINARY_DIR}/managed-aot")
        # Extra arguments the publish needs; only Android adds any.
        set(publishArgs "")
        set(publishLauncher "")
        if(ANDROID)
            # The APK carries the game's runtime as its own lib*.so, so it lands in the jniLibs
            # tree Gradle packages from rather than beside a desktop executable. Linking against
            # that same file is what puts its soname in the engine library's DT_NEEDED.
            set(nativeLib "${GK_ANDROID_JNILIBS_DIR}/${GK_DOTNET_ANDROID_ABI}/lib${nativeName}.so")
            set(nativeBinary "${nativeLib}")
            set(stagedLib "${stageDir}/lib${nativeName}.so")
            set(stagedBinary "${stagedLib}")
            # ILC compiles for the target itself but shells out to a platform compiler driver to
            # link, and finds it only by name on PATH — see the `where /Q` probe in
            # Microsoft.NETCore.Native.Unix.targets. A bare `clang` there would link a glibc x86-64
            # object, so the NDK's driver has to come first.
            set(publishLauncher ${CMAKE_COMMAND} -E env
                "--modify" "PATH=path_list_prepend:${GK_DOTNET_ANDROID_LINKER_DIR}" "--")
            list(APPEND publishArgs
                "-p:CppCompilerAndLinker=${GK_DOTNET_ANDROID_LINKER}"
                -p:LinkerFlavor=lld)
        elseif(WIN32)
            set(nativeLib "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${nativeName}.lib")
            set(nativeBinary "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${nativeName}.dll")
            set(stagedLib "${stageDir}/${nativeName}.lib")
            set(stagedBinary "${stageDir}/${nativeName}.dll")
        else()
            set(nativeLib "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/lib${nativeName}.so")
            set(nativeBinary "${nativeLib}")
            set(stagedLib "${stageDir}/lib${nativeName}.so")
            set(stagedBinary "${stagedLib}")
        endif()

        # Published into a staging directory, then only the native artifacts are copied out.
        # Publishing straight into bin/ would also drop the referenced managed assembly's portable
        # PDB there — and a managed FlappyCSharp.pdb sitting next to a native FlappyCSharp.exe makes
        # the linker fail with LNK1207 on a PDB it never wrote.
        add_custom_command(
            OUTPUT ${stamp} ${nativeLib}
            COMMAND ${publishLauncher} "${GK_DOTNET_EXE}" publish
                "${GK_DOTNET_MANAGED_ROOT}/GkNext.Bootstrap/GkNext.Bootstrap.csproj"
                -c Release -r ${GK_DOTNET_RID} -p:GkAot=true
                "-p:GkGameProject=${ARG_PROJECT}"
                -p:GkNativeName=${nativeName}
                ${publishArgs}
                -o "${stageDir}" --nologo
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${stagedBinary}" "${nativeBinary}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${stagedLib}" "${nativeLib}"
            COMMAND ${CMAKE_COMMAND} -E touch ${stamp}
            DEPENDS ${GK_DOTNET_MANAGED_SOURCES}
            COMMENT "Publishing ${target} managed game (NativeAOT)..."
            VERBATIM
        )
        add_custom_target(${target}ManagedGame DEPENDS ${stamp})
        add_dependencies(${target} ${target}ManagedGame)
        target_link_libraries(${target} PRIVATE "${nativeLib}")
        if(MSVC)
            # Incremental linking keeps a .ilk/.pdb pair describing the previous link. Switching
            # backend changes the inputs enough that the linker rejects them (LNK1207), and there
            # is nothing to gain from incremental linking against a NativeAOT library anyway.
            target_link_options(${target} PRIVATE /INCREMENTAL:NO)
        endif()
    else()
        add_custom_command(
            OUTPUT ${stamp}
            COMMAND "${GK_DOTNET_EXE}" publish "${ARG_PROJECT}"
                -c Release -o "${managedOutputDir}/${ARG_DIR}" --nologo
            COMMAND ${CMAKE_COMMAND} -E touch ${stamp}
            DEPENDS ${GK_DOTNET_MANAGED_SOURCES}
            COMMENT "Publishing ${target} managed game (CoreCLR)..."
            VERBATIM
        )
        add_custom_target(${target}ManagedGame DEPENDS ${stamp})
        add_dependencies(${target} ${target}ManagedGame)
    endif()

    gk_dotnet_chain_publish(${target}ManagedGame)
    set_target_properties(${target}ManagedGame PROPERTIES FOLDER "Modules")
endfunction()
