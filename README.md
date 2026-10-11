# Controlador de iluminacion con consola serial

Proyecto hecho en C (sin RTOS) para el STM32 Nucleo-F303RE. Controla el brillo de un LED con PWM, recibe comandos por UART y usa un boton con interrupcion.

Nota: el enunciado era para la FRDM-MCXA156, pero lo adapte a la placa que tengo.

## Materiales

- Nucleo-F303RE
- STM32CubeIDE
- Un LED con resistencia de 1k y cables
- Un terminal serial

## Como compilarlo

1. Abrir STM32CubeIDE y abrir el proyecto desde la carpeta del repositorio.
2. Compilar con Project > Build Project.
3. Conectar la placa por USB y darle Run.
4. Abrir el terminal en el puerto del ST-LINK a 115200 baudios, 8N1.

Al arrancar sale "Consola lista. Escribe HELP".

## Conexiones

El LED va del pin PB3 (D3) a la resistencia, de ahi al LED y el otro lado a GND.
El boton y el LED de estado son los de la placa. La UART usa el USB de la placa, no hay que conectar nada mas.

## Recursos usados

| Funcion | Pin | Periferico | Interrupcion |
|---|---|---|---|
| PWM del LED | PB3 | TIM2 canal 2 | no usa |
| Tick de 1 ms | - | TIM4 | TIM4_IRQn |
| Boton | PC13 | EXTI13 | EXTI15_10_IRQn |
| LED de estado | PA5 | GPIO | - |
| UART | PA2 / PA3 | USART2 | USART2_IRQn |

El reloj de la placa es de 72 MHz. Con prescaler 71 los timers cuentan a 1 MHz, y con period 999 dan 1 kHz (el tick de 1 ms y el PWM).

## Como funciona

Tiene tres modos:

- MANUAL: PWM al 25% al inicio y el LED de estado fijo.
- AUTO: el brillo sube y baja entre 10% y 90% (un punto cada 20 ms) y el LED de estado parpadea cada 500 ms.
- PAUSA: PWM en 0 y el LED de estado parpadea cada 125 ms. Al salir sigue donde se quedo.

Con el boton:

- Pulsacion corta: cambia entre MANUAL y AUTO (en pausa no hace nada).
- Pulsacion larga (1.5 s): entra o sale de PAUSA.

El antirrebote se hace en el main, esperando 30 ms estables. La interrupcion del boton solo avisa que hubo un cambio.

El tiempo se mide con un contador de milisegundos que sube en la interrupcion de TIM4. Todo se compara restando enteros sin signo, asi que sigue funcionando cuando el contador se desborda. No uso delays.

El UART recibe por interrupcion con un buffer circular de 128 bytes, y envia con otro buffer de 512 bytes para no bloquear el programa.

## Comandos

- HELP: muestra los comandos
- STATUS: modo, duty, uptime, contadores y errores
- MODE MANUAL / MODE AUTO: cambia el modo
- DUTY n: pone el duty (0 a 100), solo en MANUAL
- PAUSE / RESUME: pausa y reanuda
- STREAM ON / STREAM OFF: manda datos cada segundo

Las respuestas son OK o ERR COMMAND, ERR ARG, ERR RANGE, ERR STATE o ERR OVERFLOW. Las lineas pueden tener hasta 63 caracteres y se aceptan CR, LF y CRLF.

## Pruebas

No tengo analizador logico, asi que el duty lo medi con un multimetro en DC entre el pin PWM y GND (voltaje = 3.3 V x duty).

| Prueba | Resultado |
|---|---|
| Encendido | MANUAL, 25%, STREAM apagado |
| DUTY 0, 50 y 100 | LED apagado, a medias y fijo encendido |
| Multimetro: 25%, 50%, 75% | cerca de 0.8 V, 1.65 V y 2.5 V |
| AUTO durante 3.2 s | sube a 90%, baja y vuelve a 10% |
| 20 pulsaciones cortas | cortas = 20 en STATUS, sin duplicados |
| Mantener el boton 3 s | una sola larga, al soltar no cuenta corta |
| PAUSA y RESUME desde AUTO | PWM en 0 y despues sigue donde iba |
| DUTY 20abc, -1 y 101 | ERR y el duty no cambia |
| Linea de 100 caracteres y luego STATUS | ERR OVERFLOW y STATUS funciona |
| CR, LF y CRLF | una ejecucion por comando |
| STREAM ON con 10 comandos por segundo por 30 s | sin reinicios, rampa y boton funcionan |
| Desbordamiento del contador de tiempo | todo sigue funcionando al dar la vuelta |

