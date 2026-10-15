/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : I2C Bus Scanner
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include <stdio.h>

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

I2C_HandleTypeDef hi2c1;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/

void SystemClock_Config(void);

static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_I2C1_Init(void);

/* USER CODE BEGIN PFP */

void I2C_Scan(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */


/* ============================================================
 * PRINTF -> USART2
 * ============================================================ */

int __io_putchar(int ch)
{
    HAL_UART_Transmit(&huart2,
                      (uint8_t *)&ch,
                      1,
                      HAL_MAX_DELAY);

    return ch;
}


/* ============================================================
 * I2C BUS SCANNER
 * ============================================================ */

void I2C_Scan(void)
{
    HAL_StatusTypeDef result;

    uint8_t devices_found = 0;


    printf("\r\n");
    printf("========================================\r\n");
    printf("            I2C BUS SCANNER\r\n");
    printf("========================================\r\n");

    printf("Scanning addresses 0x08 - 0x77...\r\n");
    printf("\r\n");


    /*
     * Scan the normal 7-bit I2C address range.
     *
     * HAL_I2C_IsDeviceReady() expects the 7-bit
     * address shifted left by one bit.
     */

    for (uint8_t address = 0x08;
         address <= 0x77;
         address++)
    {
        result = HAL_I2C_IsDeviceReady(
                    &hi2c1,
                    (uint16_t)(address << 1),
                    3,
                    20);


        if (result == HAL_OK)
        {
            printf(">>> DEVICE FOUND AT 0x%02X <<<\r\n",
                   address);

            devices_found++;
        }
    }


    printf("\r\n");
    printf("----------------------------------------\r\n");


    if (devices_found == 0)
    {
        printf("NO I2C DEVICES FOUND\r\n");
    }

    else
    {
        printf("SCAN COMPLETE\r\n");

        printf("%u DEVICE(S) FOUND\r\n",
               devices_found);
    }


    printf("----------------------------------------\r\n");
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


    /*
     * Reset of all peripherals,
     * Initializes Flash interface and SysTick.
     */

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

    MX_I2C1_Init();


    /* USER CODE BEGIN 2 */


    /*
     * Allow power rails and peripherals
     * to settle before scanning.
     */

    HAL_Delay(500);


    printf("\r\n");
    printf("========================================\r\n");
    printf("              PCB I2C TEST\r\n");
    printf("========================================\r\n");


    /*
     * Run the I2C scanner once.
     */

    I2C_Scan();


    printf("\r\n");
    printf("I2C scan finished.\r\n");
    printf("Press RESET to scan again.\r\n");


    /* USER CODE END 2 */


    /* Infinite loop */

    /* USER CODE BEGIN WHILE */

    while (1)
    {
        HAL_StatusTypeDef result;

        result = HAL_I2C_IsDeviceReady(&hi2c1,
                                       (0x5A << 1),
                                       1,
                                       100);

        if (result == HAL_OK)
        {
            printf("0x5A ACK\r\n");
        }
        else
        {
            printf("0x5A NO ACK\r\n");
        }

        HAL_Delay(100);
    }

    /* USER CODE END WHILE */


    /* USER CODE BEGIN 3 */

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


    /**
     * Configure the main internal regulator output voltage
     */

    if (HAL_PWREx_ControlVoltageScaling(
            PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
    {
        Error_Handler();
    }


    /**
     * Initializes the RCC Oscillators
     */

    RCC_OscInitStruct.OscillatorType =
        RCC_OSCILLATORTYPE_HSI;

    RCC_OscInitStruct.HSIState =
        RCC_HSI_ON;

    RCC_OscInitStruct.HSICalibrationValue =
        RCC_HSICALIBRATION_DEFAULT;

    RCC_OscInitStruct.PLL.PLLState =
        RCC_PLL_ON;

    RCC_OscInitStruct.PLL.PLLSource =
        RCC_PLLSOURCE_HSI;

    RCC_OscInitStruct.PLL.PLLM =
        1;

    RCC_OscInitStruct.PLL.PLLN =
        10;

    RCC_OscInitStruct.PLL.PLLP =
        RCC_PLLP_DIV7;

    RCC_OscInitStruct.PLL.PLLQ =
        RCC_PLLQ_DIV2;

    RCC_OscInitStruct.PLL.PLLR =
        RCC_PLLR_DIV2;


    if (HAL_RCC_OscConfig(
            &RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }


    /**
     * Initializes CPU, AHB and APB clocks
     */

    RCC_ClkInitStruct.ClockType =
        RCC_CLOCKTYPE_HCLK |
        RCC_CLOCKTYPE_SYSCLK |
        RCC_CLOCKTYPE_PCLK1 |
        RCC_CLOCKTYPE_PCLK2;


    RCC_ClkInitStruct.SYSCLKSource =
        RCC_SYSCLKSOURCE_PLLCLK;


    RCC_ClkInitStruct.AHBCLKDivider =
        RCC_SYSCLK_DIV1;


    RCC_ClkInitStruct.APB1CLKDivider =
        RCC_HCLK_DIV1;


    RCC_ClkInitStruct.APB2CLKDivider =
        RCC_HCLK_DIV1;


    if (HAL_RCC_ClockConfig(
            &RCC_ClkInitStruct,
            FLASH_LATENCY_4) != HAL_OK)
    {
        Error_Handler();
    }
}


/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

    /* USER CODE BEGIN I2C1_Init 0 */

    /* USER CODE END I2C1_Init 0 */


    /* USER CODE BEGIN I2C1_Init 1 */

    /* USER CODE END I2C1_Init 1 */


    hi2c1.Instance =
        I2C1;


    hi2c1.Init.Timing =
        0x10D19CE4;


    hi2c1.Init.OwnAddress1 =
        0;


    hi2c1.Init.AddressingMode =
        I2C_ADDRESSINGMODE_7BIT;


    hi2c1.Init.DualAddressMode =
        I2C_DUALADDRESS_DISABLE;


    hi2c1.Init.OwnAddress2 =
        0;


    hi2c1.Init.OwnAddress2Masks =
        I2C_OA2_NOMASK;


    hi2c1.Init.GeneralCallMode =
        I2C_GENERALCALL_DISABLE;


    hi2c1.Init.NoStretchMode =
        I2C_NOSTRETCH_DISABLE;


    if (HAL_I2C_Init(&hi2c1) != HAL_OK)
    {
        Error_Handler();
    }


    /**
     * Configure Analogue filter
     */

    if (HAL_I2CEx_ConfigAnalogFilter(
            &hi2c1,
            I2C_ANALOGFILTER_ENABLE) != HAL_OK)
    {
        Error_Handler();
    }


    /**
     * Configure Digital filter
     */

    if (HAL_I2CEx_ConfigDigitalFilter(
            &hi2c1,
            0) != HAL_OK)
    {
        Error_Handler();
    }


    /* USER CODE BEGIN I2C1_Init 2 */

    /* USER CODE END I2C1_Init 2 */
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


    huart2.Instance =
        USART2;


    huart2.Init.BaudRate =
        115200;


    huart2.Init.WordLength =
        UART_WORDLENGTH_8B;


    huart2.Init.StopBits =
        UART_STOPBITS_1;


    huart2.Init.Parity =
        UART_PARITY_NONE;


    huart2.Init.Mode =
        UART_MODE_TX_RX;


    huart2.Init.HwFlowCtl =
        UART_HWCONTROL_NONE;


    huart2.Init.OverSampling =
        UART_OVERSAMPLING_16;


    huart2.Init.OneBitSampling =
        UART_ONE_BIT_SAMPLE_DISABLE;


    huart2.AdvancedInit.AdvFeatureInit =
        UART_ADVFEATURE_NO_INIT;


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


    /* Configure GPIO pin Output Level */

    HAL_GPIO_WritePin(
        LD2_GPIO_Port,
        LD2_Pin,
        GPIO_PIN_RESET);


    /* Configure GPIO pin : B1_Pin */

    GPIO_InitStruct.Pin =
        B1_Pin;

    GPIO_InitStruct.Mode =
        GPIO_MODE_IT_FALLING;

    GPIO_InitStruct.Pull =
        GPIO_NOPULL;

    HAL_GPIO_Init(
        B1_GPIO_Port,
        &GPIO_InitStruct);


    /*
     * Configure encoder pins.
     *
     * These remain as generated by CubeMX,
     * although they are not used by this test.
     */

    GPIO_InitStruct.Pin =
        ENC_A_Pin |
        ENC_B_Pin;

    GPIO_InitStruct.Mode =
        GPIO_MODE_IT_RISING_FALLING;

    GPIO_InitStruct.Pull =
        GPIO_NOPULL;

    HAL_GPIO_Init(
        GPIOA,
        &GPIO_InitStruct);


    /* Configure LD2 */

    GPIO_InitStruct.Pin =
        LD2_Pin;

    GPIO_InitStruct.Mode =
        GPIO_MODE_OUTPUT_PP;

    GPIO_InitStruct.Pull =
        GPIO_NOPULL;

    GPIO_InitStruct.Speed =
        GPIO_SPEED_FREQ_LOW;

    HAL_GPIO_Init(
        LD2_GPIO_Port,
        &GPIO_InitStruct);


    /* Configure PA12 */

    GPIO_InitStruct.Pin =
        GPIO_PIN_12;

    GPIO_InitStruct.Mode =
        GPIO_MODE_INPUT;

    GPIO_InitStruct.Pull =
        GPIO_NOPULL;

    HAL_GPIO_Init(
        GPIOA,
        &GPIO_InitStruct);


    /* EXTI interrupt init */

    HAL_NVIC_SetPriority(
        EXTI0_IRQn,
        0,
        0);

    HAL_NVIC_EnableIRQ(
        EXTI0_IRQn);


    HAL_NVIC_SetPriority(
        EXTI1_IRQn,
        0,
        0);

    HAL_NVIC_EnableIRQ(
        EXTI1_IRQn);


    /* USER CODE BEGIN MX_GPIO_Init_2 */

    /* USER CODE END MX_GPIO_Init_2 */
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

    __disable_irq();


    while (1)
    {
    }

    /* USER CODE END Error_Handler_Debug */
}


#ifdef USE_FULL_ASSERT

/**
  * @brief Reports the name of the source file and source line
  *        where assert_param error occurred.
  */
void assert_failed(uint8_t *file,
                   uint32_t line)
{
    /* USER CODE BEGIN 6 */

    /* USER CODE END 6 */
}

#endif /* USE_FULL_ASSERT */
