/*
 * boot_crypto.h
 *
 *  Created on: Oct 25, 2026
 *      Author: paulb
 *
 *  Verificarea semnaturii imaginii aplicatiei: SHA-256 + ECDSA-P256
 *  (X-CUBE-CRYPTOLIB / CMOX). Singurul fisier care depinde de CMOX.
 *
 *  Cheia publica e in boot_pubkey.h (generat de Tools/sb_sign.py keygen).
 */

#ifndef INC_MODULE_BOOT_CRYPTO_H_
#define INC_MODULE_BOOT_CRYPTO_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOOT_SIG_LEN            64U     /* r||s, big-endian */

/* true doar daca semnatura e valida pentru SHA-256(image[0:size]) */
bool BootCrypto_VerifyImage(const uint8_t *image, uint32_t size,
                            const uint8_t signature[BOOT_SIG_LEN]);

#ifdef __cplusplus
}
#endif

#endif /* INC_MODULE_BOOT_CRYPTO_H_ */
