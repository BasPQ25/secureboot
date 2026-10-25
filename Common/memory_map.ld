/*
******************************************************************************
**
** @file        : memory_map.ld
**
**  Harta de memorie comuna pentru STM32F446RE (512 KB flash, 128 KB RAM).
**  SINGURA sursa pentru adrese: inclusa de linker scriptul fiecarui proiect.
**
**    Bootloader_F4/STM32F446RETX_FLASH.ld : REGION_ALIAS("FLASH", BOOT_FLASH)
**    Safety/STM32F446RETX_FLASH.ld        : REGION_ALIAS("FLASH", APP_FLASH)
**
**  Sectoarele F446 au dimensiuni fixe (16/16/16/16/64/128/128/128 KB);
**  fiecare regiune trebuie sa inceapa pe o granita de sector, altfel
**  stergerea unei zone afecteaza si vecinii (verificat cu ASSERT mai jos).
**
**    S0-S1 0x08000000  32 KB   bootloader (cu CMOX: SHA-256 + ECDSA-P256)
**    S2    0x08008000  16 KB   date aplicatie (FlashAPI)
**    S3-S7 0x0800C000 464 KB   slot aplicatie:
**            0x0800C000  512 B   header semnat (magic, size, semnatura)
**            0x0800C200          aplicatie (vectori, BOOT_APP_ADDR)
**
**  Headerul are 512 B pentru ca VTOR cere tabela de vectori aliniata la
**  puterea lui 2 >= dimensiunea ei (F446: 113 vectori -> 512 B).
**
**  Inclus din folderul de build (<proiect>/Debug) cu cale relativa,
**  deci functioneaza pe orice PC cat timp structura secureboot/ se pastreaza.
**
******************************************************************************
*/

MEMORY
{
  RAM           (xrw)   : ORIGIN = 0x20000000,  LENGTH = 128K
  BOOT_FLASH    (rx)    : ORIGIN = 0x08000000,  LENGTH = 32K           /* S0-S1  */
  DATA_FLASH    (rx)    : ORIGIN = 0x08008000,  LENGTH = 16K           /* S2     */
  APP_HEADER    (rx)    : ORIGIN = 0x0800C000,  LENGTH = 512           /* S3 ... */
  APP_FLASH     (rx)    : ORIGIN = 0x0800C200,  LENGTH = 464K - 512    /* ... S7 */
}

/* Simboluri pentru cod (extern const uint32_t __x__[]) */
__ram_start__             = ORIGIN(RAM);
__ram_end__               = ORIGIN(RAM) + LENGTH(RAM);

__boot_start__            = ORIGIN(BOOT_FLASH);
__boot_end__              = ORIGIN(BOOT_FLASH) + LENGTH(BOOT_FLASH);

__appdata_start__         = ORIGIN(DATA_FLASH);
__appdata_end__           = ORIGIN(DATA_FLASH) + LENGTH(DATA_FLASH);

/* Slotul = header + aplicatie: zona pe care bootloader-ul o sterge si o scrie */
__slot_start__            = ORIGIN(APP_HEADER);
__slot_end__              = ORIGIN(APP_FLASH) + LENGTH(APP_FLASH);

/* Aplicatia propriu-zisa: aici sunt vectorii, aici sare bootloader-ul */
__app_start__             = ORIGIN(APP_FLASH);
__app_end__               = ORIGIN(APP_FLASH) + LENGTH(APP_FLASH);

/* Regiunile trebuie sa fie contigue si pe granite de sector */
ASSERT(__boot_end__      == __appdata_start__, "memory_map.ld: BOOT_FLASH nu se termina la DATA_FLASH")
ASSERT(__appdata_end__   == __slot_start__, "memory_map.ld: DATA_FLASH nu se termina la APP_HEADER")
ASSERT(ORIGIN(APP_HEADER) + LENGTH(APP_HEADER) == __app_start__, "memory_map.ld: APP_HEADER nu se termina la APP_FLASH")
ASSERT(__slot_end__      == 0x08080000, "memory_map.ld: APP_FLASH trebuie sa se termine la capatul flash-ului")
ASSERT(__appdata_start__ == 0x08008000, "memory_map.ld: DATA_FLASH trebuie sa fie exact sectorul 2")
ASSERT(__slot_start__ == 0x08008000 || __slot_start__ == 0x0800C000 ||
       __slot_start__ == 0x08010000 || __slot_start__ == 0x08020000 ||
       __slot_start__ == 0x08040000 || __slot_start__ == 0x08060000,
       "memory_map.ld: APP_HEADER nu incepe pe o granita de sector")
ASSERT((__app_start__ & 0x1FF) == 0, "memory_map.ld: vectorii aplicatiei trebuie aliniati la 512 B (VTOR)")
