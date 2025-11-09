#!/usr/bin/env python3
"""
Extract firmware version from CMakeLists.txt and pass to build system

This script is called by PlatformIO before compilation to extract the
firmware version from the root CMakeLists.txt and inject it as a 
compile-time definition (FIRMWARE_VERSION).

This ensures the version is defined in ONE place only (CMakeLists.txt).
"""
import re

Import("env")

# Read version from CMakeLists.txt
try:
    with open("CMakeLists.txt", "r") as f:
        cmake_content = f.read()
        
    # Extract version using regex: set(PROJECT_VER "x.y.z")
    match = re.search(r'set\(PROJECT_VER\s+"([^"]+)"\)', cmake_content)

    if match:
        version = match.group(1)
        print(f"✓ Extracted firmware version from CMakeLists.txt: {version}")
        
        # Add as build flag with proper escaping for C string
        env.Append(CPPDEFINES=[
            ("FIRMWARE_VERSION", f'\\"{version}\\"')
        ])
    else:
        print("⚠ Warning: Could not extract PROJECT_VER from CMakeLists.txt")
        print("  Using fallback version: dev")
        env.Append(CPPDEFINES=[
            ("FIRMWARE_VERSION", '\\"dev\\"')
        ])
        
except FileNotFoundError:
    print("✗ Error: CMakeLists.txt not found!")
    env.Append(CPPDEFINES=[
        ("FIRMWARE_VERSION", '\\"unknown\\"')
    ])
except Exception as e:
    print(f"✗ Error extracting version: {e}")
    env.Append(CPPDEFINES=[
        ("FIRMWARE_VERSION", '\\"error\\"')
    ])
