/*
 * print.cpp
 *
 *  Created on: Oct 10, 2026
 *      Author: paulb
 *
 *  Redirectare printf() pe NUCLEO-F446RE.
 *
 *  Doua variante (alegi una cu PRINT_USE_ITM mai jos):
 *
 *  1) UART (implicit) - USART2 (PA2/PA3) -> ST-LINK Virtual COM Port.
 *     In STM32CubeIDE: Window > Show View > Console -> Open Console (iconita)
 *     -> Command Shell Console -> Serial Port -> COMx, 115200, 8N1.
 *     (sau PuTTY / TeraTerm pe acelasi COM)
 *
 *  2) ITM / SWV (PRINT_USE_ITM = 1) - pin SWO (PB3), fara UART.
 *     In Debug Configuration > Debugger: Serial Wire Viewer (SWV) -> Enable,
 *     Core Clock = 84 MHz. In debug: Window > Show View > SWV > SWV ITM Data
 *     Console -> Configure trace: bifeaza port 0 -> Start Trace.
 *
 *  Nu trebuie modificat nimic in main: fisierul suprascrie _write() (care e
 *  __weak in syscalls.c) si opreste buffering-ul pe stdout inainte de main(),
 *  deci printf("abc") apare imediat, chiar si fara '\n'.
 *
 *  Pentru printf cu float ("%f"): Project > Properties > C/C++ Build >
 *  Settings > MCU Settings -> bifeaza "Use float with printf from newlib-nano"
 *  (echivalent cu flag-ul de linker -u _printf_float).
 */

#include "main.h"
#include <stdio.h>

/* 0 = UART2 (Virtual COM Port), 1 = ITM/SWV (SWO) */
#ifndef PRINT_USE_ITM
#define PRINT_USE_ITM 0
#endif

#if !PRINT_USE_ITM
extern UART_HandleTypeDef huart2;
#endif

extern "C" int _write(int file, char *ptr, int len)
{
	(void)file;

	if (ptr == nullptr || len <= 0)
	{
		return 0;
	}

#if PRINT_USE_ITM
	for (int i = 0; i < len; i++)
	{
		ITM_SendChar(static_cast<uint32_t>(ptr[i]));
	}
#else
	/* Daca printf e apelat inainte de MX_USART2_UART_Init(), ignoram textul */
	if (huart2.gState == HAL_UART_STATE_RESET)
	{
		return len;
	}

	HAL_UART_Transmit(&huart2, reinterpret_cast<uint8_t*>(ptr),
					  static_cast<uint16_t>(len), HAL_MAX_DELAY);
#endif

	return len;
}

/* Ruleaza automat inainte de main() (constructor global C++) */
namespace
{
	struct PrintInit
	{
		PrintInit()
		{
			setvbuf(stdout, nullptr, _IONBF, 0);
		}
	};

	PrintInit printInit;
}
