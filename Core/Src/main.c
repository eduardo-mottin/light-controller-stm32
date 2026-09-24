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
#include "adc.h"
#include "dma.h"
#include "i2c.h"
#include "stm32f3xx_hal.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdint.h>
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

// Conxtexto que será compartilhado entre os estados
typedef struct {
} Context;

// Definição do código de retorno dos estados
typedef enum {
  SETUP_STATE,               // Estado de incialização
  READ_SENSOR_STATE,         // Leitura do sensor de luminosidade
  READ_POT_STATE,            // Leitura do potenciômetro via ADC
  CONTROL_STATE,             // Calcula o PWM
  APPLY_PWM_STATE,           // Atualiza o PWM do LED
  UPDATE_INDICATORS_STATE,   // Atualiza os 4 LEDs indicadores
  WAIT_STATE,                // Aguarda o tempo de resposta
  ERROR_STATE,               // Trata o erro
} StateId;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

// Porcentagens do PWM
#define PWM_0_PCT 0
#define PWM_25_PCT 250
#define PWM_50_PCT 500
#define PWM_75_PCT 750
#define PWM_90_PCT 900

#define ARR 999

#define ERROR_BLINK_DELAY_MS 250

#define PWM_CONTROL_STEP 100

// Variáveis I²C
#define GY302_I2C_ADDR (0x5C << 1)
#define GY302_I2C_POWER_ON 0x01
#define GY302_I2C_ON_TIME_HR_MODE 0x20

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

uint32_t sensor_value = 0;

uint16_t adc_buffer[16];
uint16_t pot_value = 0;

uint16_t pwm_target = 0;
uint16_t pwm_current = 0;

uint16_t reponso_delay_ms = 100;
uint16_t last_update_ms = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

// Protótipo da função estado
typedef StateId StateFunction(Context *ctx) ;

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

// Habilita o uso do printf para enviar dados pelo UART
void _write(int file, char *ptr, int len) {
  // Transmit the data over UART
  HAL_UART_Transmit(&huart2, (uint8_t*)ptr, len, HAL_MAX_DELAY);
}

// Função para obter a média dos valores do potenciometro
uint16_t get_pot_average(uint16_t *vector)
{
  uint16_t sum = 0;
  for (uint16_t i = 0; i < 16; i++)
  {
    sum += vector[i] >> 4; // divide por 16, equivalente a deslocar 4 bits para a direita
  }

  return sum;
}

// Função do estado de setup
StateId state_setup(Context *ctx) 
{
  // printf("[INFO] Starting system...\n");

  // Incia o PWM do TIM3 CH1
  if (HAL_TIM_PWM_Start( &htim3, TIM_CHANNEL_1) != HAL_OK) {
    // printf("[ERROR] Erro starting TIM3 PWM\n");
    return ERROR_STATE;
  }

  // Inicia o PWM desligado
  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, PWM_0_PCT);

  pwm_current = 0;
  pwm_target = 0;

  // Incia com os LEDs desligados
  HAL_GPIO_WritePin(LED_D2_GPIO_Port, LED_D2_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LED_D3_GPIO_Port, LED_D3_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LED_D4_GPIO_Port, LED_D4_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LED_D5_GPIO_Port, LED_D5_Pin, GPIO_PIN_RESET);

  // printf("[INFO] System OK\n");

  return READ_SENSOR_STATE;
}

// Função do estado de leitura do sensor
StateId state_read_sensor(Context *ctx) 
{
  // printf("[INFO] Reading sensor...\n");

  uint8_t power_on = GY302_I2C_POWER_ON;
  uint8_t measure = GY302_I2C_ON_TIME_HR_MODE;
  uint8_t rx_data[2] = {0};

  // Solicia iniciação do sensor
  HAL_I2C_Master_Transmit(&hi2c1, GY302_I2C_ADDR, &power_on, 1, 100);

  // Solicia a leitura da luminosidade
  HAL_I2C_Master_Transmit(&hi2c1, GY302_I2C_ADDR, &measure, 1, 100);

  // Tempo de leitura (datasheet)
  HAL_Delay(180);

  // Lê o sensor de luminosidade
  if (HAL_I2C_Master_Receive(&hi2c1, GY302_I2C_ADDR, rx_data, 2, 100) != HAL_OK) {
    printf("[ERROR] Erro reading sensor\n");
    return ERROR_STATE;
  }
  
  // Converte o valor bruto
  uint16_t raw_val = (rx_data[0] << 8) | rx_data[1];
  sensor_value = (uint32_t)raw_val / 1.2;

  printf("[INFO] Raw: %u | Lux: %lu\n", raw_val, sensor_value);

  return READ_POT_STATE;
}

// Função do estado de leitura do potenciometro
StateId state_read_pot(Context *ctx) 
{
  // printf("[INFO] Reading potentiometer...\n");

  // Obtêm o valor
  pot_value = get_pot_average(adc_buffer);

  // printf("[INFO] Potentiometer: %u\n", pot_value);

  // Transforma o adc em milisegudos (20ms a 4s)
  reponso_delay_ms = 20 + ((uint32_t)pot_value / 4095) * (3980 / 4095);

  // printf("[INFO] Response time: %u\n", pot_value);

  return CONTROL_STATE;
}

// Função do estado de controle e cálculo do PWM
StateId state_control(Context *ctx)
{
  // printf("[INFO] Controlling PWM...\n");

  // Converte o nível do sensor em em CCR (duty cycle)
  pwm_target = ((uint32_t)sensor_value * ARR) / 54612;

  // limita o valor
  if (pwm_target > 999)
    pwm_target = 999;

  printf("[INFO] Current: %u | Target: %u\n", pwm_current, pwm_target);

  return APPLY_PWM_STATE;
}

// Função do estado de aplicação do PWM
StateId state_apply_pwm(Context *ctx)
{
  // printf("[INFO] Applying PWM...======================\n");

  // Reduz o PWM aos poucos
  if (pwm_current < pwm_target)
  {
    if (pwm_current + PWM_CONTROL_STEP >= pwm_target)
    {
      pwm_current = pwm_target;
    }
    else 
    {
      pwm_current = pwm_current + PWM_CONTROL_STEP;
    }
  }
  else if (pwm_current > pwm_target)
  {
    if (pwm_current - PWM_CONTROL_STEP <= 0) 
    {
      pwm_current = 0;
    }
    else 
    {
      pwm_current = pwm_current - PWM_CONTROL_STEP;
    }
  }

  // Atualiza o valor do CCR
  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, pwm_current);

  // printf("[INFO] TIM channel 1 CRR: %u\n", pwm_current);

  return UPDATE_INDICATORS_STATE;
}

//Função do estado de atualização dos indicadores
StateId state_update_indicators(Context *ctx)
{
  // printf("[INFO] Updating indicators...\n");

  // Verifica qual o LED que deve ser ligado e desliga os demais
  
  // 25% do PWM
  if (pwm_current >= PWM_25_PCT)
  {
    HAL_GPIO_WritePin(LED_D2_GPIO_Port, LED_D2_Pin, GPIO_PIN_SET);
  }
  else 
  {
    HAL_GPIO_WritePin(LED_D2_GPIO_Port, LED_D2_Pin, GPIO_PIN_RESET);
  }
  
  // 50% do PWM
  if (pwm_current >= PWM_50_PCT)
  {
    HAL_GPIO_WritePin(LED_D3_GPIO_Port, LED_D3_Pin, GPIO_PIN_SET);
  }
  else 
  {  
    HAL_GPIO_WritePin(LED_D3_GPIO_Port, LED_D3_Pin, GPIO_PIN_RESET);
  }
  
  // 75% do PWM
  if (pwm_current >= PWM_75_PCT)
  {
    HAL_GPIO_WritePin(LED_D4_GPIO_Port, LED_D4_Pin, GPIO_PIN_SET);
  }
  else 
  {
    HAL_GPIO_WritePin(LED_D4_GPIO_Port, LED_D4_Pin, GPIO_PIN_RESET);
  }
  
  // 90% do PWM
  if (pwm_current >= PWM_90_PCT)
  {
    HAL_GPIO_WritePin(LED_D5_GPIO_Port, LED_D5_Pin, GPIO_PIN_SET);
  }
  else 
  {
    HAL_GPIO_WritePin(LED_D5_GPIO_Port, LED_D5_Pin, GPIO_PIN_RESET);
  }

  return WAIT_STATE;
}

// Função do estado de aguardo do tempo de leitura do sensor
StateId state_wait(Context *ctx)
{
  // Verifica se o tempo atual é maior que o tempo de resposta
  if((HAL_GetTick() - last_update_ms) >= reponso_delay_ms)
  {
    // Atualiza o ultima atualização
    last_update_ms = HAL_GetTick();

    return READ_SENSOR_STATE;
  }

  return APPLY_PWM_STATE;
}

// Função do estado de erro
StateId state_error(Context *ctx)
{
  // printf("[ERROR] Error\n");

  // Deliga o PWM 
  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, PWM_0_PCT);

  // Começa a piscar os LEDs indicadores
  while (1) {
    HAL_GPIO_TogglePin(LED_D2_GPIO_Port, LED_D2_Pin);
    HAL_GPIO_TogglePin(LED_D3_GPIO_Port, LED_D3_Pin);
    HAL_GPIO_TogglePin(LED_D4_GPIO_Port, LED_D4_Pin);
    HAL_GPIO_TogglePin(LED_D5_GPIO_Port, LED_D5_Pin);

    HAL_Delay(ERROR_BLINK_DELAY_MS);
  }
}


StateId currentState = SETUP_STATE;

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  // Inicia tipos
  Context ctx;

  // Define tabela de estados
  StateFunction *state_table[] = {
    [SETUP_STATE] = state_setup,
    [READ_SENSOR_STATE] = state_read_sensor,
    [READ_POT_STATE] = state_read_pot,
    [CONTROL_STATE] = state_control,
    [APPLY_PWM_STATE] = state_apply_pwm,
    [UPDATE_INDICATORS_STATE] = state_update_indicators,
    [WAIT_STATE] = state_wait,
    [ERROR_STATE] = state_error
  };


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
  MX_ADC1_Init();
  MX_I2C1_Init();
  MX_USART2_UART_Init();
  MX_TIM1_Init();
  MX_TIM3_Init();
  /* USER CODE BEGIN 2 */

  // Inicializa DMA para a leitura do potenciometro
  HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);
  HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_buffer, 16);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    // Inicia máquina de estados
    currentState = state_table[currentState](&ctx);
  }

  return 0;
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
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL4;
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

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_I2C1|RCC_PERIPHCLK_TIM1
                              |RCC_PERIPHCLK_ADC12;
  PeriphClkInit.Adc12ClockSelection = RCC_ADC12PLLCLK_DIV16;
  PeriphClkInit.I2c1ClockSelection = RCC_I2C1CLKSOURCE_HSI;
  PeriphClkInit.Tim1ClockSelection = RCC_TIM1CLK_HCLK;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

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
