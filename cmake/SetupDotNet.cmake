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

# The .NET runtime identifier for this host. NativeAOT publishing requires it explicitly, and the
# host pack that carries hostfxr.h is named after it.
if(WIN32)
    set(gkHostRid "win-x64")
elseif(APPLE)
    set(gkHostRid "osx-arm64")
else()
    set(gkHostRid "linux-x64")
endif()

if(ANDROID OR IOS)
    # The managed runtime needs NativeAOT on mobile and device validation is out of scope for the
    # first version (design section 9), so the module is simply absent there.
    message(STATUS ".NET scripting disabled on this platform")
    return()
endif()

# --- locate the .NET root -------------------------------------------------------------------
if(GK_DOTNET_ROOT AND EXISTS "${GK_DOTNET_ROOT}")
    set(gkDotNetRoot "${GK_DOTNET_ROOT}")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/external/dotnet")
    set(gkDotNetRoot "${CMAKE_SOURCE_DIR}/external/dotnet")
elseif(DEFINED ENV{DOTNET_ROOT} AND EXISTS "$ENV{DOTNET_ROOT}")
    set(gkDotNetRoot "$ENV{DOTNET_ROOT}")
elseif(WIN32 AND EXISTS "$ENV{ProgramFiles}/dotnet")
    set(gkDotNetRoot "$ENV{ProgramFiles}/dotnet")
elseif(APPLE AND EXISTS "/usr/local/share/dotnet")
    set(gkDotNetRoot "/usr/local/share/dotnet")
elseif(EXISTS "/usr/share/dotnet")
    set(gkDotNetRoot "/usr/share/dotnet")
endif()

if(NOT gkDotNetRoot)
    message(STATUS ".NET scripting disabled: no .NET installation found. Run 'gnb dotnet setup'.")
    return()
endif()

# --- locate the host pack headers ------------------------------------------------------------
if(GK_DOTNET_HOSTPACK AND EXISTS "${GK_DOTNET_HOSTPACK}/hostfxr.h")
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

if(NOT gkDotNetHostPack)
    message(STATUS ".NET scripting disabled: hostfxr.h not found under ${gkDotNetRoot}/packs")
    return()
endif()

if(WIN32)
    set(gkDotNetExe "${gkDotNetRoot}/dotnet.exe")
else()
    set(gkDotNetExe "${gkDotNetRoot}/dotnet")
endif()
if(NOT EXISTS "${gkDotNetExe}")
    message(STATUS ".NET scripting disabled: ${gkDotNetExe} not found")
    return()
endif()

# Forward slashes throughout: this path is baked into a C++ string literal, where a Windows
# backslash would start an escape sequence.
file(TO_CMAKE_PATH "${gkDotNetRoot}" gkDotNetRoot)
file(TO_CMAKE_PATH "${gkDotNetHostPack}" gkDotNetHostPack)

set(GK_DOTNET_ENABLED ON)
set(GK_DOTNET_RESOLVED_ROOT "${gkDotNetRoot}")
set(GK_DOTNET_RESOLVED_HOSTPACK "${gkDotNetHostPack}")
set(GK_DOTNET_EXE "${gkDotNetExe}")
set(GK_DOTNET_RID "${gkHostRid}")

if(GK_DOTNET_BACKEND STREQUAL "AOT")
    set(GK_DOTNET_USE_AOT 1)
else()
    set(GK_DOTNET_USE_AOT 0)
endif()

message(STATUS ".NET scripting enabled (${GK_DOTNET_BACKEND}) root=${gkDotNetRoot}")

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
        if(WIN32)
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
            COMMAND "${GK_DOTNET_EXE}" publish
                "${GK_DOTNET_MANAGED_ROOT}/GkNext.Bootstrap/GkNext.Bootstrap.csproj"
                -c Release -r ${GK_DOTNET_RID} -p:GkAot=true
                "-p:GkGameProject=${ARG_PROJECT}"
                -p:GkNativeName=${nativeName}
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
