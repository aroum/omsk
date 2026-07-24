#!/usr/bin/env bash

# Environment setup
export PICO_SDK_PATH="/Users/aroum/pico/pico-sdk"
export PATH="/opt/homebrew/bin:$PATH"

# Configuration
TARGET_NAME="omsk_midi"
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
SIZE=false
FLASH=false

# Help function
show_help() {
    echo "Usage: ./build_all.sh [options]"
    echo "Options:"
    echo "  -c, --clean    Remove build directory and re-run CMake"
    echo "  -s, --size     Show detailed memory usage report"
    echo "  -f, --flash    Build and flash via picotool"
    echo "  -p <platform>  Specify platform (rp2040 or rp2350, default: rp2040)"
    echo "  -h, --help     Show this help message"
    echo ""
    echo "Example: ./build_all.sh -p rp2350 -c -s"
}

# Parse arguments
while getopts "csfhp:" opt; do
    case "${opt}" in
        c) CLEAN=true ;;
        s) SIZE=true ;;
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
        h) show_help; exit 0 ;;
        *) show_help; exit 1 ;;
    esac
done

# 1. Clean if requested
if [ "$CLEAN" = true ]; then
    echo -e "${YELLOW}=== Cleaning build directory ===${NC}"
    rm -rf "$BUILD_DIR"
fi

# 2. Setup Build Directory and CMake
if [ ! -d "$BUILD_DIR" ]; then
    echo -e "${YELLOW}=== Initializing CMake ===${NC}"
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR" || exit
    cmake -DPICO_PLATFORM=$PICO_PLATFORM -DPICO_BOARD=$PICO_BOARD ..
    cd ..
fi

# 3. Always Build
echo -e "${YELLOW}=== Building $TARGET_NAME ===${NC}"
cd "$BUILD_DIR" || exit

# Determine number of cores for faster build
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

echo -e "${GREEN}=== Build Successful! ===${NC}"
echo -e "Binary: $BUILD_DIR/$TARGET_NAME.uf2"

# 4. Size Report
if [ "$SIZE" = true ]; then
    ELF_FILE="$BUILD_DIR/$TARGET_NAME.elf"
    if [ ! -f "$ELF_FILE" ]; then
        echo -e "${RED}Error: ELF file not found at $ELF_FILE${NC}"
    else
        echo -e "\n${GREEN}=== Detailed Memory Analysis ($PICO_PLATFORM) ===${NC}"
        
        # Get raw data from arm-none-eabi-size
        SIZE_DATA=$(arm-none-eabi-size -A "$ELF_FILE")
        
        # Extract values (bytes)
        TEXT=$(echo "$SIZE_DATA" | grep "\.text" | awk '{print $2}' | head -n 1)
        RODATA=$(echo "$SIZE_DATA" | grep "\.rodata" | awk '{print $2}' | head -n 1)
        DATA=$(echo "$SIZE_DATA" | grep "\.data" | awk '{print $2}' | head -n 1)
        BSS=$(echo "$SIZE_DATA" | grep "\.bss" | awk '{print $2}' | head -n 1)
        HEAP=$(echo "$SIZE_DATA" | grep "\.heap" | awk '{print $2}' | head -n 1)
        STACK_DUMMY=$(echo "$SIZE_DATA" | grep "\.stack_dummy" | awk '{print $2}' | head -n 1)
        
        # Default to 0 if empty
        TEXT=${TEXT:-0}; RODATA=${RODATA:-0}; DATA=${DATA:-0}; BSS=${BSS:-0}; 
        HEAP=${HEAP:-0}; STACK_DUMMY=${STACK_DUMMY:-0}

        # Calculations
        TOTAL_FLASH=$((TEXT + RODATA + DATA))
        TOTAL_RAM=$((DATA + BSS + HEAP + STACK_DUMMY))
        
        if [ "$PICO_PLATFORM" = "rp2350" ]; then
            FLASH_TOTAL=4194304
            RAM_TOTAL=532480
        else
            FLASH_TOTAL=2097152
            RAM_TOTAL=270336
        fi
        
        FLASH_PCT=$(echo "scale=2; $TOTAL_FLASH * 100 / $FLASH_TOTAL" | bc)
        RAM_FREE=$((RAM_TOTAL - TOTAL_RAM)) 

        echo -e "${YELLOW}1. Flash (Non-volatile Memory)${NC}"
        echo "   .text:   $(printf "%'d" $TEXT) bytes"
        echo "   .rodata: $(printf "%'d" $RODATA) bytes"
        echo "   .data:   $(printf "%'d" $DATA) bytes"
        echo -e "   -------------------------------------------"
        echo -e "   Total Flash Usage: ${GREEN}$((TOTAL_FLASH / 1024)) KB${NC} (~$FLASH_PCT% of $((FLASH_TOTAL/1024/1024))MB)"

        echo -e "\n${YELLOW}2. RAM (Operating Memory)${NC}"
        echo "   .data:   $(printf "%'d" $DATA) bytes"
        echo "   .bss:    $(printf "%'d" $BSS) bytes"
        echo "   .heap:   $(printf "%'d" $HEAP) bytes"
        echo "   .stack:  $(printf "%'d" $STACK_DUMMY) bytes"
        echo -e "   -------------------------------------------"
        echo -e "   Total RAM Usage:   ${GREEN}$((TOTAL_RAM / 1024)) KB${NC} out of $((RAM_TOTAL / 1024)) KB"
        echo "   Remaining RAM:     $((RAM_FREE / 1024)) KB"
        echo ""
    fi
fi

# 5. Flashing
if [ "$FLASH" = true ]; then
    BIN_FILE="$BUILD_DIR/$TARGET_NAME.bin"
    echo -e "${YELLOW}=== Flashing Process ===${NC}"
    
    if ! picotool info > /dev/null 2>&1; then
        echo -e "${RED}Device not found in BOOTSEL mode or USB-ready mode.${NC}"
        echo -e "${YELLOW}Action Required:${NC} Hold BOOTSEL, press Reset, then release BOOTSEL."
        read -n 1 -s -r -p "Press any key when device is ready..."
        echo -e "\nContinuing..."
    fi

    echo "Uploading $BIN_FILE..."
    picotool load "$BIN_FILE" -x
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}Flash Successful! Program started.${NC}"
    else
        echo -e "${RED}Flash failed! Check connection.${NC}"
    fi
fi
