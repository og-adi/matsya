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
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "waveform_gen.h"
#include "oled_ssd1306.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

DAC_HandleTypeDef hdac;
DMA_HandleTypeDef hdma_dac1;

I2C_HandleTypeDef hi2c1;

TIM_HandleTypeDef htim6;
TIM_HandleTypeDef htim7;

/* USER CODE BEGIN PV */
#define HALF_BUF_SAMPLES 4096u
static uint16_t dac_buffer[2 * HALF_BUF_SAMPLES];
static volatile sonar_params_t g_current_params;
static volatile float g_sample_rate_hz = 1000000.0f;
static volatile uint8_t g_params_dirty = 0;
static float last_turbidity = -1.0f, last_depth = -1.0f;

/* Hysteresis set to 0 for instant linear response */
#define HYSTERESIS_TURB   0.0f
#define HYSTERESIS_DEPTH  0.0f

/* ★★★ NEW: ADC update control ★★★ */
static volatile uint8_t g_adc_update_pending = 0;

static volatile uint32_t g_last_turb_raw = 0;
static volatile uint32_t g_last_depth_raw = 0;

static volatile float g_display_turbidity = 0.0f;
static volatile float g_display_depth_m = 0.0f;

/* Mode and other globals */
static volatile uint8_t g_print_ready = 0;
static volatile modulation_type_t g_current_mode = MOD_LFM;

/* Single OLED display */
static oled_t oled;
static uint8_t g_oled_ok = 0;

/* Display update control */
static volatile uint8_t g_display_update_requested = 1;  /* request initial update */
static volatile uint32_t g_last_display_update_ms = 0;
#define DISPLAY_UPDATE_INTERVAL_MS  50   /* update at most every 50 ms */

/* Helper: compute x offset to center a string on the 128‑pixel wide display. */
static uint8_t centered_x(const char *str)
{
    uint8_t len = strlen(str);
    uint16_t pixel_width = len * 6;
    if (pixel_width >= OLED_WIDTH) return 0;
    return (OLED_WIDTH - pixel_width) / 2;
}
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_DAC_Init(void);
static void MX_TIM6_Init(void);
static void MX_ADC1_Init(void);
static void MX_TIM7_Init(void);
static void MX_I2C1_Init(void);
/* USER CODE BEGIN PFP */
static void seed_initial_waveform(void);
static void adc_slow_task_update(float turbidity_raw, float depth_raw_m);
static const char *mode_name_string(modulation_type_t mode);
static void update_display(void);
static void process_adc_values(void);   /* ★★★ NEW ★★★ */
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

  HAL_Init();
  SystemClock_Config();

  MX_GPIO_Init();
  MX_DMA_Init();
  MX_DAC_Init();
  MX_TIM6_Init();
  MX_ADC1_Init();
  MX_TIM7_Init();
  MX_I2C1_Init();

  /* USER CODE BEGIN 2 */
  HAL_DBGMCU_EnableDBGSleepMode();
  HAL_DBGMCU_EnableDBGStopMode();
  HAL_DBGMCU_EnableDBGStandbyMode();
  seed_initial_waveform();

  /* Initialise the OLED */
  g_oled_ok = (oled_init(&oled, &hi2c1, OLED_ADDR) == HAL_OK);

  HAL_TIM_Base_Start(&htim6);
  HAL_DAC_Start_DMA(&hdac, DAC_CHANNEL_1,
                     (uint32_t *)dac_buffer, 2 * HALF_BUF_SAMPLES,
                     DAC_ALIGN_12B_R);
  HAL_TIM_Base_Start_IT(&htim7);
  /* USER CODE END 2 */

  while (1)
  {
    /* ★★★ ADC acquisition triggered by TIM7 flag ★★★ */
    if (g_adc_update_pending) {
        g_adc_update_pending = 0;        /* clear flag */
        process_adc_values();            /* read ADC, convert, update waveform & display flag */
    }

    /* ★★★ Non‑blocking display update ★★★ */
    if (g_oled_ok && g_display_update_requested) {
        uint32_t now = HAL_GetTick();
        if (now - g_last_display_update_ms >= DISPLAY_UPDATE_INTERVAL_MS) {
            g_display_update_requested = 0;
            g_last_display_update_ms = now;
            update_display();
        }
    }

    /* Sleep until next interrupt (TIM7 or button) */
    __WFI();
  }
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 180;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) Error_Handler();

  if (HAL_PWREx_EnableOverDrive() != HAL_OK) Error_Handler();

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK) Error_Handler();
}

/**
  * @brief ADC1 Initialization Function
  */
static void MX_ADC1_Init(void)
{
  ADC_ChannelConfTypeDef sConfig = {0};

  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = ENABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;      /* ★ CHANGED: we start/stop manually ★ */
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 2;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK) Error_Handler();

  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_84CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) Error_Handler();

  sConfig.Channel = ADC_CHANNEL_1;
  sConfig.Rank = 2;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) Error_Handler();
}

/**
  * @brief DAC Initialization Function
  */
static void MX_DAC_Init(void)
{
  DAC_ChannelConfTypeDef sConfig = {0};

  hdac.Instance = DAC;
  if (HAL_DAC_Init(&hdac) != HAL_OK) Error_Handler();

  sConfig.DAC_Trigger = DAC_TRIGGER_T6_TRGO;
  sConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
  if (HAL_DAC_ConfigChannel(&hdac, &sConfig, DAC_CHANNEL_1) != HAL_OK) Error_Handler();
}

/**
  * @brief I2C1 Initialization Function
  */
static void MX_I2C1_Init(void)
{
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 400000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 120;   // not used in master mode, but keep as is
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_ENABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK) Error_Handler();
}

/**
  * @brief TIM6 Initialization Function
  */
static void MX_TIM6_Init(void)
{
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  htim6.Instance = TIM6;
  htim6.Init.Prescaler = 0;
  htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim6.Init.Period = 89;
  htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim6) != HAL_OK) Error_Handler();

  sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim6, &sMasterConfig) != HAL_OK) Error_Handler();
}

/**
  * @brief TIM7 Initialization Function
  */
static void MX_TIM7_Init(void)
{
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  htim7.Instance = TIM7;
  htim7.Init.Prescaler = 8999;
  htim7.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim7.Init.Period = 499;
  htim7.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim7) != HAL_OK) Error_Handler();

  sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim7, &sMasterConfig) != HAL_OK) Error_Handler();
}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{
  __HAL_RCC_DMA1_CLK_ENABLE();
  HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn);
}

/**
  * @brief GPIO Initialization Function
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* ★ CRITICAL: Configure PA0 and PA1 as analog inputs ★ */
  GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* I2C1: PB8 (SCL), PB9 (SDA) */
  GPIO_InitStruct.Pin = GPIO_PIN_8 | GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER button PC13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

/* USER CODE BEGIN 4 */

static const char *mode_name_string(modulation_type_t mode)
{
    switch (mode) {
        case MOD_GEOMETRIC:   return "GEOMETRIC";
        case MOD_PHASE_CODED: return "BARKER-7";
        case MOD_LFM:
        default:              return "LFM";
    }
}

static void seed_initial_waveform(void)
{
    g_current_params = sonar_adaptive_params(0.3f, 20.0f);
    sonar_fill_waveform(dac_buffer, HALF_BUF_SAMPLES,
                         (sonar_params_t *)&g_current_params,
                         g_sample_rate_hz, WINDOW_HAMMING, g_current_mode);
    sonar_fill_waveform(dac_buffer + HALF_BUF_SAMPLES, HALF_BUF_SAMPLES,
                         (sonar_params_t *)&g_current_params,
                         g_sample_rate_hz, WINDOW_HAMMING, g_current_mode);
}

static void adc_slow_task_update(float turbidity_raw, float depth_raw_m)
{
    if (fabsf(turbidity_raw - last_turbidity) < HYSTERESIS_TURB &&
        fabsf(depth_raw_m   - last_depth)     < HYSTERESIS_DEPTH) {
        return;
    }
    last_turbidity = turbidity_raw;
    last_depth     = depth_raw_m;

    sonar_params_t p = sonar_adaptive_params(turbidity_raw, depth_raw_m);
    g_current_params = p;
    g_params_dirty = 1;
}

/* ★★★ NEW: ADC processing – called from main loop ★★★ */
static void process_adc_values(void)
{
    uint32_t turb_raw;
    uint32_t depth_raw;

    /*
     * Start the two-channel ADC scan:
     * Rank 1 = PA0 (turbidity)
     * Rank 2 = PA1 (depth)
     */
    if (HAL_ADC_Start(&hadc1) != HAL_OK)
        return;

    /* Wait for first conversion (PA0) */
    if (HAL_ADC_PollForConversion(&hadc1, 10) != HAL_OK) {
        HAL_ADC_Stop(&hadc1);
        return;
    }
    turb_raw = HAL_ADC_GetValue(&hadc1);

    /* Wait for second conversion (PA1) */
    if (HAL_ADC_PollForConversion(&hadc1, 10) != HAL_OK) {
        HAL_ADC_Stop(&hadc1);
        return;
    }
    depth_raw = HAL_ADC_GetValue(&hadc1);

    HAL_ADC_Stop(&hadc1);

    /* Store raw values (optional) */
    g_last_turb_raw = turb_raw;
    g_last_depth_raw = depth_raw;

    /* Convert to application units */
    float turbidity = (float)turb_raw / 4095.0f;
    float depth_m   = ((float)depth_raw / 4095.0f) * 200.0f;

    /* Store display values */
    g_display_turbidity = turbidity;
    g_display_depth_m = depth_m;

    /* Update waveform parameters using the same values */
    adc_slow_task_update(turbidity, depth_m);

    /* Request OLED update */
    g_display_update_requested = 1;
}

/* Update the display – called from main loop */
static void update_display(void)
{
    char line1[32], line2[32], line3[32];

    int turbidity_percent = (int)(g_display_turbidity * 100.0f + 0.5f);
    int depth_m = (int)(g_display_depth_m + 0.5f);

    snprintf(line1, sizeof(line1), "MODE: %s", mode_name_string(g_current_mode));
    snprintf(line2, sizeof(line2), "TURBIDITY: %3d %%", turbidity_percent);
    snprintf(line3, sizeof(line3), "DEPTH: %3d M", depth_m);

    oled_clear(&oled);
    oled_draw_string(&oled, centered_x(line1), 2, line1);
    oled_draw_string(&oled, centered_x(line2), 4, line2);
    oled_draw_string(&oled, centered_x(line3), 6, line3);

    /* If flush fails, mark OLED as not OK to avoid repeated attempts */
    if (oled_flush(&oled) != HAL_OK) {
        g_oled_ok = 0;
    }
}

/* DMA half/full callbacks */
void HAL_DAC_ConvHalfCpltCallbackCh1(DAC_HandleTypeDef *hdac)
{
    if (g_params_dirty) {
        sonar_fill_waveform(dac_buffer + HALF_BUF_SAMPLES, HALF_BUF_SAMPLES,
                             (sonar_params_t *)&g_current_params,
                             g_sample_rate_hz, WINDOW_HAMMING, g_current_mode);
    }
}

void HAL_DAC_ConvCpltCallbackCh1(DAC_HandleTypeDef *hdac)
{
    if (g_params_dirty) {
        sonar_fill_waveform(dac_buffer, HALF_BUF_SAMPLES,
                             (sonar_params_t *)&g_current_params,
                             g_sample_rate_hz, WINDOW_HAMMING, g_current_mode);
        g_params_dirty = 0;
    }
}

/* USER button – cycle modulation mode */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GPIO_PIN_13) {
        g_current_mode = (modulation_type_t)((g_current_mode + 1) % 3);
        g_params_dirty = 1;
        g_display_update_requested = 1;
    }
}

/* ★★★ SIMPLIFIED TIM7 ISR: only sets a flag ★★★ */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM7) {
        /* Just tell the main loop to acquire new ADC samples */
        g_adc_update_pending = 1;
    }
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  __disable_irq();
  while (1) {}
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
