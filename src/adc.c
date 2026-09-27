#include "adc.h"

#include "board.h"

#define BUF_LEN  512

enum { ST_START_SOUND, ST_SOUND, ST_START_LIGHT, ST_LIGHT };

static uint16_t buf[BUF_LEN];
static uint8_t state;
static adc_cb_t on_sound, on_light;

static void stop(void)
{
  TIM15->CR1 = 0;
  if (ADC1->CR & ADC_CR_ADSTART)
  {
    ADC1->CR |= ADC_CR_ADSTP;
    while (ADC1->CR & ADC_CR_ADSTP)
    {
    }
  }
  DMA1_Channel1->CCR &= ~DMA_CCR_EN;
}

static void start(uint32_t channel)
{
  ADC1->CHSELR = 1u << channel;
  DMA1->IFCR = DMA_IFCR_CGIF1;
  DMA1_Channel1->CNDTR = BUF_LEN;
  DMA1_Channel1->CCR |= DMA_CCR_EN;
  ADC1->CR |= ADC_CR_ADSTART;   /* waits for TIM15 TRGO */
  TIM15->CNT = 0;
  TIM15->CR1 = TIM_CR1_CEN;
}

void adc_init(void)
{
  RCC->AHBENR |= RCC_AHBENR_GPIOAEN | RCC_AHBENR_DMA1EN;
  RCC->APB2ENR |= RCC_APB2ENR_ADC1EN | RCC_APB2ENR_TIM15EN;

  gpio_mode(GPIOA, 1, GPIO_MODE_ANALOG);
  gpio_mode(GPIOA, 6, GPIO_MODE_ANALOG);

  /* TIM15: 8 kHz update event as TRGO */
  TIM15->PSC = 47;
  TIM15->ARR = 124;
  TIM15->CR2 = TIM_CR2_MMS_1;
  TIM15->EGR = TIM_EGR_UG;

  /* ADC clock PCLK/4 = 12 MHz (the F0 ADC is limited to 14 MHz) */
  ADC1->CFGR2 = ADC_CFGR2_CKMODE_1;

  ADC1->CR |= ADC_CR_ADCAL;
  while (ADC1->CR & ADC_CR_ADCAL)
  {
  }

  /* 10-bit, right aligned, EXTSEL = TIM15_TRGO, rising, DISCEN, circular DMA */
  ADC1->CFGR1 = 0x0001050B;
  ADC1->SMPR = 0;
  ADC->CCR |= ADC_CCR_VREFEN;

  ADC1->ISR = ADC_ISR_ADRDY;
  ADC1->CR |= ADC_CR_ADEN;
  while (!(ADC1->ISR & ADC_ISR_ADRDY))
  {
  }

  /* DMA1 CH1: ADC_DR -> buf, 16-bit, MINC, circular */
  DMA1_Channel1->CPAR = (uint32_t)&ADC1->DR;
  DMA1_Channel1->CMAR = (uint32_t)buf;
  DMA1_Channel1->CCR = DMA_CCR_MINC | DMA_CCR_CIRC | DMA_CCR_MSIZE_0 | DMA_CCR_PSIZE_0 |
                       DMA_CCR_PL_1;

  state = ST_START_SOUND;
}

void adc_set_callbacks(adc_cb_t sound_cb, adc_cb_t light_cb)
{
  on_sound = sound_cb;
  on_light = light_cb;
}

void adc_sampler_poll(void)
{
  switch (state)
  {
  case ST_START_SOUND:
    start(MIC_ADC_CH);
    state = ST_SOUND;
    break;

  case ST_SOUND:
    /* 512 samples at 8 kHz = 64 ms */
    if (DMA1->ISR & DMA_ISR_TCIF1)
    {
      stop();
      if (on_sound)
        on_sound(buf, BUF_LEN);
      state = ST_START_LIGHT;
    }
    break;

  case ST_START_LIGHT:
    start(LIGHT_ADC_CH);
    state = ST_LIGHT;
    break;

  default:
  {
    uint16_t n = (uint16_t)(BUF_LEN - DMA1_Channel1->CNDTR);
    if (n >= 3)
    {
      stop();
      if (on_light)
        on_light(buf, n);
      state = ST_START_SOUND;
    }
    break;
  }
  }
}
