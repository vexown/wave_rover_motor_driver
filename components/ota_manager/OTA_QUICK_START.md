# OTA Manager - Quick Reference

## One-Time Setup

```bash
# 1. Generate signing keys
./scripts/setup_ota_signing.sh

# 2. Configure partition table in CMakeLists.txt or sdkconfig
# Use the provided partitions_ota.csv
```

## Build & Deploy Workflow

```bash
# 1. Build firmware (idf.py build or if you use PlatformIO then just run the build option)

# 2. Sign firmware
./scripts/sign_firmware.sh

# 3. Upload via web interface
# Navigate to: http://<device-ip>:8080
# Upload: build/*_signed.bin
```

## Key Rotation (Replacing Key Pair)

If you need to replace your signing keys in the future:

```bash
# 1. Regenerate keys (choose 'y' to overwrite existing keys)
./scripts/setup_ota_signing.sh

# 2. Rebuild firmware (new public key gets embedded)
# (idf.py build or PlatformIO build)

# 3. Sign with new private key
./scripts/sign_firmware.sh

# 4. Flash via USB (REQUIRED - cannot OTA with new keys!)
# idf.py flash  (or PlatformIO upload)
```

**⚠️ Important:** After key rotation, you **MUST** flash via USB/serial at least once. Devices with the old public key embedded will reject firmware signed with the new private key. After the USB flash, all future OTA updates will work with the new key pair.