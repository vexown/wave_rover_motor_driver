/******************************************************************************
 * @file ota_signature.h
 * @brief Header file for ECDSA P-256 firmware signature verification
 *
 * @details This module provides cryptographic verification of firmware binaries
 *          using ECDSA (Elliptic Curve Digital Signature Algorithm) with the
 *          secp256r1 curve (also known as NIST P-256 or prime256v1).
 * 
 *          SIGNATURE FORMAT:
 *          Signed firmware files have the signature appended to the end:
 *          [Firmware Binary Data] + [64-byte ECDSA Signature]
 * 
 *          The signature is computed as:
 *          signature = ECDSA_sign(SHA256(firmware_data), private_key)
 * 
 *          VERIFICATION PROCESS:
 *          1. Split uploaded file into firmware + signature (last 64 bytes)
 *          2. Compute SHA-256 hash of firmware data
 *          3. Verify signature using embedded public key
 *          4. Only flash firmware if signature is valid
 * 
 *          SECURITY PROPERTIES:
 *          - Public key is embedded in firmware (compile-time constant)
 *          - Private key is kept secret on developer's machine
 *          - Signature proves firmware was created by holder of private key
 *          - Any modification to firmware invalidates signature
 *          - P-256 provides ~128-bit security level (equivalent to RSA-3072)
 * 
 ******************************************************************************/

#ifndef OTA_SIGNATURE_H
#define OTA_SIGNATURE_H

/*******************************************************************************/
/*                                 INCLUDES                                    */
/*******************************************************************************/
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

/*******************************************************************************/
/*                                  MACROS                                     */
/*******************************************************************************/

/* ECDSA P-256 (secp256r1) signature size in bytes
 * ECDSA signature consists of two 32-byte integers (r, s)
 * Total: 32 + 32 = 64 bytes */
#define ECDSA_SIGNATURE_SIZE 64

/* ECDSA P-256 public key size in uncompressed format
 * Format: 0x04 || X || Y (1 byte prefix + 32-byte X coord + 32-byte Y coord)
 * Total: 1 + 32 + 32 = 65 bytes */
#define ECDSA_PUBLIC_KEY_SIZE 65

/* SHA-256 hash output size in bytes */
#define SHA256_HASH_SIZE 32

/*******************************************************************************/
/*                                DATA TYPES                                   */
/*******************************************************************************/

/*******************************************************************************/
/*                     GLOBAL VARIABLES DECLARATIONS                           */
/*******************************************************************************/

/*******************************************************************************/
/*                     GLOBAL FUNCTION DECLARATIONS                            */
/*******************************************************************************/

/**
 * @brief Verify ECDSA P-256 signature of firmware binary
 *
 * @details This function performs cryptographic verification to ensure that
 *          the provided firmware was signed by the holder of the private key
 *          corresponding to the embedded public key.
 * 
 *          VERIFICATION STEPS:
 *          1. Compute SHA-256 hash of firmware data
 *          2. Parse signature into (r, s) components
 *          3. Verify: ECDSA_verify(hash, signature, public_key)
 *          4. Return true only if signature is mathematically valid
 * 
 *          IMPORTANT NOTES:
 *          - The public key is embedded at compile time (see ota_public_key.h)
 *          - Signature must be in raw format (64 bytes: r || s), NOT DER-encoded
 *          - Uses mbedTLS cryptographic library (hardware-accelerated on ESP32)
 * 
 *          PERFORMANCE:
 *          - Verification takes ~40-80ms on ESP32 (depends on CPU clock)
 *          - Uses hardware crypto acceleration if enabled in menuconfig
 *          - Memory usage: ~2KB stack + signature/key buffers
 * 
 *          SECURITY:
 *          - Resistant to signature forgery without private key
 *          - Prevents firmware tampering/modification
 *          - Does NOT encrypt firmware (data is plaintext)
 *          - Does NOT prevent replay of old signed firmware (use version check)
 *
 * @param[in] firmware_data Pointer to firmware binary (WITHOUT signature)
 * @param[in] firmware_len Length of firmware data in bytes
 * @param[in] signature Pointer to 64-byte ECDSA signature (r || s format)
 *
 * @return
 *      - true if signature is valid (firmware authenticated)
 *      - false if signature is invalid or verification fails
 */
bool ota_signature_verify(const uint8_t *firmware_data, 
                          size_t firmware_len,
                          const uint8_t *signature);

/**
 * @brief Verify ECDSA P-256 signature against pre-computed hash
 *
 * @details This is a lower-level verification function that accepts a
 *          SHA-256 hash instead of the raw firmware data. Useful for
 *          incremental/streaming verification where you compute the hash
 *          yourself in chunks.
 * 
 *          This is used internally for verifying firmware read back from
 *          flash in chunks (memory-efficient verification).
 *
 * @param[in] firmware_hash SHA-256 hash of firmware (32 bytes)
 * @param[in] hash_len Length of hash (must be 32)
 * @param[in] signature Pointer to 64-byte ECDSA signature (r || s format)
 *
 * @return
 *      - true if signature is valid
 *      - false if signature is invalid or verification fails
 */
bool ota_signature_verify_hash(const uint8_t *firmware_hash,
                               size_t hash_len,
                               const uint8_t *signature);

/**
 * @brief Extract signature from signed firmware file
 *
 * @details Signed firmware files have the format:
 *          [Firmware Data] + [64-byte Signature]
 * 
 *          This helper function splits the file into:
 *          - firmware_data: Points to original buffer, length reduced by 64
 *          - signature: Points to last 64 bytes of original buffer
 * 
 *          Example usage:
 *          @code
 *            uint8_t *signed_firmware = uploaded_data;
 *            size_t signed_len = uploaded_size;
 *            
 *            const uint8_t *firmware;
 *            size_t firmware_len;
 *            const uint8_t *signature;
 *            
 *            if (ota_signature_extract(signed_firmware, signed_len,
 *                                       &firmware, &firmware_len, &signature)) {
 *                if (ota_signature_verify(firmware, firmware_len, signature)) {
 *                    ESP_LOGI(TAG, "Signature valid!");
 *                }
 *            }
 *          @endcode
 * 
 *          WARNING: This function does NOT copy data! The output pointers
 *                   point into the input buffer. The input buffer must remain
 *                   valid while using the output pointers.
 *
 * @param[in] signed_firmware Pointer to signed firmware buffer (firmware + signature)
 * @param[in] signed_len Total length of signed firmware (must be > 64 bytes)
 * @param[out] firmware_data Output pointer to firmware portion (points into input buffer)
 * @param[out] firmware_len Output length of firmware (signed_len - 64)
 * @param[out] signature Output pointer to signature portion (points to last 64 bytes)
 *
 * @return
 *      - ESP_OK if extraction successful
 *      - ESP_ERR_INVALID_ARG if any pointer is NULL or signed_len <= 64
 */
esp_err_t ota_signature_extract(const uint8_t *signed_firmware,
                                size_t signed_len,
                                const uint8_t **firmware_data,
                                size_t *firmware_len,
                                const uint8_t **signature);

#endif /* OTA_SIGNATURE_H */
