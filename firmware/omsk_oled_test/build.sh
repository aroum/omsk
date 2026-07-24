#!/usr/bin/env bash

# Environment setup
export PICO_SDK_PATH="/Users/aroum/pico/pico-sdk"
export PATH="/opt/homebrew/bin:$PATH"

# Configuration
TARGET_NAME="omsk_oled_test"
BUILD_DIR="build_rp2040"
PICO_PLATFORM="rp2040"
PICO_BOARD="pico"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Default flags
CLEAN=false
FLASH=false

# Parse arguments
while getopts "cfp:" opt; do
    case "${opt}" in
        c) CLEAN=true ;;
        f) FLASH=true ;;
        p) 
            if [ "$OPTARG" = "rp2040" ] || [ "$OPTARG" = "rp2350" ]; then
                PICO_PLATFORM="$OPTARG"
                BUILD_DIR="build_${OPTARG}"
                if [ "$OPTARG" = "rp2040" ]; then
                    PICO_BOARD="pico"
                else
                    PICO_BOARD="pico2"
                fi
            else
                echo -e "${RED}Invalid platform: $OPTARG. Use rp2040 or rp2350.${NC}"
                exit 1
            fi
            ;;
        *) echo "Usage: ./build.sh [-c] [-f] [-p rp2040|rp2350]"; exit 1 ;;
    esac
done

# Clean if requested
if [ "$CLEAN" = true ]; then
    echo -e "${YELLOW}=== Cleaning build directory ===${NC}"
    rm -rf "$BUILD_DIR"
fi

# Setup Build Directory and CMake
if [ ! -d "$BUILD_DIR" ]; then
    echo -e "${YELLOW}=== Initializing CMake ===${NC}"
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR" || exit
    cmake -DPICO_PLATFORM=$PICO_PLATFORM -DPICO_BOARD=$PICO_BOARD ..
    cd ..
fi

# Build
echo -e "${YELLOW}=== Building $TARGET_NAME ===${NC}"
cd "$BUILD_DIR" || exit

if [[ "$OSTYPE" == "darwin"* ]]; then
    JOBS=$(sysctl -n hw.ncpu)
else
    JOBS=$(nproc 2>/dev/null || echo 1)
fi

make -j"$JOBS"
if [ $? -ne 0 ]; then
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi
cd ..

# Flashing
if [ "$FLASH" = true ]; then
    BIN_FILE="$BUILD_DIR/$TARGET_NAME.bin"
    echo -e "${YELLOW}=== Flashing Process ===${NC}"
    
    if ! picotool info > /dev/null 2>&1; then
        echo -e "${RED}Device not found in BOOTSEL mode.${NC}"
        echo -e "${YELLOW}Action Required:${NC} Hold BOOTSEL, press Reset, then release BOOTSEL."
        read -n 1 -s -r -p "Press any key when device is ready..."
        echo -e "\nContinuing..."
    fi

    echo "Uploading $BIN_FILE..."
    picotool load "$BIN_FILE" -u -x
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}Flash Successful! Program started.${NC}"
    else
        echo -e "${RED}Flash failed! Check connection.${NC}"
    fi
fi
