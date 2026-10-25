
#include "module/flash_hal.h"
#include <iterator>


FlashAPI::FlashAPI()
{

}

void FlashAPI::flushDataCache()
{
	if (READ_BIT(FLASH->ACR, FLASH_ACR_DCEN) != 0u)
	{
		__HAL_FLASH_DATA_CACHE_DISABLE();
		__HAL_FLASH_DATA_CACHE_RESET();
		__HAL_FLASH_DATA_CACHE_ENABLE();
	}
}

void FlashAPI::clearFlashErrorFlags()
{
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
}

FlashAPI::Flags FlashAPI::eraseSector(uint8_t sector)
{
	FlashAPI::Flags status = Flags::FUNCTION_OK;

	//erase parametrization
	FLASH_EraseInitTypeDef erase = {
			.TypeErase = FLASH_TYPEERASE_SECTORS,
			.Sector = sector,
			.NbSectors = 1,
			.VoltageRange = FLASH_VOLTAGE_RANGE_3
	};

	uint32_t sectorError;
	HAL_FLASHEx_Erase(&erase, &sectorError);

	if( sectorError != 0xFFFFFFFFU)
		status = Flags::ERASE_ERROR;

	return status;
}

const FlashAPI::FlashSector* FlashAPI::checkSector(uint32_t addr)
{
	// doar sectorul de date din linker: S0-S1 = bootloader, S3+ = aplicatia
	const uint32_t dataStart = reinterpret_cast<uint32_t>(&__appdata_start__);
	const uint32_t dataEnd   = reinterpret_cast<uint32_t>(&__appdata_end__);

	if (addr < dataStart || addr >= dataEnd)
		return nullptr;

	for (const auto& s : FlashAPI::SECTORS)
	    if (s.contains(addr)) return &s;

	return nullptr;
}


