#!/usr/bin/env bash

# Environment setup
export PICO_SDK_PATH="/Users/aroum/pico/pico-sdk"
export PATH="/opt/homebrew/bin:$PATH"

# Configuration
TARGET_NAME="omsk_wave"
BUILD_DIR="build_rp2350"
PICO_PLATFORM="rp2350"
PICO_BOARD="pico2"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Default flags
CLEAN=false
SIZE=true
FLASH=false

# Help function
show_help() {
    echo "Usage: ./build_all.sh [options]"
    echo "Options:"
    echo "  -c, --clean    Remove build directory and re-run CMake"
    echo "  -s, --size     Show detailed memory usage report"
    echo "  -f, --flash    Build and flash via picotool"
    echo "  -p <platform>  Specify platform (rp2040 or rp2350, default: rp2350)"
    echo "  -h, --help     Show this help message"
    echo ""
    echo "Example: ./build_all.sh -p rp2040 -c -s"
}

# Parse arguments
# Note: We handle simple combined flags like -csf using getopts
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

# Determine flash size based on platform
if [ "$PICO_PLATFORM" = "rp2040" ]; then
    FLASH_MAX=$((2 * 1024 * 1024))
else
    FLASH_MAX=$((4 * 1024 * 1024))
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

# 3. Always Build (Unless only help was requested)
# This ensures the binary is updated even if no flags like -s or -f are passed
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

# 4. Size Report
if [ "$SIZE" = true ]; then
    ELF_FILE="$BUILD_DIR/$TARGET_NAME.elf"
    if [ ! -f "$ELF_FILE" ]; then
        echo -e "${RED}Error: ELF file not found at $ELF_FILE${NC}"
    else
        echo -e "\n${GREEN}=== Detailed Memory Analysis (RP2350) ===${NC}"
        
        # Get raw data from arm-none-eabi-size
        SIZE_DATA=$(arm-none-eabi-size -A "$ELF_FILE")
        
        # Extract values (bytes)
        TEXT=$(echo "$SIZE_DATA" | grep "\.text" | awk '{print $2}' | head -n 1)
        RODATA=$(echo "$SIZE_DATA" | grep "\.rodata" | awk '{print $2}' | head -n 1)
        DATA=$(echo "$SIZE_DATA" | grep "\.data" | awk '{print $2}' | head -n 1)
        BSS=$(echo "$SIZE_DATA" | grep "\.bss" | awk '{print $2}' | head -n 1)
        HEAP=$(echo "$SIZE_DATA" | grep "\.heap" | awk '{print $2}' | head -n 1)
        STACK_DUMMY=$(echo "$SIZE_DATA" | grep "\.stack_dummy" | awk '{print $2}' | head -n 1)
        STACK1_DUMMY=$(echo "$SIZE_DATA" | grep "\.stack1_dummy" | awk '{print $2}' | head -n 1)
        
        # Default to 0 if empty
        TEXT=${TEXT:-0}; RODATA=${RODATA:-0}; DATA=${DATA:-0}; BSS=${BSS:-0}; 
        HEAP=${HEAP:-0}; STACK_DUMMY=${STACK_DUMMY:-0}; STACK1_DUMMY=${STACK1_DUMMY:-0}

        # Calculations
        TOTAL_FLASH=$((TEXT + RODATA + DATA))
        TOTAL_RAM=$((DATA + BSS + HEAP + STACK_DUMMY + STACK1_DUMMY))
        # Flash size based on platform
FLASH_PCT=$(echo "scale=2; $TOTAL_FLASH * 100 / $FLASH_MAX" | bc)
        # RAM is 520KB (532480 bytes)
        RAM_FREE=$((532480 - TOTAL_RAM)) 

        echo -e "${YELLOW}1. Flash (Non-volatile Memory)${NC}"
        echo "   .text:   $(printf "%'d" $TEXT) bytes (Code and instructions)"
        echo "   .rodata: $(printf "%'d" $RODATA) bytes (Constants/Samples/Wavetables)"
        echo "   .data:   $(printf "%'d" $DATA) bytes (Initial values for global vars)"
        echo -e "   -------------------------------------------"
        echo -e "   Total Flash Usage: ${GREEN}$((TOTAL_FLASH / 1024)) KB${NC} (~$FLASH_PCT% of 4MB)"

        echo -e "\n${YELLOW}2. RAM (Operating Memory)${NC}"
        echo "   .data:   $(printf "%'d" $DATA) bytes (Copied to RAM at boot)"
        echo "   .bss:    $(printf "%'d" $BSS) bytes (Zero-initialized variables)"
        echo "   .heap:   $(printf "%'d" $HEAP) bytes (Dynamic allocation pool)"
        echo "   .stacks: $((STACK_DUMMY + STACK1_DUMMY)) bytes (Core0 and Core1 stacks)"
        echo -e "   -------------------------------------------"
        echo -e "   Total RAM Usage:   ${GREEN}$((TOTAL_RAM / 1024)) KB${NC} out of 520 KB"
        
        if [ $RAM_FREE -lt 32768 ]; then
            echo -e "   ${RED}WARNING: Critical RAM level! Only $((RAM_FREE / 1024)) KB remaining.${NC}"
        else
            echo -e "   Remaining RAM:     $((RAM_FREE / 1024)) KB"
        fi
        echo ""
    fi
fi

# 5. Flashing
if [ "$FLASH" = true ]; then
    BIN_FILE="$BUILD_DIR/$TARGET_NAME.bin"
    echo -e "${YELLOW}=== Flashing Process ===${NC}"

    # Try magic reboot via CDC 1200 baud
    OS_TYPE=$(uname)
    PORT=""
    STTY_FLAG="-f"
    FALLBACK_PORTS="/dev/cu.usbmodem*"

    if [ "$OS_TYPE" = "Darwin" ]; then
        PORT=$(python3 -c '
import re, subprocess
def find_port():
    try:
        ioreg_data = subprocess.check_output(["ioreg", "-l"]).decode("utf-8")
    except Exception:
        return ""
    lines = ioreg_data.splitlines()
    for i, line in enumerate(lines):
        if "\"idVendor\" = 51966" in line or "idVendor = 51966" in line or "\"USB Product Name\" = \"OMSK\"" in line or "\"USB Product Name\" = \"Granular Synth\"" in line:
            device_idx = -1
            for k in range(i, -1, -1):
                if "+-o" in lines[k]:
                    device_idx = k
                    break
            if device_idx == -1:
                continue
            m_indent = re.match(r"^([ \|]*)\+\-o", lines[device_idx])
            parent_depth = len(m_indent.group(1)) if m_indent else 0
            for j in range(device_idx + 1, len(lines)):
                next_line = lines[j]
                m_next = re.match(r"^([ \|]*)\+\-o", next_line)
                if m_next:
                    next_depth = len(m_next.group(1))
                    if next_depth <= parent_depth:
                        break
                if "IOCalloutDevice" in next_line:
                    m = re.search(r"\"IOCalloutDevice\" = \"([^\"]+)\"", next_line)
                    if m:
                        return m.group(1)
    return ""
print(find_port())
' 2>/dev/null)
    else
        # Linux
        STTY_FLAG="-F"
        FALLBACK_PORTS="/dev/ttyACM*"
        for dev in /sys/class/tty/ttyACM*; do
            if [ -d "$dev" ]; then
                vid=$(cat "$dev/device/../idVendor" 2>/dev/null | tr -d '[:space:]')
                if [ "$vid" = "cafe" ]; then
                    PORT="/dev/$(basename "$dev")"
                    break
                fi
            fi
        done
    fi

    if [ -n "$PORT" ] && [ -e "$PORT" ]; then
        echo -e "${YELLOW}Found device port: $PORT. Rebooting to BOOTSEL...${NC}"
        stty $STTY_FLAG "$PORT" 1200 >/dev/null 2>&1
        sleep 1.5
    else
        # Fallback: try all matching ports
        for p in $FALLBACK_PORTS; do
            if [ -e "$p" ]; then
                echo -e "${YELLOW}Attempting fallback reboot on $p...${NC}"
                stty $STTY_FLAG "$p" 1200 >/dev/null 2>&1
            fi
        done
        sleep 1.5
    fi
    
    # Try magic reboot or check if already in BOOTSEL
    if ! picotool info > /dev/null 2>&1; then
        echo -e "${RED}Device not found in BOOTSEL mode or USB-ready mode.${NC}"
        echo -e "${YELLOW}Action Required:${NC} Hold BOOTSEL, press Reset, then release BOOTSEL."
        read -n 1 -s -r -p "Press any key when device is ready..."
        echo -e "\nContinuing..."
    fi

    echo "Uploading $BIN_FILE..."
    picotool load "$BIN_FILE" -u
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}Flash Successful! Program started.${NC}"
    else
        echo -e "${RED}Flash failed! Check connection.${NC}"
    fi
fi