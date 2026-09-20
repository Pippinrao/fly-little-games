# Mirror upstream so quote-local includes see the same patched class declarations
# in every translation unit. Never modify the submodule. GLOB tracks added files;
# configure dependencies track edits; COPYONLY keeps unchanged output timestamps.
set(_flynes_upstream_root "${NESTOPIA_ROOT}")
set(_flynes_mirror "${CMAKE_CURRENT_BINARY_DIR}/patched-nestopia")
file(GLOB_RECURSE _flynes_upstream_files CONFIGURE_DEPENDS LIST_DIRECTORIES false
    "${_flynes_upstream_root}/*")
set(NESTOPIA_SOURCES)
set(_flynes_patched_files NstApu.cpp NstSoundRenderer.hpp NstSoundRenderer.cpp)
foreach(_flynes_source IN LISTS _flynes_upstream_files)
    file(RELATIVE_PATH _flynes_relative "${_flynes_upstream_root}" "${_flynes_source}")
    if(_flynes_relative IN_LIST _flynes_patched_files)
        # Write patched content only once below, preserving unchanged timestamps.
        # These sources still need the dependency normally added by configure_file.
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_flynes_source}")
    else()
        configure_file("${_flynes_source}" "${_flynes_mirror}/${_flynes_relative}" COPYONLY)
    endif()
    if(_flynes_relative MATCHES "\\.cpp$")
        list(APPEND NESTOPIA_SOURCES "${_flynes_mirror}/${_flynes_relative}")
    endif()
endforeach()

# The upstream NTSC filter includes its sibling source/nes_ntsc headers/inlines.
file(GLOB_RECURSE _flynes_ntsc_files CONFIGURE_DEPENDS LIST_DIRECTORIES false
    "${_flynes_upstream_root}/../nes_ntsc/*")
foreach(_flynes_source IN LISTS _flynes_ntsc_files)
    file(RELATIVE_PATH _flynes_relative "${_flynes_upstream_root}/../nes_ntsc" "${_flynes_source}")
    configure_file("${_flynes_source}" "${_flynes_mirror}/../nes_ntsc/${_flynes_relative}" COPYONLY)
endforeach()

function(flynes_read_reviewed_source filename expected_hash output)
    file(READ "${_flynes_upstream_root}/${filename}" source)
    string(REPLACE "\r\n" "\n" source "${source}")
    string(SHA256 actual_hash "${source}")
    if(NOT actual_hash STREQUAL expected_hash)
        message(FATAL_ERROR "Nestopia ${filename} differs from the reviewed upstream source; review its deterministic-state patch before building")
    endif()
    set(${output} "${source}" PARENT_SCOPE)
endfunction()

function(flynes_replace_once variable old new)
    set(source "${${variable}}")
    string(REPLACE "${old}" "" removed "${source}")
    string(LENGTH "${source}" before_length)
    string(LENGTH "${removed}" after_length)
    string(LENGTH "${old}" context_length)
    math(EXPR delta "${before_length} - ${after_length}")
    if(NOT delta EQUAL context_length)
        message(FATAL_ERROR "Nestopia ${variable} patch context must occur exactly once")
    endif()
    string(REPLACE "${old}" "${new}" source "${source}")
    set(${variable} "${source}" PARENT_SCOPE)
endfunction()

flynes_read_reviewed_source(NstApu.cpp
    aeb9cd77f4d1483582d277320cc965abe049e24d444601ddb82816b6ff8f4b2f apu)
# Game Genie sound distortion is opt-in through Sound::SetGenie. Upstream
# leaves this flag indeterminate, so heap history can alter power-on APU writes.
flynes_replace_once(apu
    ": rate(44100), speed(0), muted(false), transpose(false), stereo(false), audible(true)"
    ": rate(44100), speed(0), muted(false), transpose(false), genie(false), stereo(false), audible(true)")
flynes_replace_once(apu
    "\t\tApu::Triangle::Triangle()\n\t\t: outputVolume(0) {}"
    "\t\tApu::Triangle::Triangle()\n\t\t: outputVolume(0), linearCtrl(0) {}")
flynes_replace_once(apu
    "\t\t\tdcBlocker.SaveState( state, AsciiId<'D','C','B'>::V );"
    "\t\t\tdcBlocker.SaveState( state, AsciiId<'D','C','B'>::V );\n\t\t\tstate.Begin( AsciiId<'B','F','R'>::V );\n\t\t\tbuffer.SaveState( state );\n\t\t\tstate.End();")
flynes_replace_once(apu
    "\t\tvoid Apu::LoadState(State::Loader& state)\n\t\t{"
    "\t\tvoid Apu::LoadState(State::Loader& state)\n\t\t{\n\t\t\t// Older NST files lack the output queue/history chunk.\n\t\t\tbuffer.Reset();\n\t\t\tbool bufferLoaded = false;")
flynes_replace_once(apu
    "\t\t\t\t\tcase AsciiId<'D','C','B'>::V:"
    "\t\t\t\t\tcase AsciiId<'B','F','R'>::V:\n\t\t\t\t\t\tif (bufferLoaded) throw RESULT_ERR_CORRUPT_FILE;\n\t\t\t\t\t\tbuffer.LoadState( state );\n\t\t\t\t\t\tbufferLoaded = true;\n\t\t\t\t\t\tbreak;\n\n\t\t\t\t\tcase AsciiId<'D','C','B'>::V:")

flynes_read_reviewed_source(NstSoundRenderer.hpp
    bda9c1483751f9fbb5eaa274f483dde68b4f8ce7016f23abec016a10a030ebe7 renderer_header)
flynes_replace_once(renderer_header
    "\t\tnamespace Sound"
    "\t\tnamespace State { class Saver; class Loader; }\n\t\tnamespace Sound")
flynes_replace_once(renderer_header
    "\t\t\t\tvoid Reset(bool=true);"
    "\t\t\t\tvoid Reset(bool=true);\n\t\t\t\tvoid SaveState(State::Saver&) const;\n\t\t\t\tvoid LoadState(State::Loader&);")

flynes_read_reviewed_source(NstSoundRenderer.cpp
    8b774ab7c2eb0eeebdaf191fde476fce3d6d02e1baaa2d4c058666593199822a renderer)
flynes_replace_once(renderer "#include <algorithm>"
    "#include <algorithm>\n#include <vector>\n#include \"NstState.hpp\"")
string(APPEND renderer [=[

// FlyNES optional APU/BFR chunk, format 1. State's Write/Read16/32 use
// little endian. Store FIFO/history in logical order, never inactive memory.
void Nes::Core::Sound::Buffer::SaveState(State::Saver& state) const
{
    const uint count = (pos + SIZE - start) & MASK;
    state.Write32(1).Write32(count);
    for (uint i = 0; i < count; ++i)
        state.Write16(static_cast<uint>(output[(start + i) & MASK]) & 0xFFFFU);
    for (uint i = 0; i < History::SIZE; ++i)
        state.Write16(static_cast<uint>(history.buffer[(history.pos + i) & History::MASK]) & 0xFFFFU);
}

void Nes::Core::Sound::Buffer::LoadState(State::Loader& state)
{
    const dword length = state.Length();
    if (length < 8) throw RESULT_ERR_CORRUPT_FILE;
    if (state.Read32() != 1) throw RESULT_ERR_UNSUPPORTED_FILE_VERSION;
    const dword count = state.Read32();
    if (count >= SIZE || length != 8 + 2 * (count + History::SIZE))
        throw RESULT_ERR_CORRUPT_FILE;
    // Validate/read the complete chunk before changing the output queue.
    std::vector<iword> samples(count + History::SIZE);
    for (uint i = 0; i < samples.size(); ++i)
    {
        const uint bits = state.Read16();
        samples[i] = static_cast<iword>(bits < 0x8000U ? static_cast<int>(bits)
                                                    : static_cast<int>(bits) - 0x10000);
    }
    Reset();
    std::copy(samples.begin(), samples.begin() + count, output);
    pos = count;
    std::copy(samples.begin() + count, samples.end(), history.buffer);
}
]=])

file(CONFIGURE OUTPUT "${_flynes_mirror}/NstApu.cpp" CONTENT "${apu}" @ONLY NEWLINE_STYLE UNIX)
file(CONFIGURE OUTPUT "${_flynes_mirror}/NstSoundRenderer.hpp" CONTENT "${renderer_header}" @ONLY NEWLINE_STYLE UNIX)
file(CONFIGURE OUTPUT "${_flynes_mirror}/NstSoundRenderer.cpp" CONTENT "${renderer}" @ONLY NEWLINE_STYLE UNIX)
set(NESTOPIA_ROOT "${_flynes_mirror}")
