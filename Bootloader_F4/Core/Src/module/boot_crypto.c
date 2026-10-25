/*
 * boot_crypto.c
 *
 *  Created on: Oct 25, 2026
 *      Author: paulb
 *
 *  SHA-256 peste imaginea din flash, apoi ECDSA-P256 verify cu cheia
 *  publica din boot_pubkey.h. Se folosesc variantele "LOWMEM_VERIFY" /
 *  "SMALL" din CMOX: doar codul de verificare ajunge in bootloader.
 */

#include "module/boot_crypto.h"
#include "module/boot_pubkey.h"
#include "cmox_crypto.h"
#include <string.h>

/* Memoria de lucru a CMOX pentru calculele pe curba (fara malloc) */
#define BOOT_ECC_BUF_SIZE       2048U

static uint8_t s_eccBuf[BOOT_ECC_BUF_SIZE];

bool BootCrypto_VerifyImage(const uint8_t *image, uint32_t size,
                            const uint8_t signature[BOOT_SIG_LEN])
{
    cmox_ecc_handle_t ctx;
    cmox_ecc_retval_t ret;
    uint8_t           digest[CMOX_SHA256_SIZE];
    size_t            digestLen = 0U;
    uint32_t          faultCheck = CMOX_ECC_AUTH_FAIL;

    if (cmox_hash_compute(CMOX_SHA256_ALGO, image, size,
                          digest, sizeof(digest), &digestLen) != CMOX_HASH_SUCCESS)
    {
        return false;
    }
    if (digestLen != sizeof(digest))
    {
        return false;
    }

    cmox_ecc_construct(&ctx, CMOX_MATH_FUNCS_SMALL, s_eccBuf, sizeof(s_eccBuf));
    ret = cmox_ecdsa_verify(&ctx, CMOX_ECC_SECP256R1_LOWMEM_VERIFY,
                            BOOT_PUBKEY, sizeof(BOOT_PUBKEY),
                            digest, sizeof(digest),
                            signature, BOOT_SIG_LEN,
                            &faultCheck);
    cmox_ecc_cleanup(&ctx);
    memset(s_eccBuf, 0, sizeof(s_eccBuf));

    /* Ambele trebuie sa fie AUTH_SUCCESS (0x6C726): un glitch care sare
       peste un test sau zeroizeaza un registru nu produce aceasta valoare */
    if (ret != CMOX_ECC_AUTH_SUCCESS)
    {
        return false;
    }
    if (faultCheck != CMOX_ECC_AUTH_SUCCESS)
    {
        return false;
    }
    return true;
}
