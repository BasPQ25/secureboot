/*
 * boot.h
 *
 *  Created on: Oct 13, 2026
 *      Author: paulb
 *
 *  Bootloader UART (USART2 / VCOM ST-LINK) pentru STM32F446RE.
 *
 *  Harta flash (definita in secureboot/Common/memory_map.ld, comuna cu Safety):
 *    0x08000000 - 0x08007FFF  bootloader (sectoarele 0-1, 32 KB)
 *    0x08008000 - 0x0800BFFF  date aplicatie (sectorul 2, nu se sterge aici)
 *    0x0800C000 - 0x0807FFFF  slot (sectoarele 3-7, 464 KB):
 *      0x0800C000             header semnat (BootHeader_t, 512 B)
 *      0x0800C200             aplicatie (vectori)
 *  Adresele de mai jos vin din simbolurile linker-ului, nu se scriu de mana.
 *
 *  Secure boot: aplicatia porneste doar daca header.magic e scris si
 *  semnatura ECDSA-P256 din header e valida pentru SHA-256(aplicatie).
 *  Magic-ul se scrie ultimul, dupa VERIFY (CRC + semnatura) reusit, deci o
 *  imagine intrerupta sau nesemnata ramane invalida.
 *
 *  Format cadru (host -> bootloader), little-endian:
 *    +------+-----+--------+------------------+---------+
 *    | SOF  | CMD | LEN    | PAYLOAD          | CRC32   |
 *    | 0xA5 | 1 B | 2 B    | LEN octeti       | 4 B     |
 *    +------+-----+--------+------------------+---------+
 *    - LEN este multiplu de 4 (0 .. BOOT_MAX_PAYLOAD)
 *    - CRC32 = CRC hardware STM32 (poly 0x04C11DB7, init 0xFFFFFFFF,
 *      fara reflexie, fara XOR final) calculat pe cuvinte de 32 biti
 *      little-endian peste SOF..PAYLOAD
 *
 *  Raspuns (bootloader -> host): 2 octeti [BOOT_ACK | BOOT_NACK][cod]
 *
 *  Comenzi:
 *    CONNECT 0x10  payload: -                        cod = BOOT_VERSION
 *    ERASE   0x20  payload: size(4)                  sterge sectoarele necesare
 *    WRITE   0x30  payload: offset(4) + date(N*4)    offset fata de BOOT_SLOT_ADDR
 *    VERIFY  0x40  payload: size(4) + crc32(4)       CRC peste slot + semnatura
 *    JUMP    0x50  payload: -                        porneste aplicatia
 *
 *  Imaginea trimisa de host = header + aplicatie (Tools/sb_sign.py).
 */

#ifndef INC_MODULE_BOOT_H_
#define INC_MODULE_BOOT_H_

#include "main.h"
#include "module/boot_crypto.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- Config */
#define BOOT_VERSION            0x02U           /* 0x02 = imagini semnate */

/* Simboluri din memory_map.ld: doar adresa lor conteaza, nu continutul */
extern const uint32_t __slot_start__[];
extern const uint32_t __slot_end__[];
extern const uint32_t __app_start__[];
extern const uint32_t __ram_start__[];
extern const uint32_t __ram_end__[];

#define BOOT_SLOT_ADDR          ((uint32_t)__slot_start__)  /* header, baza ERASE/WRITE */
#define BOOT_APP_ADDR           ((uint32_t)__app_start__)   /* vectorii aplicatiei      */
#define BOOT_FLASH_END          ((uint32_t)__slot_end__)    /* capatul flash-ului       */
#define BOOT_SLOT_MAX_SIZE      (BOOT_FLASH_END - BOOT_SLOT_ADDR)
#define BOOT_HEADER_SIZE        (BOOT_APP_ADDR - BOOT_SLOT_ADDR)

#define BOOT_HEADER_MAGIC       0x31484253UL    /* 'SBH1' in memorie */

/* Inceputul slotului; restul pana la BOOT_HEADER_SIZE e 0xFF */
typedef struct
{
    uint32_t magic;                     /* scris ultimul, dupa VERIFY reusit */
    uint32_t size;                      /* octetii aplicatiei semnati        */
    uint8_t  signature[BOOT_SIG_LEN];   /* ECDSA-P256 r||s peste SHA-256     */
} BootHeader_t;

#define BOOT_HEADER             ((const BootHeader_t *)BOOT_SLOT_ADDR)

#define BOOT_RAM_START          ((uint32_t)__ram_start__)
#define BOOT_RAM_END            ((uint32_t)__ram_end__)

#define BOOT_MAX_DATA           256U            /* octeti de date per WRITE */
#define BOOT_MAX_PAYLOAD        (4U + BOOT_MAX_DATA)

#define BOOT_ENTRY_TIMEOUT_MS   3000U           /* fereastra pentru CONNECT */
#define BOOT_FRAME_TIMEOUT_MS   500U            /* timeout intre octetii unui cadru */

/* -------------------------------------------------------------- Protocol */
#define BOOT_SOF                0xA5U			/* 0b01010101 - putin probabil sa apara random, bun de SOF */
#define BOOT_ACK                0x79U
#define BOOT_NACK               0x1FU

typedef enum
{
    BOOT_CMD_CONNECT = 0x10,
    BOOT_CMD_ERASE   = 0x20,
    BOOT_CMD_WRITE   = 0x30,
    BOOT_CMD_VERIFY  = 0x40,
    BOOT_CMD_JUMP    = 0x50
} BootCmd_t;

/* Codul trimis dupa ACK/NACK */
typedef enum
{
    BOOT_OK = 0x00,
    BOOT_ERR_TIMEOUT,       /* nu a venit niciun cadru (nu se raspunde) */
    BOOT_ERR_SOF,           /* octet != SOF, ignorat (nu se raspunde)   */
    BOOT_ERR_FRAME,         /* cadru incomplet (timeout in mijloc)      */
    BOOT_ERR_LEN,
    BOOT_ERR_CRC,
    BOOT_ERR_CMD,
    BOOT_ERR_ADDR,
    BOOT_ERR_NOT_ERASED,
    BOOT_ERR_FLASH,
    BOOT_ERR_VERIFY,
    BOOT_ERR_NO_APP,
    BOOT_ERR_SIGNATURE      /* header invalid sau semnatura respinsa   */
} BootStatus_t;

typedef struct
{
    uint8_t         cmd;
    uint16_t        len;
    const uint8_t  *payload;    /* pointer in bufferul intern de receptie */
} BootFrame_t;

/* ------------------------------------------------------------------- API */
void         Boot_Init(UART_HandleTypeDef *huart, CRC_HandleTypeDef *hcrc);

BootStatus_t Boot_ReceiveFrame(BootFrame_t *frame, uint32_t sofTimeoutMs);
void         Boot_SendResponse(BootStatus_t status, uint8_t info);

BootStatus_t Boot_EraseApp(uint32_t size);
BootStatus_t Boot_WriteApp(uint32_t offset, const uint8_t *data, uint32_t len);
BootStatus_t Boot_VerifyApp(uint32_t size, uint32_t expectedCrc);

bool         Boot_IsAppValid(void);
bool         Boot_IsEntryRequested(void);
void         Boot_JumpToApp(void);

#ifdef __cplusplus
}
#endif

#endif /* INC_MODULE_BOOT_H_ */
