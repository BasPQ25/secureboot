#include"module/flash_hal.h"
#include"main.h"

uint8_t buffer[5] = { 0x44, 0x68, 0x92, 0xF3, 0x55};

void App()
{
  HAL_Delay(300);
  HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);
}
