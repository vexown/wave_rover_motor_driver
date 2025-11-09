#!/bin/bash
################################################################################
# Quick validation script to check if OTA component is ready to use
################################################################################

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}═══════════════════════════════════════════════${NC}"
echo -e "${BLUE}   OTA Manager Component Validation${NC}"
echo -e "${BLUE}═══════════════════════════════════════════════${NC}"
echo ""

ERRORS=0
WARNINGS=0

# Check component structure
echo -e "${BLUE}Checking component structure...${NC}"

if [ -d "components/ota_manager" ]; then
    echo -e "${GREEN}✓ Component directory exists${NC}"
else
    echo -e "${RED}✗ Component directory not found${NC}"
    ((ERRORS++))
fi

if [ -f "components/ota_manager/CMakeLists.txt" ]; then
    echo -e "${GREEN}✓ CMakeLists.txt exists${NC}"
else
    echo -e "${RED}✗ CMakeLists.txt not found${NC}"
    ((ERRORS++))
fi

if [ -f "components/ota_manager/Include/ota_manager.h" ]; then
    echo -e "${GREEN}✓ Main header exists${NC}"
else
    echo -e "${RED}✗ ota_manager.h not found${NC}"
    ((ERRORS++))
fi

if [ -f "components/ota_manager/Source/ota_manager.c" ]; then
    echo -e "${GREEN}✓ Main implementation exists${NC}"
else
    echo -e "${RED}✗ ota_manager.c not found${NC}"
    ((ERRORS++))
fi

echo ""

# Check scripts
echo -e "${BLUE}Checking scripts...${NC}"

if [ -f "scripts/setup_ota_signing.sh" ]; then
    echo -e "${GREEN}✓ Setup script exists${NC}"
    if [ -x "scripts/setup_ota_signing.sh" ]; then
        echo -e "${GREEN}✓ Setup script is executable${NC}"
    else
        echo -e "${YELLOW}⚠ Setup script is not executable (run: chmod +x scripts/setup_ota_signing.sh)${NC}"
        ((WARNINGS++))
    fi
else
    echo -e "${RED}✗ Setup script not found${NC}"
    ((ERRORS++))
fi

if [ -f "scripts/sign_firmware.sh" ]; then
    echo -e "${GREEN}✓ Signing script exists${NC}"
    if [ -x "scripts/sign_firmware.sh" ]; then
        echo -e "${GREEN}✓ Signing script is executable${NC}"
    else
        echo -e "${YELLOW}⚠ Signing script is not executable (run: chmod +x scripts/sign_firmware.sh)${NC}"
        ((WARNINGS++))
    fi
else
    echo -e "${RED}✗ Signing script not found${NC}"
    ((ERRORS++))
fi

echo ""

# Check partition table
echo -e "${BLUE}Checking partition table...${NC}"

if [ -f "partitions_ota.csv" ]; then
    echo -e "${GREEN}✓ OTA partition table template exists${NC}"
    
    if grep -q "ota_0" "partitions_ota.csv" && grep -q "ota_1" "partitions_ota.csv"; then
        echo -e "${GREEN}✓ Partition table has ota_0 and ota_1${NC}"
    else
        echo -e "${YELLOW}⚠ Partition table missing ota_0 or ota_1${NC}"
        ((WARNINGS++))
    fi
else
    echo -e "${YELLOW}⚠ partitions_ota.csv not found (you can create your own)${NC}"
    ((WARNINGS++))
fi

echo ""

# Check .gitignore
echo -e "${BLUE}Checking .gitignore...${NC}"

if [ -f ".gitignore" ]; then
    if grep -q "ota_private_key.pem" ".gitignore"; then
        echo -e "${GREEN}✓ Private key is in .gitignore${NC}"
    else
        echo -e "${YELLOW}⚠ Private key not in .gitignore (will be added by setup script)${NC}"
        ((WARNINGS++))
    fi
else
    echo -e "${YELLOW}⚠ .gitignore not found${NC}"
    ((WARNINGS++))
fi

echo ""

# Check if keys exist
echo -e "${BLUE}Checking signing keys...${NC}"

if [ -f "ota_private_key.pem" ]; then
    echo -e "${GREEN}✓ Private key exists${NC}"
    
    if [ -f "components/ota_manager/Include/ota_public_key.h" ]; then
        echo -e "${GREEN}✓ Public key header exists${NC}"
    else
        echo -e "${YELLOW}⚠ Public key header not found (regenerate with setup script)${NC}"
        ((WARNINGS++))
    fi
else
    echo -e "${YELLOW}⚠ Keys not generated yet (run: ./scripts/setup_ota_signing.sh)${NC}"
    ((WARNINGS++))
fi

echo ""

# Check OpenSSL
echo -e "${BLUE}Checking dependencies...${NC}"

if command -v openssl >/dev/null 2>&1; then
    echo -e "${GREEN}✓ OpenSSL is installed${NC}"
else
    echo -e "${RED}✗ OpenSSL not found (required for signing)${NC}"
    ((ERRORS++))
fi

echo ""

# Summary
echo -e "${BLUE}═══════════════════════════════════════════════${NC}"

if [ $ERRORS -eq 0 ] && [ $WARNINGS -eq 0 ]; then
    echo -e "${GREEN}   ✓ All checks passed!${NC}"
    echo -e "${GREEN}   OTA component is ready to use${NC}"
elif [ $ERRORS -eq 0 ]; then
    echo -e "${YELLOW}   ⚠ $WARNINGS warnings found${NC}"
    echo -e "${YELLOW}   Component is usable but may need attention${NC}"
else
    echo -e "${RED}   ✗ $ERRORS errors found${NC}"
    echo -e "${RED}   Please fix errors before using component${NC}"
fi

echo -e "${BLUE}═══════════════════════════════════════════════${NC}"
echo ""

if [ $ERRORS -eq 0 ]; then
    echo -e "${BLUE}Next steps:${NC}"
    echo ""
    
    if [ ! -f "ota_private_key.pem" ]; then
        echo "  1. Generate signing keys:"
        echo "     $ ./scripts/setup_ota_signing.sh"
        echo ""
    fi
    
    echo "  2. Configure your project (see README.md)"
    echo "  3. Integrate in main.c (see ota_integration_example.c)"
    echo "  4. Build and sign firmware"
    echo ""
fi

exit $ERRORS
