/*
 * flash_hal.h
 *
 *  Created on: Oct 6, 2026
 *      Author: paulb
 */
#ifndef INC_FLASH_HAL_H
#define INC_FLASH_HAL_H

#include"main.h"
#include<string.h>


// zona de date a aplicatiei (S2), din Common/memory_map.ld
// S0-S1 = bootloader, S3+ = header semnat + aplicatia
extern const uint32_t __appdata_start__;
extern const uint32_t __appdata_end__;

class FlashAPI{
public:
	FlashAPI();

	struct FlashSector
	{
	    uint8_t  number;   // numărul sectorului, ca FLASH_SECTOR_x din HAL
	    uint32_t start;    // adresa de început
	    uint32_t size;     // mărimea în octeți

	    constexpr uint32_t end() const { return start + size; }   // prima adresă DUPĂ sector

	    constexpr bool contains(uint32_t addr) const
	    {
	        return addr >= start && addr < end();
	    }

	};

	#if defined(STM32F446xx)
	static constexpr FlashSector SECTORS[] = {
	    { 0U, 0x08000000U,  16U * 1024U },
	    { 1U, 0x08004000U,  16U * 1024U },
	    { 2U, 0x08008000U,  16U * 1024U },
	    { 3U, 0x0800C000U,  16U * 1024U },
	    { 4U, 0x08010000U,  64U * 1024U },
	    { 5U, 0x08020000U, 128U * 1024U },
	    { 6U, 0x08040000U, 128U * 1024U },
	    { 7U, 0x08060000U, 128U * 1024U },
	};
	#endif

	enum class Flags
	{
		WRITE_OUT_OF_BOUNDARIES,
		WRITE_IN_SECTOR_0_NOT_ALLOWED,
		WRITE_DATA_OUT_OF_SECTOR,
		WRITE_BOUNDARIES_UNDEFINED,
		WRITE_ALIGN_ERROR,
		WRITE_TIMEOUT_ERROR,

		ERASE_ERROR,

		UNLOCK_FAIL,
		LOCK_FAIL,

		FUNCTION_OK,

		ERESE_NEEDED,
		ERASE_NOT_NEEDED,

	};

	template<typename R>
	static void read(uint32_t StartAdress, R* data, uint32_t lenInDataType)
	{
		const R* flash = reinterpret_cast<R*>(StartAdress);

		for(uint32_t i = 0; i < lenInDataType;i++)
		{
			data[i] = flash[i];
		}
	}

	template<typename C>
	static Flags allOne(C* buf)
	{
		uint8_t* p_buf = reinterpret_cast<uint8_t*>(buf);

		for(uint32_t i = 0; i < sizeof(C); i++)
		{
			if(p_buf[i] != 0xFF)
				return Flags::ERESE_NEEDED;
		}

		return Flags::ERASE_NOT_NEEDED;
	}

	template<typename W>
	static Flags write(uint32_t StartAdress, const W* data, uint32_t lenInDataType)
	{


		const FlashSector* sector = nullptr;
		Flags status = Flags::FUNCTION_OK;
		volatile W* flash = reinterpret_cast<W*>(StartAdress);

		static_assert(sizeof(W) == 1 || sizeof(W) == 2 || sizeof(W) == 4,
					"Write suporta doar aceste marimi pentru tensiunea de alimentare fara Vpp");

		//Check Adress for ALIGNMENT
		if (StartAdress % sizeof(W) != 0)
			return Flags::WRITE_ALIGN_ERROR;

		//check where the adress is
		if((sector = checkSector(StartAdress)) == nullptr)
			return Flags::WRITE_OUT_OF_BOUNDARIES;

		if( lenInDataType > (sector->end() - StartAdress) / sizeof(W) )
			return Flags::WRITE_DATA_OUT_OF_SECTOR;

		//Unlock flash
		if(HAL_FLASH_Unlock() != HAL_OK)
			return Flags::UNLOCK_FAIL;

		//clear previous errors
		clearFlashErrorFlags();

		//check if erase is needed
		W buffer[1] = {0};
		for(uint32_t i = 0 ; i < lenInDataType; i++)
		{
			read(StartAdress + (i*sizeof(W)), buffer, 1);
			if( allOne<W>(buffer) == Flags::ERESE_NEEDED)
			{

				eraseSector(sector->number);//wait for erase to finish
				if( FLASH_WaitForLastOperation((uint32_t)5000) != HAL_OK)
				{
					HAL_FLASH_Lock();
					return Flags::WRITE_TIMEOUT_ERROR;
				}
				break;
			}

		}

		CLEAR_BIT(FLASH->CR, FLASH_CR_PSIZE); //paralelism bits reset

		switch(sizeof(W))
		{
		case 1:
			FLASH->CR |= FLASH_PSIZE_BYTE;
			break;
		case 2:
			FLASH->CR |= FLASH_PSIZE_HALF_WORD;
			break;
		case 4:
			FLASH->CR |= FLASH_PSIZE_WORD;
			break;
		}

		//set Programming Operation
		FLASH->CR |= FLASH_CR_PG;

		for(uint32_t i = 0; i < lenInDataType;i++)
		{

			flash[i] = data[i];

			if( FLASH_WaitForLastOperation((uint32_t)5000) != HAL_OK)
			{
				status = Flags::WRITE_TIMEOUT_ERROR;
				break;
			}
		}

		//flush cache data after rewrite
		flushDataCache();

		//reset Programming Operation
		FLASH->CR &= (~FLASH_CR_PG);

		if (HAL_FLASH_Lock() != HAL_OK)
			return Flags::LOCK_FAIL;

		return status;
	}

private:

	static void flushDataCache();
	static void clearFlashErrorFlags();
	static Flags eraseSector(uint8_t sector);
	static const FlashSector* checkSector(uint32_t addr);
};
#endif /*INC_FLASH_HAL_H */
