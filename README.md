# Controle Automático de Luminosidade

Sistema embarcado desenvolvido em **STM32** para controle automático da luminosidade de um LED utilizando um sensor de luz, PWM e um potenciômetro para ajuste do tempo de resposta.

## Funcionamento

O sistema realiza continuamente as seguintes etapas:

1. Inicialização do sistema.
2. Leitura da luminosidade através do sensor **GY-302** via I²C.
3. Leitura do potenciômetro através do ADC.
4. Conversão da luminosidade medida em um valor de PWM.
5. Aplicação gradual do PWM no LED.
6. Atualização dos LEDs indicadores.
7. Espera pelo tempo de resposta configurado.
8. Repetição do processo.

A lógica é implementada através de uma **máquina de estados**, evitando concentrar todo o funcionamento em um único loop.

## Máquina de Estados

Os estados implementados são:

```text
SETUP_STATE
     ↓
READ_SENSOR_STATE
     ↓
READ_POT_STATE
     ↓
CONTROL_STATE
     ↓
APPLY_PWM_STATE
     ↓
UPDATE_INDICATORS_STATE
     ↓
WAIT_STATE
     ↓
READ_SENSOR_STATE
```

Em caso de erro durante a operação, o sistema é direcionado para:

```text
ERROR_STATE
```

Os estados são definidos pelo tipo `StateId` e cada estado possui uma função responsável por executar sua respectiva etapa.

## Sensor de Luminosidade

O sensor **GY-302** é utilizado para medir a luminosidade ambiente através da interface I²C.

O endereço utilizado é:

```c
#define GY302_I2C_ADDR (0x5C << 1)
```

O sensor é inicializado e configurado para realizar a medição. Após o tempo necessário para aquisição, são recebidos dois bytes:

```c
uint8_t rx_data[2] = {0};
```

O valor bruto é reconstruído através dos dois bytes recebidos:

```c
uint16_t raw_val = (rx_data[0] << 8) | rx_data[1];
```

O valor de luminosidade é então calculado e armazenado em `sensor_value`.

## Controle do PWM

O valor de luminosidade é convertido para o valor de comparação do PWM.

O projeto utiliza:

```c
#define ARR 999
```

O cálculo do valor alvo é:

```c
pwm_target = ((uint32_t)sensor_value * ARR) / 54612;
```

O valor é limitado ao intervalo permitido pelo PWM:

```c
if (pwm_target > 999)
    pwm_target = 999;
```

Dessa forma, o valor do sensor determina o nível de PWM aplicado ao LED.

## Aplicação Gradual do PWM

O PWM não é alterado diretamente para o valor alvo. O sistema utiliza uma variação gradual definida por:

```c
#define PWM_CONTROL_STEP 100
```

Quando o PWM atual é menor que o alvo:

```c
pwm_current = pwm_current + PWM_CONTROL_STEP;
```

Quando o PWM atual é maior que o alvo:

```c
pwm_current = pwm_current - PWM_CONTROL_STEP;
```

Quando o valor atual chega próximo do alvo, ele é ajustado diretamente para o valor desejado.

O valor resultante é aplicado ao canal 1 do TIM3:

```c
__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, pwm_current);
```

Isso evita mudanças bruscas na luminosidade do LED.

## Ajuste do Tempo de Resposta

O potenciômetro é utilizado para controlar o tempo de resposta do sistema.

Os valores do ADC são obtidos através de uma média dos valores armazenados no buffer:

```c
uint16_t get_pot_average(uint16_t *vector)
```

O valor médio do potenciômetro é convertido para um intervalo de **20 ms a 4000 ms**:

```c
response_delay_ms =
    20 + ((uint32_t)pot_value * 3980) / 4095;
```

Assim:

| Potenciômetro | Tempo de resposta |
| ------------: | ----------------: |
|             0 |             20 ms |
|          4095 |           4000 ms |

O tempo configurado determina o intervalo entre as novas leituras do sensor.

## LEDs Indicadores

Quatro LEDs indicam o nível atual do PWM.

| LED    | Nível |
| ------ | ----: |
| LED D2 |   25% |
| LED D3 |   50% |
| LED D4 |   75% |
| LED D5 |   90% |

Os limites são definidos por:

```c
#define PWM_25_PCT 250
#define PWM_50_PCT 500
#define PWM_75_PCT 750
#define PWM_90_PCT 900
```

Cada LED é acionado quando o PWM atual atinge seu respectivo nível.

## Tratamento de Erros

Quando ocorre uma falha na comunicação com o sensor, o sistema entra no estado `ERROR_STATE`.

Nesse estado:

* O PWM do LED é desligado.
* Os quatro LEDs indicadores começam a piscar.
* O sistema permanece nesse estado.

O comportamento de indicação utiliza um intervalo de:

```c
#define ERROR_BLINK_DELAY_MS 250
```

## Estrutura da Máquina de Estados

As funções dos estados são organizadas em uma tabela de ponteiros de função:

```c
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
```

A execução da máquina ocorre através da chamada:

```c
currentState = state_table[currentState](&ctx);
```

Cada estado retorna o próximo estado que deverá ser executado.

## Fluxo Geral

```text
              ┌───────────────┐
              │     SETUP     │
              └───────┬───────┘
                      ↓
              ┌───────────────┐
              │ READ SENSOR   │
              └───────┬───────┘
                      ↓
              ┌───────────────┐
              │   READ POT    │
              └───────┬───────┘
                      ↓
              ┌───────────────┐
              │    CONTROL    │
              └───────┬───────┘
                      ↓
              ┌───────────────┐
              │   APPLY PWM   │
              └───────┬───────┘
                      ↓
              ┌───────────────┐
              │  INDICATORS   │
              └───────┬───────┘
                      ↓
              ┌───────────────┐
              │     WAIT      │
              └───────┬───────┘
                      │
                      └──────────→ READ SENSOR

          Em caso de erro
                ↓
        ┌───────────────┐
        │     ERROR     │
        └───────────────┘
```

## Principais Recursos

* Máquina de estados para organização do firmware.
* Leitura de luminosidade via I²C.
* Controle de luminosidade através de PWM.
* Alteração gradual do PWM.
* Potenciômetro para ajuste do tempo de resposta.
* Média das leituras do ADC.
* Quatro LEDs para indicação do nível de PWM.
* Detecção de erro na comunicação com o sensor.
* Indicação visual de erro através dos LEDs.
* Comunicação serial para depuração através de `printf`.
