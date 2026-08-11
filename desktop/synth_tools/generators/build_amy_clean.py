import os
import subprocess
import sys
import shutil

# Paths
BASE_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
AMY_LIB_DIR = os.path.join(BASE_DIR, "sw/lib/amy")
EMULATOR_DIR = os.path.join(BASE_DIR, "emulator")
BIN_DIR = os.path.join(EMULATOR_DIR, "bin")
BUILD_DIR = "/tmp/amy_build_clean"

def run_cmd(cmd):
    print(f"Running: {cmd}")
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"Error: {result.stderr}")
        sys.exit(1)
    return result.stdout.strip()

def build():
    if not os.path.exists(BIN_DIR):
        os.makedirs(BIN_DIR)
    if not os.path.exists(BUILD_DIR):
        os.makedirs(BUILD_DIR)

    print("Step 1: Preparing local amy package and generating constants.py...")
    # Copy the 'amy' python package to BIN_DIR so we don't touch the original
    local_amy_pkg = os.path.join(BIN_DIR, "amy")
    if os.path.exists(local_amy_pkg):
        shutil.rmtree(local_amy_pkg)
    shutil.copytree(os.path.join(AMY_LIB_DIR, "amy"), local_amy_pkg)

    # Generate constants.py into the LOCAL amy package
    amy_h = os.path.join(AMY_LIB_DIR, "src/amy.h")
    local_constants_py = os.path.join(local_amy_pkg, "constants.py")
    cmd_gen_const = f"cat {amy_h} | sed -e 's@^//.*@@' | egrep 'define +[^ ]+ +[.0-9-]+' | sed -e 's/\\([.0-9]\\)f$/\\1/' | awk '{{print $2 \"=\" $3}}' > {local_constants_py}"
    run_cmd(cmd_gen_const)

    # Create dummy patches.h in BUILD_DIR to satisfy #include "patches.h"
    patches_h = os.path.join(BUILD_DIR, "patches.h")
    with open(patches_h, "w") as f:
        f.write("#ifndef __PATCHESH\n#define __PATCHESH\n")
        f.write("static const char * const patch_commands[258] = {0};\n")
        f.write("const uint16_t patch_oscs[258] = {0};\n")
        f.write("#endif\n")

    print("Step 2: Compiling C sources...")
    py_includes = run_cmd("python3-config --includes")
    
    comp_args = [
        "-fPIC",
        "-O3",
        "-DAMY_WAVETABLE",
        "-Wno-unused-but-set-variable",
        "-Wno-unreachable-code",
        "-I" + os.path.join(AMY_LIB_DIR, "src"),
        "-I" + BUILD_DIR,
        py_includes
    ]
    
    if sys.platform == "darwin":
        comp_args += ["-DMACOS", "-I/opt/homebrew/include"]
        link_args = [
            "-L/opt/homebrew/lib",
            "-lpthread",
            "-framework CoreAudio",
            "-framework AudioToolbox",
            "-framework AudioUnit",
            "-framework CoreFoundation",
            "-framework CoreMIDI",
            "-framework Cocoa",
            "-lstdc++",
            "-dynamiclib",
            "-undefined dynamic_lookup"
        ]
        ext = "so" 
    else:
        link_args = ["-lpthread", "-shared"]
        ext = "so"

    SOURCES = [
        'algorithms.c', 'amy.c', 'delay.c', 'envelope.c', 'filters.c', 'parse.c',
        'sequencer.c', 'transfer.c', 'midi_mappings.c', 'custom.c', 'patches.c',
        'libminiaudio-audio.c', 'oscillators.c', 'interp_partials.c', 'pcm.c',
        'pyamy.c', 'log2_exp2.c', 'instrument.c', 'amy_midi.c', 'api.c'
    ]
    if sys.platform == "darwin":
        SOURCES.append('macos_midi.m')

    objs = []
    for src in SOURCES:
        obj = os.path.join(BUILD_DIR, src.replace('.c', '.o').replace('.m', '.o'))
        src_path = os.path.join(AMY_LIB_DIR, "src", src)
        cmd = f"gcc {' '.join(comp_args)} -c {src_path} -o {obj}"
        run_cmd(cmd)
        objs.append(obj)

    print("Step 3: Linking...")
    # c_amy.so MUST be alongside the amy/ package or inside it if we use relative imports.
    # In AMY's __init__.py it does `import c_amy as _amy`.
    # So c_amy.so should be in the same directory as amy/ (i.e. in BIN_DIR)
    output_so = os.path.join(BIN_DIR, f"c_amy.{ext}")
    cmd_link = f"gcc {' '.join(objs)} {' '.join(link_args)} -o {output_so}"
    run_cmd(cmd_link)

    print(f"\nSuccess! Built AMY extension to {output_so}")
    print(f"Library folder {AMY_LIB_DIR} remains clean.")

if __name__ == "__main__":
    build()
