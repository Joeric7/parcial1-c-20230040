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
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define MODO_MANUAL 0
#define MODO_AUTO   1
#define MODO_PAUSA  2

#define RX_TAM    128   // tamano del buffer circular de entrada
#define TX_TAM    512   // tamano del buffer circular de salida
#define LINEA_MAX 63    // maximo de caracteres por linea

// valor con el que arranca g_ms. Para la prueba de desbordamiento
// poner 0xFFFFFF00 (da la vuelta a los 256 ms). Normal = 0
#define G_MS_INICIAL 0
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim4;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
volatile uint32_t g_ms = G_MS_INICIAL;
volatile uint32_t g_flanco = 0;   // la ISR lo pone en 1 cuando el botón cambia

uint32_t boton_estable = 0;       // 1 = presionado, 0 = suelto (ya confirmado)
uint32_t cuenta_cambio = 0; // ms que lleva la lectura distinta del estado confirmado
uint32_t ms_presionado = 0;
uint32_t larga_contada = 0;
uint32_t cont_cortas = 0;
uint32_t cont_largas = 0;

uint32_t modo = MODO_MANUAL;
uint32_t modo_previo = MODO_MANUAL;   // a donde vuelvo al salir de pausa
uint32_t duty_manual = 25;            // ultimo duty manual guardado
uint32_t duty = 25;                   // duty que se esta aplicando
uint32_t subiendo = 1;
uint32_t ms_rampa = 0;
uint32_t ms_led = 0;
uint32_t evento_corta = 0;            // las pone el boton, las consume main
uint32_t evento_larga = 0;

// uart: buffer circular de entrada
uint8_t rx_buf[RX_TAM];
volatile uint32_t rx_head = 0;        // donde escribe la ISR
volatile uint32_t rx_tail = 0;        // donde lee main
uint8_t rx_byte;                      // aqui llega cada caracter
uint32_t rx_total = 0;                // caracteres recibidos
uint32_t rx_errores = 0;              // errores de la uart
uint32_t rx_overflow = 0;             // veces que se lleno el buffer

// uart: buffer circular de salida
uint8_t tx_buf[TX_TAM];
volatile uint32_t tx_head = 0;        // donde escribe main
volatile uint32_t tx_tail = 0;        // donde lee la ISR
volatile uint32_t tx_len = 0;         // tamano del bloque que se esta mandando
volatile uint32_t tx_enviando = 0;    // 1 = hay un bloque saliendo
uint32_t tx_perdidos = 0;             // caracteres descartados por buffer lleno

// uart: linea que se esta armando
char linea[LINEA_MAX + 1];            // +1 para el '\0'
uint32_t linea_len = 0;               // cuantos caracteres lleva
uint32_t descartando = 0;            // 1 = linea muy larga, ignoro hasta el fin
uint32_t err_overflow = 0;            // lineas que se pasaron de 63

// comandos y telemetria
uint32_t stream_activo = 0;           // 1 = manda telemetria cada 1000 ms
uint32_t ms_stream = 0;
uint32_t ms_uptime = 0;
uint32_t uptime_s = 0;                // segundos desde el arranque
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM4_Init(void);
static void MX_TIM2_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */
void pwm_set_duty(uint32_t duty_pct);
void boton_actualizar(void);
uint32_t tomar_flanco(void);
void cambiar_modo(uint32_t nuevo);
void entrar_pausa(void);
void salir_pausa(void);
uint32_t rx_leer(uint8_t *c);
uint32_t armar_linea(uint8_t c);
void tx_iniciar(void);
void enviar(const char *texto);
void enviar_telemetria(void);
uint32_t leer_numero(const char *s, uint32_t *valor);
void procesar_linea(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
 * @brief  The application entry point.
 * @retval int
 */
int main(void) {

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
	MX_TIM4_Init();
	MX_TIM2_Init();
	MX_USART2_UART_Init();
	/* USER CODE BEGIN 2 */
	HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2);
	HAL_TIM_Base_Start_IT(&htim4);
	HAL_UART_Receive_IT(&huart2, &rx_byte, 1);   // empieza a escuchar 1 byte

	uint32_t h_tick = g_ms;

	pwm_set_duty(duty);

	enviar("Consola lista. Escribe HELP\r\n");
	/* USER CODE END 2 */

	/* Infinite loop */
	/* USER CODE BEGIN WHILE */
	while (1) {
		/* USER CODE END WHILE */

		/* USER CODE BEGIN 3 */

		// Bloque que corre una vez por cada ms
		if ((uint32_t) (g_ms - h_tick) >= 1U) {
			h_tick = h_tick + 1U;

			boton_actualizar();

			// pulsacion corta
			if (evento_corta == 1) {
				evento_corta = 0;
				if (modo == MODO_MANUAL) {
					cambiar_modo(MODO_AUTO);
				} else if (modo == MODO_AUTO) {
					cambiar_modo(MODO_MANUAL);
				}
				// en pausa se ignora
			}

			// pulsacion larga
			if (evento_larga == 1) {
				evento_larga = 0;
				if (modo == MODO_PAUSA) {
					salir_pausa();
				} else {
					entrar_pausa();
				}
			}

			// rampa, solo en AUTO
			if (modo == MODO_AUTO) {
				ms_rampa++;
				if (ms_rampa >= 20) {
					ms_rampa = 0;

					if (subiendo == 1) {
						duty = duty + 1;
						if (duty == 90) {
							subiendo = 0;
						}
					} else {
						duty = duty - 1;
						if (duty == 10) {
							subiendo = 1;
						}
					}
					pwm_set_duty(duty);
				}
			}

			// LED de estado
			ms_led++;
			if (modo == MODO_MANUAL) {
				HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET);
			} else if (modo == MODO_AUTO) {
				if (ms_led >= 500) {
					ms_led = 0;
					HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
				}
			} else {
				if (ms_led >= 125) {
					ms_led = 0;
					HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
				}
			}

			// uptime: sube 1 segundo cada 1000 ms
			ms_uptime++;
			if (ms_uptime >= 1000) {
				ms_uptime = 0;
				uptime_s++;
			}

			// telemetria cada 1000 ms (solo si esta activada)
			ms_stream++;
			if (ms_stream >= 1000) {
				ms_stream = 0;
				if (stream_activo == 1) {
					enviar_telemetria();
				}
			}
		}

		// uart: leer lo que llego y armar lineas
		uint8_t c;
		while (rx_leer(&c) == 1) {
			uint32_t r = armar_linea(c);

			if (r == 1) {
				procesar_linea();   // linea completa: ejecuta el comando
			} else if (r == 2) {
				enviar("ERR OVERFLOW\r\n");
			}
		}
	}
	/* USER CODE END 3 */
}

/**
 * @brief System Clock Configuration
 * @retval None
 */
void SystemClock_Config(void) {
	RCC_OscInitTypeDef RCC_OscInitStruct = { 0 };
	RCC_ClkInitTypeDef RCC_ClkInitStruct = { 0 };
	RCC_PeriphCLKInitTypeDef PeriphClkInit = { 0 };

	/** Initializes the RCC Oscillators according to the specified parameters
	 * in the RCC_OscInitTypeDef structure.
	 */
	RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
	RCC_OscInitStruct.HSIState = RCC_HSI_ON;
	RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
	RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
	RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
	RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
	RCC_OscInitStruct.PLL.PREDIV = RCC_PREDIV_DIV1;
	if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
		Error_Handler();
	}

	/** Initializes the CPU, AHB and APB buses clocks
	 */
	RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
			| RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
	RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
	RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
	RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
	RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

	if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) {
		Error_Handler();
	}
	PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART2
			| RCC_PERIPHCLK_TIM2 | RCC_PERIPHCLK_TIM34;
	PeriphClkInit.Usart2ClockSelection = RCC_USART2CLKSOURCE_PCLK1;
	PeriphClkInit.Tim2ClockSelection = RCC_TIM2CLK_HCLK;
	PeriphClkInit.Tim34ClockSelection = RCC_TIM34CLK_HCLK;
	if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK) {
		Error_Handler();
	}
}

/**
 * @brief TIM2 Initialization Function
 * @param None
 * @retval None
 */
static void MX_TIM2_Init(void) {

	/* USER CODE BEGIN TIM2_Init 0 */

	/* USER CODE END TIM2_Init 0 */

	TIM_ClockConfigTypeDef sClockSourceConfig = { 0 };
	TIM_MasterConfigTypeDef sMasterConfig = { 0 };
	TIM_OC_InitTypeDef sConfigOC = { 0 };

	/* USER CODE BEGIN TIM2_Init 1 */

	/* USER CODE END TIM2_Init 1 */
	htim2.Instance = TIM2;
	htim2.Init.Prescaler = 71;
	htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
	htim2.Init.Period = 999;
	htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
	htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
	if (HAL_TIM_Base_Init(&htim2) != HAL_OK) {
		Error_Handler();
	}
	sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
	if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK) {
		Error_Handler();
	}
	if (HAL_TIM_PWM_Init(&htim2) != HAL_OK) {
		Error_Handler();
	}
	sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
	sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
	if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig)
			!= HAL_OK) {
		Error_Handler();
	}
	sConfigOC.OCMode = TIM_OCMODE_PWM1;
	sConfigOC.Pulse = 250;
	sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
	sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
	if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_2)
			!= HAL_OK) {
		Error_Handler();
	}
	/* USER CODE BEGIN TIM2_Init 2 */

	/* USER CODE END TIM2_Init 2 */
	HAL_TIM_MspPostInit(&htim2);

}

/**
 * @brief TIM4 Initialization Function
 * @param None
 * @retval None
 */
static void MX_TIM4_Init(void) {

	/* USER CODE BEGIN TIM4_Init 0 */

	/* USER CODE END TIM4_Init 0 */

	TIM_ClockConfigTypeDef sClockSourceConfig = { 0 };
	TIM_MasterConfigTypeDef sMasterConfig = { 0 };

	/* USER CODE BEGIN TIM4_Init 1 */

	/* USER CODE END TIM4_Init 1 */
	htim4.Instance = TIM4;
	htim4.Init.Prescaler = 71;
	htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
	htim4.Init.Period = 999;
	htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
	htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
	if (HAL_TIM_Base_Init(&htim4) != HAL_OK) {
		Error_Handler();
	}
	sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
	if (HAL_TIM_ConfigClockSource(&htim4, &sClockSourceConfig) != HAL_OK) {
		Error_Handler();
	}
	sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
	sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
	if (HAL_TIMEx_MasterConfigSynchronization(&htim4, &sMasterConfig)
			!= HAL_OK) {
		Error_Handler();
	}
	/* USER CODE BEGIN TIM4_Init 2 */

	/* USER CODE END TIM4_Init 2 */

}

/**
 * @brief USART2 Initialization Function
 * @param None
 * @retval None
 */
static void MX_USART2_UART_Init(void) {

	/* USER CODE BEGIN USART2_Init 0 */

	/* USER CODE END USART2_Init 0 */

	/* USER CODE BEGIN USART2_Init 1 */

	/* USER CODE END USART2_Init 1 */
	huart2.Instance = USART2;
	huart2.Init.BaudRate = 115200;
	huart2.Init.WordLength = UART_WORDLENGTH_8B;
	huart2.Init.StopBits = UART_STOPBITS_1;
	huart2.Init.Parity = UART_PARITY_NONE;
	huart2.Init.Mode = UART_MODE_TX_RX;
	huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
	huart2.Init.OverSampling = UART_OVERSAMPLING_16;
	huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
	huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
	if (HAL_UART_Init(&huart2) != HAL_OK) {
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
static void MX_GPIO_Init(void) {
	GPIO_InitTypeDef GPIO_InitStruct = { 0 };
	/* USER CODE BEGIN MX_GPIO_Init_1 */

	/* USER CODE END MX_GPIO_Init_1 */

	/* GPIO Ports Clock Enable */
	__HAL_RCC_GPIOC_CLK_ENABLE();
	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();

	/*Configure GPIO pin Output Level */
	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);

	/*Configure GPIO pin : PC13 */
	GPIO_InitStruct.Pin = GPIO_PIN_13;
	GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

	/*Configure GPIO pin : PA5 */
	GPIO_InitStruct.Pin = GPIO_PIN_5;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

	/* EXTI interrupt init*/
	HAL_NVIC_SetPriority(EXTI15_10_IRQn, 0, 0);
	HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

	/* USER CODE BEGIN MX_GPIO_Init_2 */

	/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
// Cambia el brillo del LED. duty_pct es un número de 0 a 100
void pwm_set_duty(uint32_t duty_pct) {

	uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim2);

	uint32_t cuentas_totales = arr + 1;

	uint32_t cuentas_en_alto = (duty_pct * cuentas_totales) / 100;

	__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, cuentas_en_alto);
}

// Entra a un modo desde cero (no sirve para salir de pausa)
void cambiar_modo(uint32_t nuevo) {
	modo = nuevo;
	ms_led = 0;

	if (nuevo == MODO_MANUAL) {
		duty = duty_manual;
		pwm_set_duty(duty);
	} else if (nuevo == MODO_AUTO) {
		duty = 10;
		subiendo = 1;
		ms_rampa = 0;
		pwm_set_duty(duty);
	} else {
		pwm_set_duty(0);
	}
}

// Pausa: guarda el modo y apaga el pwm. duty y subiendo no se tocan
void entrar_pausa(void) {
	modo_previo = modo;
	cambiar_modo(MODO_PAUSA);
}

// Sale de pausa sin reiniciar nada (por eso no uso cambiar_modo)
void salir_pausa(void) {
	modo = modo_previo;
	pwm_set_duty(duty);
	ms_led = 0;
}

// ISR del boton
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
	if (GPIO_Pin == GPIO_PIN_13) {
		g_flanco = 1;
	}
}

// Lee la nota y la borra sin que la ISR se meta en medio
uint32_t tomar_flanco(void) {
	__disable_irq();
	uint32_t hubo = g_flanco;
	g_flanco = 0;
	__enable_irq();
	return hubo;
}

// Se llama una vez por ms
void boton_actualizar(void) {
	// 1 = presionado (en el micro presionado lee 0)
	uint32_t lectura = (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_RESET);

	// Si hubo un flanco, reinicia la espera de 30 ms
	if (tomar_flanco() == 1) {
		cuenta_cambio = 0;
	}

	// Antirrebote
	if (lectura == boton_estable) {
		cuenta_cambio = 0;
	} else {
		cuenta_cambio++;
		if (cuenta_cambio >= 30) {
			cuenta_cambio = 0;
			boton_estable = lectura;

			if (boton_estable == 1)          // pulsacion confirmada
					{
				ms_presionado = 0;
				larga_contada = 0;
			} else if (larga_contada == 0) // pulsacion soltadada confirmada, sin larga previa
					{
				cont_cortas++;
				evento_corta = 1;
			}
		}
	}

	// Pulsacion larga
	if (boton_estable == 1) {
		ms_presionado++;
		if (ms_presionado >= 1500 && larga_contada == 0) {
			larga_contada = 1;
			cont_largas++;
			evento_larga = 1;
		}
	}
}

// Se llama sola cada 1 ms
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
	if (htim->Instance == TIM4) {
		g_ms = g_ms + 1;
	}
}

// ISR de la uart: llego un byte
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
	if (huart->Instance == USART2) {
		uint32_t siguiente = (rx_head + 1) % RX_TAM;

		if (siguiente == rx_tail) {
			rx_overflow++;   // buffer lleno, se pierde el byte
		} else {
			rx_buf[rx_head] = rx_byte;
			rx_head = siguiente;
			rx_total++;
		}
		HAL_UART_Receive_IT(&huart2, &rx_byte, 1);   // listo para el siguiente
	}
}

// Si la uart tiene un error (por ejemplo overrun)
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
	if (huart->Instance == USART2) {
		rx_errores++;
		HAL_UART_Receive_IT(&huart2, &rx_byte, 1);   // volver a escuchar
	}
}

// ISR de la uart: termino de salir un bloque
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
	if (huart->Instance == USART2) {
		tx_tail = (tx_tail + tx_len) % TX_TAM;   // ese bloque ya salio
		tx_enviando = 0;
		tx_iniciar();                            // si queda algo, lo manda
	}
}

// Saca un byte del buffer. Devuelve 1 si habia, 0 si estaba vacio
uint32_t rx_leer(uint8_t *c) {
	if (rx_tail == rx_head) {
		return 0;
	}
	*c = rx_buf[rx_tail];
	rx_tail = (rx_tail + 1) % RX_TAM;
	return 1;
}

// Mete un caracter a la linea.
// Devuelve: 0 = nada, 1 = linea completa, 2 = linea muy larga (overflow)
uint32_t armar_linea(uint8_t c) {
	// fin de linea (CR o LF)
	if (c == '\r' || c == '\n') {
		if (descartando == 1) {
			descartando = 0;
			linea_len = 0;
			err_overflow++;
			return 2;
		}
		if (linea_len > 0) {
			linea[linea_len] = '\0';   // cierra el texto
			linea_len = 0;
			return 1;
		}
		return 0;   // linea vacia: asi CRLF cuenta como una sola orden
	}

	// caracter normal
	if (descartando == 1) {
		return 0;   // la linea se paso, ignoro hasta el fin
	}
	if (linea_len < LINEA_MAX) {
		linea[linea_len] = (char) c;
		linea_len++;
	} else {
		descartando = 1;   // llego el caracter 64
	}
	return 0;
}

// Manda por la uart el bloque que haya en el buffer de salida.
// Solo arranca si no hay otro bloque saliendo.
// (quien la llame desde main debe apagar las interrupciones un instante)
void tx_iniciar(void) {
	if (tx_enviando == 1 || tx_tail == tx_head) {
		return;   // ya hay uno saliendo, o no hay nada que mandar
	}

	uint32_t h = tx_head;
	uint32_t len;
	if (h > tx_tail) {
		len = h - tx_tail;          // todo seguido hasta head
	} else {
		len = TX_TAM - tx_tail; // hasta el final del arreglo (luego da la vuelta)
	}

	tx_len = len;
	tx_enviando = 1;
	if (HAL_UART_Transmit_IT(&huart2, &tx_buf[tx_tail], len) != HAL_OK) {
		tx_enviando = 0; // no pudo arrancar, se reintenta en el siguiente enviar
	}
}

// Manda un texto por la uart SIN esperar: lo copia al buffer y sigue
void enviar(const char *texto) {
	for (uint32_t i = 0; texto[i] != '\0'; i++) {
		uint32_t siguiente = (tx_head + 1) % TX_TAM;

		if (siguiente == tx_tail) {
			tx_perdidos++;   // buffer lleno, descarto lo que sobra
			break;
		}
		tx_buf[tx_head] = (uint8_t) texto[i];
		tx_head = siguiente;
	}

	__disable_irq();
	tx_iniciar();
	__enable_irq();
}

// Arma la linea de telemetria y la manda (se llama cada 1000 ms)
void enviar_telemetria(void) {
	const char *nombre = "MANUAL";
	if (modo == MODO_AUTO) {
		nombre = "AUTO";
	} else if (modo == MODO_PAUSA) {
		nombre = "PAUSA";
	}

	char resp[120];
	snprintf(resp, sizeof(resp),
			"T MODE=%s DUTY=%lu UPTIME=%lu CORTAS=%lu LARGAS=%lu\r\n", nombre,
			(unsigned long) duty, (unsigned long) uptime_s,
			(unsigned long) cont_cortas, (unsigned long) cont_largas);
	enviar(resp);
}

// Revisa que s sea solo digitos y que valga 0..100
// Devuelve: 0 = bien, 1 = no es un numero limpio, 2 = fuera de rango
uint32_t leer_numero(const char *s, uint32_t *valor) {
	if (s[0] == '\0') {
		return 1;   // vacio
	}

	uint32_t v = 0;
	uint32_t muy_grande = 0;

	for (uint32_t i = 0; s[i] != '\0'; i++) {
		if (s[i] < '0' || s[i] > '9') {
			return 1;   // letra, signo menos, etc
		}
		if (muy_grande == 0) {
			v = v * 10 + (s[i] - '0');
			if (v > 100) {
				muy_grande = 1;   // ya no sigo multiplicando para no desbordar
			}
		}
	}

	if (muy_grande == 1) {
		return 2;
	}
	*valor = v;
	return 0;
}

// Ejecuta el comando que esta en linea
void procesar_linea(void) {
	// parte la linea en palabras (ignora espacios al inicio y al final)
	char *cmd = strtok(linea, " \t");
	char *arg = strtok(NULL, " \t");
	char *extra = strtok(NULL, " \t");

	if (cmd == NULL) {
		return;   // solo habia espacios
	}

	if (strcmp(cmd, "HELP") == 0) {
		if (arg != NULL) {
			enviar("ERR ARG\r\n");
		} else {
			enviar("HELP, STATUS, MODE MANUAL, MODE AUTO, DUTY n,\r\n");
			enviar("PAUSE, RESUME, STREAM ON, STREAM OFF\r\n");
			enviar("OK\r\n");
		}
	}

	else if (strcmp(cmd, "STATUS") == 0) {
		if (arg != NULL) {
			enviar("ERR ARG\r\n");
		} else {
			const char *nombre = "MANUAL";
			if (modo == MODO_AUTO) {
				nombre = "AUTO";
			} else if (modo == MODO_PAUSA) {
				nombre = "PAUSA";
			}

			char resp[160];
			snprintf(resp, sizeof(resp),
					"MODE=%s FREQ=1000 DUTY=%lu MANUAL=%lu UPTIME=%lu\r\n",
					nombre, (unsigned long) duty, (unsigned long) duty_manual,
					(unsigned long) uptime_s);
			enviar(resp);

			snprintf(resp, sizeof(resp),
					"CORTAS=%lu LARGAS=%lu ERR_RX=%lu ERR_OVF_RX=%lu ERR_LINEA=%lu ERR_TX=%lu\r\n",
					(unsigned long) cont_cortas, (unsigned long) cont_largas,
					(unsigned long) rx_errores, (unsigned long) rx_overflow,
					(unsigned long) err_overflow, (unsigned long) tx_perdidos);
			enviar(resp);
			enviar("OK\r\n");
		}
	}

	else if (strcmp(cmd, "MODE") == 0) {
		if (arg == NULL || extra != NULL) {
			enviar("ERR ARG\r\n");
		} else if (modo == MODO_PAUSA) {
			enviar("ERR STATE\r\n");
		} else if (strcmp(arg, "MANUAL") == 0) {
			cambiar_modo(MODO_MANUAL);
			enviar("OK\r\n");
		} else if (strcmp(arg, "AUTO") == 0) {
			cambiar_modo(MODO_AUTO);
			enviar("OK\r\n");
		} else {
			enviar("ERR ARG\r\n");
		}
	}

	else if (strcmp(cmd, "DUTY") == 0) {
		if (modo != MODO_MANUAL) {
			enviar("ERR STATE\r\n");   // en AUTO y en PAUSA se rechaza
		} else if (arg == NULL || extra != NULL) {
			enviar("ERR ARG\r\n");
		} else {
			uint32_t n;
			uint32_t r = leer_numero(arg, &n);
			if (r == 1) {
				enviar("ERR ARG\r\n");
			} else if (r == 2) {
				enviar("ERR RANGE\r\n");
			} else {
				duty_manual = n;   // se guarda y se aplica
				duty = n;
				pwm_set_duty(duty);
				enviar("OK\r\n");
			}
		}
	}

	else if (strcmp(cmd, "PAUSE") == 0) {
		if (arg != NULL) {
			enviar("ERR ARG\r\n");
		} else {
			if (modo != MODO_PAUSA) {
				entrar_pausa();   // si ya estaba en pausa no cambia nada
			}
			enviar("OK\r\n");
		}
	}

	else if (strcmp(cmd, "RESUME") == 0) {
		if (arg != NULL) {
			enviar("ERR ARG\r\n");
		} else if (modo != MODO_PAUSA) {
			enviar("ERR STATE\r\n");
		} else {
			salir_pausa();
			enviar("OK\r\n");
		}
	}

	else if (strcmp(cmd, "STREAM") == 0) {
		if (arg == NULL || extra != NULL) {
			enviar("ERR ARG\r\n");
		} else if (strcmp(arg, "ON") == 0) {
			stream_activo = 1;
			enviar("OK\r\n");
		} else if (strcmp(arg, "OFF") == 0) {
			stream_activo = 0;
			enviar("OK\r\n");
		} else {
			enviar("ERR ARG\r\n");
		}
	}

	else {
		enviar("ERR COMMAND\r\n");   // incluye FREQ, que no implementamos
	}
}
/* USER CODE END 4 */

/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler(void) {
	/* USER CODE BEGIN Error_Handler_Debug */
	/* User can add his own implementation to report the HAL error return state */
	__disable_irq();
	while (1) {
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
