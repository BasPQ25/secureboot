/*
 * boot.c
 *
 *  Created on: Oct 13, 2026
 *      Author: paulb
 *
 *  Functiile bootloader-ului: receptie cadre cu CRC32 hardware, stergere,
 *  scriere si verificare flash (CRC + semnatura ECDSA), salt in aplicatie.
 *  Protocolul si formatul slotului sunt descrise in boot.h.
 */

#include "module/boot.h"
#include <string.h>

/* Header (4) + payload + CRC (4), aliniat la 32 biti pentru CRC hardware */
#define BOOT_RX_WORDS   ((4U + BOOT_MAX_PAYLOAD + 4U) / 4U)

static UART_HandleTypeDef *s_huart;
static CRC_HandleTypeDef  *s_hcrc;

static uint32_t s_rxBuf[BOOT_RX_WORDS];

static uint32_t s_erasedSize;           /* cat din slot e sters             */
static uint32_t s_magic;                /* header.magic primit de la host   */
static bool     s_magicPending;         /* scris doar dupa VERIFY reusit    */

/* ------------------------------------------------------------- Helpers */

static uint32_t Boot_Read32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Goleste RX pana la 20 ms de liniste (resincronizare dupa eroare) */
static void Boot_FlushRx(void)
{
    uint8_t b;

    while (HAL_UART_Receive(s_huart, &b, 1U, 20U) == HAL_OK)
    {
    }
    __HAL_UART_CLEAR_OREFLAG(s_huart);
}

static uint32_t Boot_GetSector(uint32_t addr)
{
    if (addr < 0x08004000UL) return FLASH_SECTOR_0;
    if (addr < 0x08008000UL) return FLASH_SECTOR_1;
    if (addr < 0x0800C000UL) return FLASH_SECTOR_2;
    if (addr < 0x08010000UL) return FLASH_SECTOR_3;
    if (addr < 0x08020000UL) return FLASH_SECTOR_4;
    if (addr < 0x08040000UL) return FLASH_SECTOR_5;
    if (addr < 0x08060000UL) return FLASH_SECTOR_6;
    return FLASH_SECTOR_7;
}

static void Boot_ClearFlashFlags(void)
{
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
}

/* MSP in RAM, Reset_Handler (Thumb) in interiorul aplicatiei semnate */
static bool Boot_AreVectorsValid(uint32_t appSize)
{
    uint32_t sp    = *(volatile uint32_t *)BOOT_APP_ADDR;
    uint32_t reset = *(volatile uint32_t *)(BOOT_APP_ADDR + 4U);

    if ((sp <= BOOT_RAM_START) || (sp > BOOT_RAM_END) || ((sp & 0x3U) != 0U))
    {
        return false;
    }
    if (((reset & 0x1U) == 0U) || (reset < BOOT_APP_ADDR) || (reset >= (BOOT_APP_ADDR + appSize)))
    {
        return false;
    }
    return true;
}

/*
 * Imaginea e autentica daca: magic corect, dimensiune plauzibila, vectori
 * valizi si semnatura ECDSA-P256 valida peste aplicatie[0:size].
 * magic se primeste ca parametru: la VERIFY e inca in RAM (s_magic).
 */
static bool Boot_IsImageAuthentic(uint32_t magic, uint32_t maxAppSize)
{
    const BootHeader_t *hdr     = BOOT_HEADER;
    uint32_t            appSize = hdr->size;

    if (magic != BOOT_HEADER_MAGIC)
    {
        return false;
    }
    if ((appSize < 8U) || ((appSize % 4U) != 0U) || (appSize > maxAppSize))
    {
        return false;
    }
    if (!Boot_AreVectorsValid(appSize))
    {
        return false;
    }
    return BootCrypto_VerifyImage((const uint8_t *)BOOT_APP_ADDR, appSize, hdr->signature);
}

/* ----------------------------------------------------------------- API */

void Boot_Init(UART_HandleTypeDef *huart, CRC_HandleTypeDef *hcrc)
{
    s_huart            = huart;
    s_hcrc             = hcrc;
    s_erasedSize       = 0U;
    s_magic            = 0xFFFFFFFFUL;
    s_magicPending     = false;
}

BootStatus_t Boot_ReceiveFrame(BootFrame_t *frame, uint32_t sofTimeoutMs)
{
    uint8_t  *buf = (uint8_t *)s_rxBuf;
    uint16_t  len;
    uint32_t  rxCrc;
    uint32_t  calcCrc;

    /* SOF */
    if (HAL_UART_Receive(s_huart, &buf[0], 1U, sofTimeoutMs) != HAL_OK)
    {
        return BOOT_ERR_TIMEOUT;
    }
    if (buf[0] != BOOT_SOF)
    {
        return BOOT_ERR_SOF;
    }

    /* CMD + LEN */
    if (HAL_UART_Receive(s_huart, &buf[1], 3U, BOOT_FRAME_TIMEOUT_MS) != HAL_OK)
    {
        return BOOT_ERR_FRAME;
    }
    len = (uint16_t)buf[2] | ((uint16_t)buf[3] << 8);
    if ((len > BOOT_MAX_PAYLOAD) || ((len % 4U) != 0U))
    {
        Boot_FlushRx();
        return BOOT_ERR_LEN;
    }

    /* PAYLOAD + CRC */
    if (HAL_UART_Receive(s_huart, &buf[4], (uint16_t)(len + 4U), BOOT_FRAME_TIMEOUT_MS) != HAL_OK)
    {
        return BOOT_ERR_FRAME;
    }

    rxCrc   = Boot_Read32(&buf[4U + len]);
    calcCrc = HAL_CRC_Calculate(s_hcrc, s_rxBuf, (4U + len) / 4U);
    if (rxCrc != calcCrc)
    {
        return BOOT_ERR_CRC;
    }

    frame->cmd     = buf[1];
    frame->len     = len;
    frame->payload = &buf[4];
    return BOOT_OK;
}

void Boot_SendResponse(BootStatus_t status, uint8_t info)
{
    uint8_t tx[2];

    tx[0] = (status == BOOT_OK) ? BOOT_ACK : BOOT_NACK;
    tx[1] = (status == BOOT_OK) ? info : (uint8_t)status;
    (void)HAL_UART_Transmit(s_huart, tx, sizeof(tx), 100U);
}

BootStatus_t Boot_EraseApp(uint32_t size)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t               sectorError = 0U;
    uint32_t               firstSector;
    uint32_t               lastSector;
    HAL_StatusTypeDef      st;

    if ((size == 0U) || (size > BOOT_SLOT_MAX_SIZE) || ((size % 4U) != 0U))
    {
        return BOOT_ERR_ADDR;
    }

    /* memory_map.ld garanteaza ca BOOT_SLOT_ADDR e pe granita de sector */
    firstSector = Boot_GetSector(BOOT_SLOT_ADDR);
    lastSector  = Boot_GetSector(BOOT_SLOT_ADDR + size - 1U);

    erase.TypeErase    = FLASH_TYPEERASE_SECTORS;
    erase.Banks        = FLASH_BANK_1;
    erase.Sector       = firstSector;
    erase.NbSectors    = lastSector - firstSector + 1U;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;     /* 2.7 - 3.6 V, Nucleo = 3.3 V */

    s_erasedSize       = 0U;
    s_magicPending     = false;

    HAL_FLASH_Unlock();
    Boot_ClearFlashFlags();
    st = HAL_FLASHEx_Erase(&erase, &sectorError);
    HAL_FLASH_Lock();

    if ((st != HAL_OK) || (sectorError != 0xFFFFFFFFUL))
    {
        return BOOT_ERR_FLASH;
    }

    s_erasedSize = size;
    return BOOT_OK;
}

BootStatus_t Boot_WriteApp(uint32_t offset, const uint8_t *data, uint32_t len)
{
    BootStatus_t ret = BOOT_OK;
    uint32_t     addr;
    uint32_t     word;
    uint32_t     i;

    if (((offset % 4U) != 0U) || ((len % 4U) != 0U) ||
        (len == 0U) || (len > BOOT_MAX_DATA))
    {
        return BOOT_ERR_LEN;
    }
    if ((offset >= s_erasedSize) || (len > (s_erasedSize - offset)))
    {
        return BOOT_ERR_NOT_ERASED;
    }

    HAL_FLASH_Unlock();
    Boot_ClearFlashFlags();

    for (i = 0U; i < len; i += 4U)
    {
        addr = BOOT_SLOT_ADDR + offset + i;
        memcpy(&word, &data[i], 4U);

        /* header.magic se scrie ultimul, dupa VERIFY: o imagine intrerupta
           sau cu semnatura gresita ramane invalida pentru Boot_IsAppValid() */
        if (addr == BOOT_SLOT_ADDR)
        {
            s_magic        = word;
            s_magicPending = true;
            continue;
        }

        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, word) != HAL_OK)
        {
            ret = BOOT_ERR_FLASH;
            break;
        }
        if (*(volatile uint32_t *)addr != word)
        {
            ret = BOOT_ERR_VERIFY;
            break;
        }
    }

    HAL_FLASH_Lock();
    return ret;
}

BootStatus_t Boot_VerifyApp(uint32_t size, uint32_t expectedCrc)
{
    BootStatus_t ret = BOOT_OK;
    uint32_t     crc;

    if ((size <= BOOT_HEADER_SIZE) || ((size % 4U) != 0U) || (size > s_erasedSize))
    {
        return BOOT_ERR_ADDR;
    }
    if (!s_magicPending)
    {
        return BOOT_ERR_NO_APP;
    }

    /* 1. Integritatea transferului: magic din RAM, restul direct din flash */
    crc = HAL_CRC_Calculate(s_hcrc, &s_magic, 1U);
    crc = HAL_CRC_Accumulate(s_hcrc, (uint32_t *)(BOOT_SLOT_ADDR + 4U), (size - 4U) / 4U);
    if (crc != expectedCrc)
    {
        return BOOT_ERR_VERIFY;
    }

    /* 2. Autenticitatea: semnatura ECDSA peste aplicatia deja scrisa */
    if (!Boot_IsImageAuthentic(s_magic, size - BOOT_HEADER_SIZE))
    {
        return BOOT_ERR_SIGNATURE;
    }

    /* 3. Abia acum imaginea devine pornibila */
    HAL_FLASH_Unlock();
    Boot_ClearFlashFlags();
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, BOOT_SLOT_ADDR, s_magic) != HAL_OK)
    {
        ret = BOOT_ERR_FLASH;
    }
    else if (*(volatile uint32_t *)BOOT_SLOT_ADDR != s_magic)
    {
        ret = BOOT_ERR_VERIFY;
    }
    else
    {
        s_magicPending = false;
    }
    HAL_FLASH_Lock();

    return ret;
}

/* Semnatura se reverifica la fiecare boot: o aplicatie rescrisa prin SWD,
   ocolind bootloader-ul, nu porneste */
bool Boot_IsAppValid(void)
{
    return Boot_IsImageAuthentic(BOOT_HEADER->magic, BOOT_SLOT_MAX_SIZE - BOOT_HEADER_SIZE);
}

/* Butonul albastru B1 (PC13, activ pe 0) tinut apasat la reset */
bool Boot_IsEntryRequested(void)
{
    return HAL_GPIO_ReadPin(B1_GPIO_Port, B1_Pin) == GPIO_PIN_RESET;
}

void Boot_JumpToApp(void)
{
    uint32_t i;

    HAL_UART_DeInit(s_huart);
    HAL_CRC_DeInit(s_hcrc);
    HAL_RCC_DeInit();
    HAL_DeInit();

    __disable_irq();

    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL  = 0U;

    for (i = 0U; i < (sizeof(NVIC->ICER) / sizeof(NVIC->ICER[0])); i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFFUL;
        NVIC->ICPR[i] = 0xFFFFFFFFUL;
    }

    SCB->VTOR = BOOT_APP_ADDR;
    __DSB();
    __ISB();

    __set_MSP(*(volatile uint32_t *)BOOT_APP_ADDR);
    __enable_irq();

    /* Adresa citita direct din flash: stiva veche nu mai e valida */
    ((void (*)(void))(*(volatile uint32_t *)(BOOT_APP_ADDR + 4U)))(); //jump to reset handler

    while (1)
    {
    }
}
