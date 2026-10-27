#include "stm8s.h"
#include "stm8s_gpio.h"
#include "stm8s_clk.h"
#include "stm8s_exti.h"
#include "bsp_delay.h"
#include "bsp_uart.h"
#include "stm8s_it.h"

/* 蓝牙名称：以后只需要修改这里 */
#define BLE_NAME                  "ZAYS-001"
#define BLE_STARTUP_DELAY_MS       ((uint16_t)500)
#define BLE_AT_TIMEOUT_MS         ((uint16_t)500)
#define BLE_RESTART_DELAY_MS      ((uint16_t)1500)
#define BLE_NAME_SET_RETRY_COUNT  ((uint8_t)3)
#define BUTTON_DEBOUNCE_MS        ((uint16_t)20)

typedef enum {
    BLE_BOOT_WAIT,
    BLE_QUERY_WAIT,
    BLE_SET_WAIT,
    BLE_RESTART_WAIT,
    BLE_READY,
    BLE_FAILED
} BLE_State_t;

typedef enum {
    BUTTON_IDLE,
    BUTTON_PRESS_DEBOUNCE,
    BUTTON_PRESSED,
    BUTTON_RELEASE_DEBOUNCE
} Button_State_t;

typedef struct {
    Button_State_t state;
    uint32_t since_ms;
} Button_t;

static BLE_State_t ble_state = BLE_BOOT_WAIT;
static uint32_t ble_state_since_ms = 0;
static uint8_t ble_set_attempts = 0;

/* IAR 调试器中查看：1=名称验证成功，0=失败 */
volatile uint8_t g_ble_name_ok = 0;


/* GPIO 初始化 */
static void GPIO_Init_All(void)
{
    // PD3, PC5, PC6 推挽输出
    GPIO_Init(GPIOD, GPIO_PIN_3, GPIO_MODE_OUT_PP_LOW_FAST);
    GPIO_Init(GPIOC, GPIO_PIN_5, GPIO_MODE_OUT_PP_LOW_FAST);
    GPIO_Init(GPIOC, GPIO_PIN_6, GPIO_MODE_OUT_PP_LOW_FAST);

    // PC3, PC4 上拉输入+中断
    GPIO_Init(GPIOC, GPIO_PIN_3, GPIO_MODE_IN_PU_IT);
    GPIO_Init(GPIOC, GPIO_PIN_4, GPIO_MODE_IN_PU_IT);

    // PORTC 下降沿触发
    EXTI_SetExtIntSensitivity(EXTI_PORT_GPIOC, EXTI_SENSITIVITY_FALL_ONLY);

    // PD3 默认低电平
    GPIO_WriteLow(GPIOD, GPIO_PIN_3);
    GPIO_WriteLow(GPIOC, GPIO_PIN_6);
    GPIO_WriteHigh(GPIOC, GPIO_PIN_5);
}

/* 时钟初始化 */
static void Clock_Init(void)
{
    CLK_HSIPrescalerConfig(CLK_PRESCALER_HSIDIV1); // HSI = 16MHz
}

/* 清空上一条 AT 指令的接收状态 */
static void BLE_RX_Reset(void)
{
    disableInterrupts();
    rx_packet.len = 0;
    rx_packet_ready_flag = 0;
    rx_packet_overflow_flag = 0;
    rx_packet_error_flag = 0;
    enableInterrupts();
}

/* 在蓝牙返回数据中查找指定文本 */
static uint8_t BLE_ResponseContains(const char *text)
{
    uint8_t i;
    uint8_t j;

    for (i = 0; i < rx_packet.len; i++)
    {
        j = 0;
        while ((text[j] != '\0') &&
               ((uint16_t)i + j < rx_packet.len) &&
               (rx_packet.buffer[i + j] == (uint8_t)text[j]))
        {
            j++;
        }

        if (text[j] == '\0')
        {
            return 1;
        }
    }

    return 0;
}

static void BLE_StartQuery(void)
{
    BLE_RX_Reset();
    BSP_UART_SendString("AT+NAME=?");
    ble_state_since_ms = Delay_GetTick();
    ble_state = BLE_QUERY_WAIT;
}

static void BLE_StartSet(void)
{
    BLE_RX_Reset();
    BSP_UART_SendString("AT+NAME=" BLE_NAME);
    ble_set_attempts++;
    ble_state_since_ms = Delay_GetTick();
    ble_state = BLE_SET_WAIT;
}

/* 蓝牙配名按Tick推进，不阻塞SW1；SW2在配置结束后才可发送业务命令。 */
static void BLE_Service(uint32_t now)
{
    if (ble_state == BLE_BOOT_WAIT)
    {
        if ((uint32_t)(now - ble_state_since_ms) >= BLE_STARTUP_DELAY_MS)
        {
            BLE_StartQuery();
        }
        return;
    }
    if (ble_state == BLE_QUERY_WAIT)
    {
        if (rx_packet_ready_flag != 0U)
        {
            if ((rx_packet_overflow_flag == 0U) &&
                (rx_packet_error_flag == 0U) &&
                (BLE_ResponseContains("OK+NAME=" BLE_NAME) != 0U))
            {
                g_ble_name_ok = 1U;
                BLE_RX_Reset();
                ble_state = BLE_READY;
                return;
            }
            /* 忽略无关或不完整的应答，继续等待目标名称直至超时。 */
            BLE_RX_Reset();
        }
        if ((uint32_t)(now - ble_state_since_ms) >= BLE_AT_TIMEOUT_MS)
        {
            if (ble_set_attempts < BLE_NAME_SET_RETRY_COUNT)
            {
                BLE_StartSet();
            }
            else
            {
                ble_state = BLE_FAILED;
            }
        }
        return;
    }
    if (ble_state == BLE_SET_WAIT)
    {
        if ((uint32_t)(now - ble_state_since_ms) >= BLE_AT_TIMEOUT_MS)
        {
            BLE_RX_Reset();
            ble_state_since_ms = now;
            ble_state = BLE_RESTART_WAIT;
        }
        return;
    }
    if (ble_state == BLE_RESTART_WAIT)
    {
        if ((uint32_t)(now - ble_state_since_ms) >= BLE_RESTART_DELAY_MS)
        {
            BLE_StartQuery();
        }
        return;
    }
    /* 业务阶段尚无蓝牙入站命令；避免旧AT响应长期占据接收缓冲。 */
    if (rx_packet_ready_flag != 0U)
    {
        BLE_RX_Reset();
    }
}

/* 返回1=确认按下，2=确认松开；两个方向都做20ms非阻塞消抖。 */
static uint8_t Button_Service(Button_t *button, volatile uint8_t *press_flag,
                              uint8_t level_low, uint32_t now)
{
    uint8_t edge = *press_flag;
    *press_flag = 0U;

    if (button->state == BUTTON_IDLE)
    {
        if ((edge != 0U) && (level_low != 0U))
        {
            button->state = BUTTON_PRESS_DEBOUNCE;
            button->since_ms = now;
        }
    }
    else if (button->state == BUTTON_PRESS_DEBOUNCE)
    {
        if (level_low == 0U)
        {
            button->state = BUTTON_IDLE;
        }
        else if ((uint32_t)(now - button->since_ms) >= BUTTON_DEBOUNCE_MS)
        {
            button->state = BUTTON_PRESSED;
            return 1U;
        }
    }
    else if (button->state == BUTTON_PRESSED)
    {
        if (level_low == 0U)
        {
            button->state = BUTTON_RELEASE_DEBOUNCE;
            button->since_ms = now;
        }
    }
    else
    {
        if (level_low != 0U)
        {
            button->state = BUTTON_PRESSED;
        }
        else if ((uint32_t)(now - button->since_ms) >= BUTTON_DEBOUNCE_MS)
        {
            button->state = BUTTON_IDLE;
            return 2U;
        }
    }
    return 0U;
}

/* 主函数 */
void main(void)
{
    static const uint8_t ble_packet[] = {
        0xFF, 0x55, 0x12, 0x34, 0x50, 0x00, 0x01, 0x00, 0x00
    };
    Button_t sw1 = {BUTTON_IDLE, 0U};
    Button_t sw2 = {BUTTON_IDLE, 0U};
    uint32_t now;
    uint8_t sw1_event;
    uint8_t sw2_event;
    
    Clock_Init();      // 1. 时钟
    GPIO_Init_All();   // 2. GPIO
    Delay_Init();      // 3. TIM4 (1ms Tick)
    BSP_UART_Init();   // 4. UART1 (9600 8N1)
    enableInterrupts(); // 5. 开启全局中断

    g_ble_name_ok = 0U;
    ble_state_since_ms = Delay_GetTick();

    while (1)
    {
        now = Delay_GetTick();
        BLE_Service(now);

        sw1_event = Button_Service(&sw1, &sw1_press_flag,
                                   GPIO_ReadInputPin(GPIOC, GPIO_PIN_3) == RESET,
                                   now);
        if (sw1_event == 1U)
        {
            GPIO_WriteHigh(GPIOD, GPIO_PIN_3);
        }
        else if (sw1_event == 2U)
        {
            GPIO_WriteLow(GPIOD, GPIO_PIN_3);
        }

        sw2_event = Button_Service(&sw2, &sw2_press_flag,
                                   GPIO_ReadInputPin(GPIOC, GPIO_PIN_4) == RESET,
                                   now);
        if ((sw2_event == 1U) &&
            ((ble_state == BLE_READY) || (ble_state == BLE_FAILED)))
        {
            BSP_UART_SendBuffer(ble_packet, (uint8_t)sizeof(ble_packet));
        }

        /* TIM4或UART/按键中断会唤醒，避免主循环持续空转。 */
        wfi();
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t* file, uint32_t line)
{
    (void)file; (void)line;
    while (1) {}
}
#endif
