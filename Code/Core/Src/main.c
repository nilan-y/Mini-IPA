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
#include "usb_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "usb_commands.h"
#include "sensor_app.h"
#include "power_telemetry.h"
#include "display_app.h"
#include "usbd_cdc_if.h"
#include "oled.h"
#include "custom_bus.h"
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
// (1/(1/fsys)*ADCcycles)/2 =>
#define ADC_COUNTER_MAX 0x18870
#define ADC_SHUNT_APM 	150.0f
#define ADC_PD_AMP 		4606.0f
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
extern I2C_HandleTypeDef hi2c1;
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc;
DMA_HandleTypeDef hdma_adc;

TIM_HandleTypeDef htim1;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_ADC_Init(void);
static void MX_TIM1_Init(void);
/* USER CODE BEGIN PFP */
static void ExecuteUsbCommand(uint8_t command);
static void ProcessPowerMeasurements(void);
<<<<<<< HEAD

//static void ConvertInttoStr(char *buffer,char *praefix, char *suffix, int var, uint8_t significants);

=======
>>>>>>> f8ba83b9c3d365d8eb56c1029c2ce56d6f244a0f
static void ProcessButton(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
uint16_t photodiode;
uint16_t shunt;
float voltage_photodiode = 0;
float voltage_shunt = 0;
float current_shunt = 0;
<<<<<<< HEAD
const char  oled_buffer[] = {0};
=======
>>>>>>> f8ba83b9c3d365d8eb56c1029c2ce56d6f244a0f
uint16_t power_photodiode = 0;
uint16_t power_shunt = 0;
uint32_t adc_counter = 0;
static uint16_t adc_values[2];
static volatile uint8_t adc_ready = 0U;
static uint8_t power_measurement_enabled;
static uint8_t adc_running;
static uint8_t power_single_requested;
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *adc)
 {
     if (adc->Instance == ADC1)
     {
         adc_ready = 1U;
     }
 }

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
  MX_DMA_Init();
  MX_ADC_Init();
  MX_TIM1_Init();
  MX_USB_DEVICE_Init();
  /* USER CODE BEGIN 2 */
  // Timer-Takt: 48 MHz / (47 + 1) = 1 MHz
  __HAL_TIM_SET_PRESCALER(&htim1, 47U);
  // PWM-Frequenz: 1 MHz / (999 + 1) = 1 kHz
  __HAL_TIM_SET_AUTORELOAD(&htim1, 999U);
  // Vergleichswert für 50 % Tastverhältnis
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, 200U);


  HAL_GPIO_WritePin(LED_B1_GPIO_Port, LED_B1_Pin, GPIO_PIN_RESET);
  /* I2C1 is shared by the OLED (LL) and the VL53L4CD (HAL/BSP). The BSP init is
     reference counted, so calling it here once keeps the bus alive even if the
     TOF driver later calls BSP_I2C1_DeInit() on an error path. */

  if (BSP_I2C1_Init() != BSP_ERROR_NONE)
  {
    Error_Handler();
  }

  DisplayApp_Init((OLED_Init() == 0) ? 1U : 0U);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
		uint8_t command;
		CDC_ProcessActivity_FS();
		ProcessButton();
		if(USB_Commands_Read(&command) != 0U)
		{
			ExecuteUsbCommand(command);
		}
		ProcessPowerMeasurements();
		SensorApp_Process();
		DisplayApp_Process();
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
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_HSI14
                              |RCC_OSCILLATORTYPE_HSI48;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.HSI14State = RCC_HSI14_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.HSI14CalibrationValue = 16;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI48;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB|RCC_PERIPHCLK_I2C1;
  PeriphClkInit.I2c1ClockSelection = RCC_I2C1CLKSOURCE_HSI;
  PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;

  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC_Init(void)
{

  /* USER CODE BEGIN ADC_Init 0 */

  /* USER CODE END ADC_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC_Init 1 */

  /* USER CODE END ADC_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */
  hadc.Instance = ADC1;
  hadc.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV1;
  hadc.Init.Resolution = ADC_RESOLUTION_12B;
  hadc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc.Init.ScanConvMode = ADC_SCAN_DIRECTION_FORWARD;
  hadc.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc.Init.LowPowerAutoWait = DISABLE;
  hadc.Init.LowPowerAutoPowerOff = DISABLE;
  hadc.Init.ContinuousConvMode = DISABLE;
  hadc.Init.DiscontinuousConvMode = DISABLE;
  hadc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc.Init.DMAContinuousRequests = DISABLE;
  hadc.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  if (HAL_ADC_Init(&hadc) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel to be converted.
  */
  sConfig.Channel = ADC_CHANNEL_2;
  sConfig.Rank = ADC_RANK_CHANNEL_NUMBER;
  sConfig.SamplingTime = ADC_SAMPLETIME_1CYCLE_5;
  if (HAL_ADC_ConfigChannel(&hadc, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel to be converted.
  */
  sConfig.Channel = ADC_CHANNEL_3;
  if (HAL_ADC_ConfigChannel(&hadc, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC_Init 2 */

  /* USER CODE END ADC_Init 2 */

}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 65;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 65535;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */
  HAL_TIM_MspPostInit(&htim1);

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);

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
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(NSTBY_OSC_GPIO_Port, NSTBY_OSC_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, LED_USB_Pin|LED_B1_Pin|LED_B2_Pin|LED_B3_Pin
                          |SHUT_TOF_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);

  /*Configure GPIO pin : NSTBY_OSC_Pin */
  GPIO_InitStruct.Pin = NSTBY_OSC_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(NSTBY_OSC_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : BTN_Pin */
  GPIO_InitStruct.Pin = BTN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(BTN_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : LED_USB_Pin LED_B1_Pin LED_B2_Pin LED_B3_Pin
                           SHUT_TOF_Pin */
  GPIO_InitStruct.Pin = LED_USB_Pin|LED_B1_Pin|LED_B2_Pin|LED_B3_Pin
                          |SHUT_TOF_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : PB0 */
  GPIO_InitStruct.Pin = GPIO_PIN_0;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : INT_TOF_Pin */
  GPIO_InitStruct.Pin = INT_TOF_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(INT_TOF_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
/* BTN has an external pull-up: one falling edge after 30 ms of stable input. */
static void ProcessButton(void)
{
    static GPIO_PinState last_raw = GPIO_PIN_SET;
    static GPIO_PinState stable = GPIO_PIN_SET;
    static uint32_t changed_at;
    GPIO_PinState raw = HAL_GPIO_ReadPin(BTN_GPIO_Port, BTN_Pin);
    uint32_t now = HAL_GetTick();

    if (raw != last_raw)
    {
        last_raw = raw;
        changed_at = now;
    }
    if ((raw != stable) && ((uint32_t)(now - changed_at) >= 30U))
    {
        stable = raw;
        if (stable == GPIO_PIN_RESET)
        {
            ExecuteUsbCommand(3U);
        }
    }
}

/* Start a fresh two-channel ADC scan every 500 ms while command 1 is active. */
static void ProcessPowerMeasurements(void)
{
    if ((adc_running != 0U) && (adc_ready != 0U))
    {
        /* ADC continuous conversion is disabled: the complete pair is stable. */
        photodiode = adc_values[0];
        shunt = adc_values[1];
        adc_ready = 0U;
        if (HAL_ADC_Stop_DMA(&hadc) == HAL_OK)
        {
            adc_running = 0U;

            /* Keep the existing formulas/scaling; convert after the calculation. */
            voltage_shunt = (float)5.0f-shunt/1241.2f/ADC_SHUNT_APM;
            current_shunt = (float)(shunt/1241.2f/ADC_SHUNT_APM)/0.2f;
            power_photodiode = (uint16_t)((photodiode/1241.2f/ADC_PD_AMP)*(10.77f)*1000.0f);
            power_shunt = (uint16_t)(voltage_shunt * current_shunt * 1000.0f);

            PowerTelemetry_Update(power_photodiode, power_shunt);
<<<<<<< HEAD
			snprintf(oled_buffer, sizeof(oled_buffer),"Pshunt = %u mW", (unsigned) power_shunt);
            OLED_Print(1, 2,oled_buffer);
=======
>>>>>>> f8ba83b9c3d365d8eb56c1029c2ce56d6f244a0f
            DisplayApp_SetPower(power_photodiode, power_shunt);
        }
        else
        {
            power_measurement_enabled = 0U;
        }
    }

    uint32_t now = HAL_GetTick();
    if ((adc_running == 0U) &&
        ((power_single_requested != 0U) ||
         ((power_measurement_enabled != 0U) &&
          ((uint32_t)(now - adc_counter) >= 500U))))
    {
        power_single_requested = 0U;
        adc_ready = 0U;
        if (HAL_ADC_Start_DMA(&hadc, (uint32_t *)adc_values, 2U) == HAL_OK)
        {
            adc_running = 1U;
        }
        /* Also bound retries when the ADC cannot be started. */
        adc_counter = now;
    }
    PowerTelemetry_Process();
}

//static void ConvertInttoStr(char *buffer,char *praefix, char *suffix, float var, int significants)
//{
////	uint8_t j = 0;
//////	uint16_t temp = 0;
////	float power = 1;
////	for(uint8_t i = 0; i < sizeof(praefix); i++)
////	{
////		buffer[j] = praefix[i];
////		j = i;
////	}
////	for(int i = 7; i > 0-significants; i--)
////	{
////		j++;
////		if(i > 0)
////		{
////			for(int k = 0; k < i; k++)
////			{
////				power *= 10;
////			}
////			buffer[j] = (char)var%power;
////		}
////		else
////		{
////			for(int k = 0; k > i; k--)
////			{
////				power *= 0.1;
////			}
////		}
////
////	}
//}


/* Called from the main loop, outside the USB interrupt. */
static void ExecuteUsbCommand(uint8_t command)
{
    switch (command)
    {
        case 1U:
            if (power_measurement_enabled == 0U)
            {
                power_measurement_enabled = 1U;
                adc_counter = HAL_GetTick();
            }
            SensorApp_StartContinuous();
            break;
        case 2U:
            power_measurement_enabled = 0U;
            power_single_requested = 0U;
            if (adc_running != 0U)
            {
                (void)HAL_ADC_Stop_DMA(&hadc);
                adc_running = 0U;
            }
            adc_ready = 0U;
            PowerTelemetry_ClearPending();
            SensorApp_Stop();
            break;
		case 3U:
			power_single_requested = 1U;
			SensorApp_Measure();
			break;
		case 4U:
			if(HAL_TIMEx_GetChannelNState(&htim1, TIM_CHANNEL_3) == HAL_TIM_CHANNEL_STATE_READY)
			{
				HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_3);
				HAL_GPIO_WritePin(LED_B1_GPIO_Port, LED_B1_Pin, GPIO_PIN_SET);
			}
			break;
		case 5U:
			HAL_TIMEx_PWMN_Stop(&htim1, TIM_CHANNEL_3);
			break;
		default:
			break;
	}

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
