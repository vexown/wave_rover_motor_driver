#!/bin/bash
################################################################################
# @file sign_firmware.sh
# @brief Sign ESP32 firmware binary with ECDSA P-256 private key
#
# @details This script signs a compiled firmware binary for OTA updates.
#          It performs the following:
#          1. Locates the built firmware .bin file
#          2. Computes SHA-256 hash of the firmware
#          3. Signs the hash using ECDSA P-256 private key
#          4. Appends 64-byte signature to firmware
#          5. Creates signed firmware ready for OTA upload
#
# USAGE:
#   ./scripts/sign_firmware.sh [path/to/firmware.bin]
#
#   If no path is provided, auto-detects build output:
#     - build/*.bin (ESP-IDF standalone build)
#     - .pio/build/*/firmware.bin (PlatformIO build)
#
# PREREQUISITES:
#   - OpenSSL installed
#   - Private key generated (run ./scripts/setup_ota_signing.sh first)
#   - Firmware already built (idf.py build or pio run)
#
# OUTPUT:
#   - *_signed.bin (original firmware + 64-byte ECDSA signature)
#
################################################################################

set -e  # Exit on any error

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# File paths
PRIVATE_KEY_FILE="ota_private_key.pem"

echo -e "${BLUE}═══════════════════════════════════════════════${NC}"
echo -e "${BLUE}   ESP32 Firmware Signing Tool${NC}"
echo -e "${BLUE}   ECDSA P-256 Signature Generation${NC}"
echo -e "${BLUE}═══════════════════════════════════════════════${NC}"
echo ""

################################################################################
# Step 1: Check prerequisites
################################################################################

# Check if OpenSSL is installed
if ! command -v openssl &> /dev/null; then
    echo -e "${RED}✗ Error: OpenSSL not found!${NC}"
    echo "  Please install OpenSSL"
    exit 1
fi

echo -e "${GREEN}✓ OpenSSL available${NC}"

# Check if private key exists
if [ ! -f "$PRIVATE_KEY_FILE" ]; then
    echo -e "${RED}✗ Error: Private key not found: $PRIVATE_KEY_FILE${NC}"
    echo ""
    echo "  Please generate keys first:"
    echo "  $ ./scripts/setup_ota_signing.sh"
    exit 1
fi

echo -e "${GREEN}✓ Private key found: $PRIVATE_KEY_FILE${NC}"
echo ""

################################################################################
# Step 2: Locate firmware binary
################################################################################

FIRMWARE_BIN=""

# If firmware path provided as argument, use it
if [ -n "$1" ]; then
    if [ ! -f "$1" ]; then
        echo -e "${RED}✗ Error: Firmware file not found: $1${NC}"
        exit 1
    fi
    FIRMWARE_BIN="$1"
    echo -e "${BLUE}Using provided firmware: $FIRMWARE_BIN${NC}"
else
    # Auto-detect firmware location
    echo -e "${BLUE}Auto-detecting firmware binary...${NC}"
    
    # Try ESP-IDF build directory first
    if [ -d "build" ]; then
        # Look for .bin files in build/ (exclude bootloader, partition table)
        CANDIDATES=$(find build -maxdepth 1 -name "*.bin" ! -name "bootloader.bin" ! -name "partition-table.bin" 2>/dev/null)
        
        if [ -n "$CANDIDATES" ]; then
            # Use the first (usually only) candidate
            FIRMWARE_BIN=$(echo "$CANDIDATES" | head -n 1)
        fi
    fi
    
    # Try PlatformIO build directory if ESP-IDF not found
    if [ -z "$FIRMWARE_BIN" ] && [ -d ".pio/build" ]; then
        CANDIDATES=$(find .pio/build -name "firmware.bin" 2>/dev/null)
        
        if [ -n "$CANDIDATES" ]; then
            FIRMWARE_BIN=$(echo "$CANDIDATES" | head -n 1)
        fi
    fi
    
    # Check if we found anything
    if [ -z "$FIRMWARE_BIN" ]; then
        echo -e "${RED}✗ Error: No firmware binary found!${NC}"
        echo ""
        echo "  Searched in:"
        echo "    - build/*.bin (ESP-IDF)"
        echo "    - .pio/build/*/firmware.bin (PlatformIO)"
        echo ""
        echo "  Please build firmware first:"
        echo "    ESP-IDF: $ idf.py build"
        echo "    PlatformIO: $ pio run"
        echo ""
        echo "  Or specify firmware path manually:"
        echo "    $ ./scripts/sign_firmware.sh path/to/firmware.bin"
        exit 1
    fi
    
    echo -e "${GREEN}✓ Found firmware: $FIRMWARE_BIN${NC}"
fi

# Get firmware size
FIRMWARE_SIZE=$(stat -f%z "$FIRMWARE_BIN" 2>/dev/null || stat -c%s "$FIRMWARE_BIN" 2>/dev/null)
echo -e "  Size: $FIRMWARE_SIZE bytes ($(echo "scale=2; $FIRMWARE_SIZE/1024" | bc) KB)"
echo ""

################################################################################
# Step 3: Sign the firmware
################################################################################

echo -e "${BLUE}Signing firmware with ECDSA P-256...${NC}"

# Temporary files
SIGNATURE_DER=$(mktemp /tmp/ota_signature_der.XXXXXX)
trap "rm -f $SIGNATURE_DER" EXIT

# Step 1: Generate DER signature
openssl dgst -sha256 -sign "$PRIVATE_KEY_FILE" -out "$SIGNATURE_DER" "$FIRMWARE_BIN" 2>/dev/null

if [ ! -f "$SIGNATURE_DER" ]; then
    echo -e "${RED}✗ Signing failed!${NC}"
    exit 1
fi

echo -e "${GREEN}✓ DER signature generated${NC}"

# Step 2: Extract R and S values from DER and convert to raw format (32 bytes each)
# Parse DER SEQUENCE and extract INTEGER R and INTEGER S
R_HEX=$(openssl asn1parse -in "$SIGNATURE_DER" -inform DER | grep 'INTEGER' | head -n1 | awk '{print $7}' | sed 's/://g')
S_HEX=$(openssl asn1parse -in "$SIGNATURE_DER" -inform DER | grep 'INTEGER' | tail -n1 | awk '{print $7}' | sed 's/://g')

# Remove leading '00' padding if present (DER adds it for positive integers with MSB set)
R_HEX=${R_HEX#00}
S_HEX=${S_HEX#00}

# Pad to exactly 32 bytes (64 hex chars) if needed
R_HEX=$(printf '%064s' "$R_HEX" | tr ' ' '0')
S_HEX=$(printf '%064s' "$S_HEX" | tr ' ' '0')

echo -e "${GREEN}✓ Signature converted to raw R+S format (64 bytes)${NC}"
echo -e "  R: ${R_HEX:0:32}...${R_HEX:32}"
echo -e "  S: ${S_HEX:0:32}...${S_HEX:32}"

################################################################################
# Step 4: Create signed firmware (firmware + signature)
################################################################################

# Determine output filename
FIRMWARE_DIR=$(dirname "$FIRMWARE_BIN")
FIRMWARE_NAME=$(basename "$FIRMWARE_BIN" .bin)
SIGNED_FIRMWARE="${FIRMWARE_DIR}/${FIRMWARE_NAME}_signed.bin"

echo -e "${BLUE}Creating signed firmware...${NC}"

# Combine R and S into 64-byte raw signature
RAW_SIG_HEX="${R_HEX}${S_HEX}"

# Concatenate firmware + raw signature (64 bytes)
cp "$FIRMWARE_BIN" "$SIGNED_FIRMWARE"
echo -n "$RAW_SIG_HEX" | xxd -r -p >> "$SIGNED_FIRMWARE"

if [ ! -f "$SIGNED_FIRMWARE" ]; then
    echo -e "${RED}✗ Failed to create signed firmware!${NC}"
    exit 1
fi

SIGNED_SIZE=$(stat -f%z "$SIGNED_FIRMWARE" 2>/dev/null || stat -c%s "$SIGNED_FIRMWARE" 2>/dev/null)
SIGNATURE_SIZE=64

echo -e "${GREEN}✓ Signed firmware created successfully!${NC}"
echo ""

################################################################################
# Step 5: Display summary
################################################################################

echo -e "${GREEN}═══════════════════════════════════════════════${NC}"
echo -e "${GREEN}   ✓ Firmware Signed Successfully${NC}"
echo -e "${GREEN}═══════════════════════════════════════════════${NC}"
echo ""
echo "  📄 Input firmware:  $FIRMWARE_BIN"
echo "     Size: $FIRMWARE_SIZE bytes"
echo ""
echo "  📦 Signed firmware: $SIGNED_FIRMWARE"
echo "     Size: $SIGNED_SIZE bytes (firmware + $SIGNATURE_SIZE byte signature)"
echo ""
echo -e "${BLUE}NEXT STEPS:${NC}"
echo ""
echo "  1. Start your ESP32 device with OTA enabled"
echo ""
echo "  2. Navigate to the OTA web interface:"
echo "     http://<device-ip>:8080"
echo ""
echo "  3. Upload the SIGNED firmware:"
echo "     $SIGNED_FIRMWARE"
echo ""
echo "  4. After successful upload and reboot, the new firmware MUST call:"
echo "     ota_manager_mark_valid()"
echo "     within a reasonable timeout to confirm it's working!"
echo ""
echo -e "${YELLOW}⚠ WARNING:${NC}"
echo "  - Only upload the SIGNED firmware (*_signed.bin)"
echo "  - Do NOT upload the unsigned .bin file"
echo "  - Signature verification will fail for unsigned files"
echo ""
echo -e "${GREEN}Happy updating! 🚀${NC}"
echo ""
