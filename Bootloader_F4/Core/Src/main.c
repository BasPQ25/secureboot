/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "module/boot.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum
{
  BL_STATE_INIT = 0,      /* decide: ramane in bootloader sau sare in aplicatie */
  BL_STATE_WAIT_CONNECT,  /* fereastra de BOOT_ENTRY_TIMEOUT_MS pentru CONNECT  */
  BL_STATE_IDLE,          /* conectat, asteapta cadre                           */
  BL_STATE_PROCESS,       /* executa comanda primita                            */
  BL_STATE_JUMP           /* porneste aplicatia                                 */
} BlState_t;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
CRC_HandleTypeDef hcrc;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
static BlState_t   blState = BL_STATE_INIT;
static BootFrame_t blFrame;
static uint32_t    blEntryTick;
static bool        blStayInBoot;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_CRC_Init(void);
/* USER CODE BEGIN PFP */
static void Bootloader_Process(void);
static void Bootloader_ExecuteCommand(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART2_UART_Init();
  MX_CRC_Init();
  /* USER CODE BEGIN 2 */
  Boot_Init(&huart2, &hcrc);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    Bootloader_Process();
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief CRC Initialization Function
  * @param None
  * @retval None
  */
static void MX_CRC_Init(void)
{

  /* USER CODE BEGIN CRC_Init 0 */

  /* USER CODE END CRC_Init 0 */

  /* USER CODE BEGIN CRC_Init 1 */

  /* USER CODE END CRC_Init 1 */
  hcrc.Instance = CRC;
  if (HAL_CRC_Init(&hcrc) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CRC_Init 2 */

  /* USER CODE END CRC_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_9B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_EVEN;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : LD2_Pin */
  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
static uint32_t Bootloader_Read32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/**
  * @brief  State machine-ul bootloader-ului, apelat din bucla principala.
  */
static void Bootloader_Process(void)
{
  BootStatus_t st;

  switch (blState)
  {
    case BL_STATE_INIT:
      /* Ramane in bootloader daca B1 e apasat sau nu exista aplicatie valida */
      blStayInBoot = Boot_IsEntryRequested() || !Boot_IsAppValid();
      blEntryTick  = HAL_GetTick();
      HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);
      blState = BL_STATE_WAIT_CONNECT;
      break;

    case BL_STATE_WAIT_CONNECT:
      st = Boot_ReceiveFrame(&blFrame, 10U);
      if (st == BOOT_OK)
      {
        if (blFrame.cmd == BOOT_CMD_CONNECT)
        {
          Boot_SendResponse(BOOT_OK, BOOT_VERSION);
          blState = BL_STATE_IDLE;
          break;
        }
        Boot_SendResponse(BOOT_ERR_CMD, 0U);  /* intai CONNECT */
      }
      else if ((st != BOOT_ERR_TIMEOUT) && (st != BOOT_ERR_SOF))
      {
        Boot_SendResponse(st, 0U);
      }

      if (!blStayInBoot && ((HAL_GetTick() - blEntryTick) >= BOOT_ENTRY_TIMEOUT_MS))
      {
        blState = BL_STATE_JUMP;
      }
      break;

    case BL_STATE_IDLE:
      st = Boot_ReceiveFrame(&blFrame, 100U);
      if (st == BOOT_OK)
      {
        blState = BL_STATE_PROCESS;
      }
      else if ((st == BOOT_ERR_TIMEOUT) || (st == BOOT_ERR_SOF))
      {
        HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);  /* LED clipeste = bootloader activ */
      }
      else
      {
        Boot_SendResponse(st, 0U);  /* CRC / lungime / cadru incomplet -> host retrimite */
      }
      break;

    case BL_STATE_PROCESS:
      Bootloader_ExecuteCommand();
      break;

    case BL_STATE_JUMP:
    default:
      if (Boot_IsAppValid())
      {
        Boot_JumpToApp();  /* nu se mai intoarce */
      }
      blStayInBoot = true;
      blState = BL_STATE_WAIT_CONNECT;
      break;
  }
}

/**
  * @brief  Executa comanda din blFrame si trimite ACK/NACK.
  */
static void Bootloader_ExecuteCommand(void)
{
  const uint8_t *p   = blFrame.payload;
  BootStatus_t   st  = BOOT_OK;
  uint8_t        info = 0U;

  blState = BL_STATE_IDLE;

  switch (blFrame.cmd)
  {
    case BOOT_CMD_CONNECT:
      info = BOOT_VERSION;
      break;

    case BOOT_CMD_ERASE:
      st = (blFrame.len == 4U) ? Boot_EraseApp(Bootloader_Read32(&p[0])) : BOOT_ERR_LEN;
      break;

    case BOOT_CMD_WRITE:
      st = (blFrame.len > 4U)
           ? Boot_WriteApp(Bootloader_Read32(&p[0]), &p[4], blFrame.len - 4U)
           : BOOT_ERR_LEN;
      break;

    case BOOT_CMD_VERIFY:
      st = (blFrame.len == 8U)
           ? Boot_VerifyApp(Bootloader_Read32(&p[0]), Bootloader_Read32(&p[4]))
           : BOOT_ERR_LEN;
      break;

    case BOOT_CMD_JUMP:
      if (Boot_IsAppValid())
      {
        blState = BL_STATE_JUMP;
      }
      else
      {
        st = BOOT_ERR_NO_APP;
      }
      break;

    default:
      st = BOOT_ERR_CMD;
      break;
  }

  Boot_SendResponse(st, info);
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
