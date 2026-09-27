#include "ft32f0xx.h"

/* Placeholder heartbeat LED; change to match the board */
#define LED_PORT   GPIOA
#define LED_PIN    GPIO_Pin_4
#define LED_CLK    RCC_AHBPeriph_GPIOA

static volatile uint32_t ticks;

void SysTick_Handler(void)
{
  ticks++;
}

static void delay_ms(uint32_t ms)
{
  uint32_t start = ticks;
  while ((ticks - start) < ms)
    __WFI();
}

int main(void)
{
  GPIO_InitTypeDef gpio;

  SystemCoreClockUpdate();
  SysTick_Config(SystemCoreClock / 1000);

  RCC_AHBPeriphClockCmd(LED_CLK, ENABLE);
  GPIO_StructInit(&gpio);
  gpio.GPIO_Pin = LED_PIN;
  gpio.GPIO_Mode = GPIO_Mode_OUT;
  gpio.GPIO_OType = GPIO_OType_PP;
  gpio.GPIO_Speed = GPIO_Speed_Level_1;
  GPIO_Init(LED_PORT, &gpio);

  while (1)
  {
    LED_PORT->ODR ^= LED_PIN;
    delay_ms(500);
  }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  (void)file;
  (void)line;
  while (1)
  {
  }
}
#endif
