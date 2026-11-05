/* Custom PCB U13 hardware: PC2 DFSDM1_CKOUT -> MP34DT05 CLK;
 * PB14 DFSDM1_DATIN2 <- MP34DT05 DAT; PB6/PB7 I2C1 -> MPR121;
 * PA0/PA1 PEC11R A/B, encoder push contact is NOT on MCU.
 * DMA1_CH4 handles DFSDM1_Filter0; CH6 and CH7 remain SAI2A/Speaker.
 * MPR121 ~IRQ and ADDR are unconnected in source schematic, so poll and
 * require I2C ACK; a floating address cannot be fixed in firmware. */
/* R7: These peripheral drivers are compiled into this translation unit.
 * Select the modules BEFORE any STM32 HAL header is included; otherwise an
 * older CubeMX-generated hal_conf.h can silently omit their implementations.
 * This setting is local to the G1 board implementation, not a global override. */
#ifndef HAL_I2C_MODULE_ENABLED
#define HAL_I2C_MODULE_ENABLED
#endif
#ifndef HAL_DFSDM_MODULE_ENABLED
#define HAL_DFSDM_MODULE_ENABLED
#endif
#include "g1_product_board.h"
#include "group_encoder.h"
/* The full-system touch controller requires I2C1. Explicitly include the
 * peripheral API, not just stm32l4xx_hal.h, because CubeMX-generated HAL
 * configuration may omit the module include. Keep HAL_I2C_MODULE_ENABLED
 * selected in stm32l4xx_hal_conf.h and compile both HAL I2C source files. */
#include "stm32l4xx_hal_i2c.h"
#include "stm32l4xx_hal_i2c_ex.h"
#include "stm32l4xx_hal_dfsdm.h"
#include <string.h>

G1BoardDiagnostics g_g1_board;
static I2C_HandleTypeDef hi2c1_product;
static DFSDM_Channel_HandleTypeDef hmic_channel;
static DFSDM_Filter_HandleTypeDef hmic_filter;
static DMA_HandleTypeDef hdma_mic;
static int32_t s_pdm_dma[G1_MIC_FRAMES*2U];
#define G1_MIC_DEPTH 8U
static int16_t s_mic_queue[G1_MIC_DEPTH][G1_MIC_FRAMES];
static volatile uint8_t s_head, s_tail;
static uint8_t s_hardware_ready, s_touch_ready;
/* 2 consecutive 15ms observations to press/release; per-pad retrigger guard.
 * Proximity bit 12 is read separately and cannot press an electrode. */
static uint8_t s_touch_integrator[12];
static uint16_t s_touch_latched;
static uint32_t s_touch_retrigger_ms[12];
static uint32_t s_last_touch_ms, s_next_probe_ms;
static GroupEncoder s_encoder;
static volatile int16_t s_encoder_pending;
static volatile uint32_t s_encoder_last_irq_ms;
static uint8_t s_encoder_last_polled_phase;
static uint8_t s_i2c_address = G1_TOUCH_ADDR_7BIT;
static volatile uint8_t s_touch_reinit_requested;
static int32_t s_mic_hp_state;
static int16_t s_mic_hp_prev_input;
/* Distinguish a successful source selection from real hardware readiness. */
static volatile uint32_t s_board_init_count;

/* IRQ decoding is essential: a 2 ms RTOS poll can skip all four detent edges. */
void G1_EncoderEdgeIRQ(void)
{
  int step=GroupEncoder_Update(&s_encoder,
    HAL_GPIO_ReadPin(GPIOA,GPIO_PIN_0)==GPIO_PIN_SET,
    HAL_GPIO_ReadPin(GPIOA,GPIO_PIN_1)==GPIO_PIN_SET);
  ++g_g1_board.encoder_irq_edges;
  s_encoder_last_irq_ms=HAL_GetTick();
  if(step!=0) {
    if(step>0 && s_encoder_pending<127)++s_encoder_pending;
    if(step<0 && s_encoder_pending>(-127))--s_encoder_pending;
    ++g_g1_board.encoder_detents;
  }
}


/* R17: independent GPIO-I2C validation and fallback, following Anna's MPR121
 * register sequence. This is only selected if the STM32 HAL probe cannot
 * obtain an ACK. Both paths use the SAME PCB nets PB6/PB7 and 7-bit address;
 * software cannot cure an unpowered chip or a floating physical ADDR pin. */
static uint8_t s_touch_sw_bus;
static uint8_t s_touch_sw_address = G1_TOUCH_ADDR_7BIT;
static uint8_t s_touch_sw_last_mask;
static uint8_t s_touch_hal_last_status;
static void touch_bb_wait(void)
{
  /* Conservative sub-100 kHz GPIO I2C. An always-terminating delay avoids
   * depending on DWT or the scheduler, including during boot. */
  volatile uint32_t cycles=SystemCoreClock/400000U + 1U;
  while(cycles--) { __NOP(); }
}
static void touch_bb_sda(uint8_t high)
{
  HAL_GPIO_WritePin(GPIOB,GPIO_PIN_7,high?GPIO_PIN_SET:GPIO_PIN_RESET);
  touch_bb_wait();
}
static uint8_t touch_bb_scl(uint8_t high)
{
  HAL_GPIO_WritePin(GPIOB,GPIO_PIN_6,high?GPIO_PIN_SET:GPIO_PIN_RESET);
  touch_bb_wait();
  if(high){
    /* Observe clock stretching or shorts; bounded to avoid a hang. */
    for(uint32_t i=0U;i<500U;++i){
      if(HAL_GPIO_ReadPin(GPIOB,GPIO_PIN_6)==GPIO_PIN_SET){
        ++g_g1_board.touch_soft_clock_rises;return 1U;
      }
      touch_bb_wait();
    }
    ++g_g1_board.touch_soft_clock_failures;return 0U;
  }
  return 1U;
}
static void touch_bb_pins(void)
{
  GPIO_InitTypeDef cfg={0};
  /* HAL hardware I2C must release both pads before switching mode. */
  __HAL_I2C_DISABLE(&hi2c1_product);
  HAL_GPIO_WritePin(GPIOB,GPIO_PIN_6|GPIO_PIN_7,GPIO_PIN_SET);
  cfg.Pin=GPIO_PIN_6|GPIO_PIN_7;
  cfg.Mode=GPIO_MODE_OUTPUT_OD;
  cfg.Pull=GPIO_PULLUP;
  cfg.Speed=GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB,&cfg);
  touch_bb_wait();
}
static void touch_bb_restore_hal(void)
{
  GPIO_InitTypeDef cfg={0};
  HAL_GPIO_WritePin(GPIOB,GPIO_PIN_6|GPIO_PIN_7,GPIO_PIN_SET);
  cfg.Pin=GPIO_PIN_6|GPIO_PIN_7;
  cfg.Mode=GPIO_MODE_AF_OD;
  cfg.Pull=GPIO_NOPULL;
  cfg.Speed=GPIO_SPEED_FREQ_VERY_HIGH;
  cfg.Alternate=GPIO_AF4_I2C1;
  HAL_GPIO_Init(GPIOB,&cfg);
  __HAL_I2C_ENABLE(&hi2c1_product);
}
static uint8_t touch_bb_start(void)
{
  touch_bb_sda(1U);
  if(!touch_bb_scl(1U))return 0U;
  if(HAL_GPIO_ReadPin(GPIOB,GPIO_PIN_7)!=GPIO_PIN_SET)return 0U;
  touch_bb_sda(0U);
  return touch_bb_scl(0U);
}
static void touch_bb_stop(void)
{
  (void)touch_bb_scl(0U);
  touch_bb_sda(0U);
  (void)touch_bb_scl(1U);
  touch_bb_sda(1U);
}
static uint8_t touch_bb_tx(uint8_t value)
{
  for(int bit=7;bit>=0;--bit){
    (void)touch_bb_scl(0U);
    touch_bb_sda((value&(1U<<bit))!=0U);
    if(!touch_bb_scl(1U))return 0U;
  }
  (void)touch_bb_scl(0U);
  touch_bb_sda(1U); /* release SDA for slave ACK */
  if(!touch_bb_scl(1U))return 0U;
  const uint8_t ack=(HAL_GPIO_ReadPin(GPIOB,GPIO_PIN_7)==GPIO_PIN_RESET);
  (void)touch_bb_scl(0U);
  return ack;
}
static uint8_t touch_bb_rx(uint8_t ack)
{
  uint8_t value=0U;
  touch_bb_sda(1U);
  for(unsigned bit=0U;bit<8U;++bit){
    (void)touch_bb_scl(0U);
    (void)touch_bb_scl(1U);
    value=(uint8_t)((value<<1U)|
      (HAL_GPIO_ReadPin(GPIOB,GPIO_PIN_7)==GPIO_PIN_SET?1U:0U));
  }
  (void)touch_bb_scl(0U);
  touch_bb_sda(ack?0U:1U);
  (void)touch_bb_scl(1U);
  (void)touch_bb_scl(0U);
  touch_bb_sda(1U);
  return value;
}
static uint8_t touch_bb_probe(uint8_t addr)
{
  if(!touch_bb_start()){touch_bb_stop();return 0U;}
  uint8_t ack=touch_bb_tx((uint8_t)(addr<<1U));
  touch_bb_stop();
  return ack;
}
static uint8_t touch_bb_scan(void)
{
  uint8_t mask=0U;
  touch_bb_pins();
  /* MPR121 supports 0x5A, 0x5B, 0x5C, 0x5D depending on ADDR strap. */
  for(uint8_t i=0U;i<4U;++i){
    if(touch_bb_probe((uint8_t)(0x5AU+i)))mask|=(uint8_t)(1U<<i);
  }
  s_touch_sw_last_mask=mask;
  g_g1_board.touch_soft_scan_mask=mask;
  ++g_g1_board.touch_soft_scans;
  if(!mask){touch_bb_restore_hal();return 0U;}
  /* Select the first electrically ACKing address. A non-0x5A result is
   * useful for diagnosis but requires a stable physical address strap. */
  uint8_t addr=0x5AU;
  for(uint8_t i=0U;i<4U;++i)if(mask&(1U<<i)){addr=(uint8_t)(0x5AU+i);break;}
  s_touch_sw_address=addr;
  s_i2c_address=addr;
  g_g1_board.touch_address=addr;
  s_touch_sw_bus=1U;
  g_g1_board.touch_transport=1U;
  return 1U;
}
static uint8_t touch_bb_write(uint8_t reg,uint8_t data)
{
  if(!touch_bb_start()){touch_bb_stop();return 0U;}
  uint8_t ok=touch_bb_tx((uint8_t)(s_touch_sw_address<<1U));
  if(ok)ok=touch_bb_tx(reg);
  if(ok)ok=touch_bb_tx(data);
  touch_bb_stop();
  return ok;
}
static uint8_t touch_bb_read(uint8_t reg,uint8_t *data,uint16_t count)
{
  if(count==0U||data==NULL)return 0U;
  if(!touch_bb_start()){touch_bb_stop();return 0U;}
  uint8_t ok=touch_bb_tx((uint8_t)(s_touch_sw_address<<1U));
  if(ok)ok=touch_bb_tx(reg);
  if(ok){
    if(!touch_bb_start())ok=0U;
    else ok=touch_bb_tx((uint8_t)((s_touch_sw_address<<1U)|1U));
  }
  if(ok){for(uint16_t i=0U;i<count;++i)
    data[i]=touch_bb_rx((uint8_t)(i+1U<count));}
  touch_bb_stop();
  return ok;
}
static uint8_t touch_write(uint8_t reg, uint8_t value)
{
  if(s_touch_sw_bus)return touch_bb_write(reg,value);
  return HAL_I2C_Mem_Write(&hi2c1_product,(uint16_t)(s_i2c_address<<1),reg,
           I2C_MEMADD_SIZE_8BIT,&value,1U,10U)==HAL_OK;
}
static uint8_t touch_read(uint8_t reg, uint8_t *data, uint16_t n)
{
  if(s_touch_sw_bus)return touch_bb_read(reg,data,n);
  return HAL_I2C_Mem_Read(&hi2c1_product,(uint16_t)(s_i2c_address<<1),reg,
           I2C_MEMADD_SIZE_8BIT,data,n,10U)==HAL_OK;
}
/* Failed touch initialisation must NOT cause immediate retry on every RTOS
 * control-loop pass: I2C timeouts would otherwise disturb audio responsiveness.
 * Keep polling as the PCB does not route MPR121 IRQ to the MCU. */
static void touch_error_snapshot(void)
{
  g_g1_board.touch_hal_error = HAL_I2C_GetError(&hi2c1_product);
  g_g1_board.touch_hal_state = (uint32_t)HAL_I2C_GetState(&hi2c1_product);
  g_g1_board.touch_isr = I2C1->ISR;
}
static void touch_fail(uint32_t now)
{
  touch_error_snapshot();
  ++g_g1_board.touch_errors;
  g_g1_board.touch_present=0U;
  s_touch_ready=0U;
  s_next_probe_ms=now+2000U;
}
/* R16: Anna's 0x5A device configuration. The 13th status bit is
 * proximity; physical E8..E11 are the four installed product buttons.
 * Only an actual I2C ACK and readback of ECR 0xFC mark the chip ready. */
static void touch_init(uint32_t now)
{
  uint8_t state[2]={0U,0U};
  uint8_t ecr=0U;
  s_touch_ready=0U;
  g_g1_board.touch_present=0U;
  g_g1_board.touch_stage=1U;
  if(!s_touch_sw_bus)s_i2c_address=G1_TOUCH_ADDR_7BIT;
  g_g1_board.touch_address=s_i2c_address;
  ++g_g1_board.touch_probe_attempts;
  /* First try Anna's normal STM32 hardware I2C at 0x5A. If the HAL cannot
   * obtain ACK, use independent GPIO clock/data generation to discriminate
   * a peripheral setup issue from a non-responding board. */
  if(!s_touch_sw_bus){
    HAL_StatusTypeDef result=HAL_I2C_IsDeviceReady(&hi2c1_product,
        (uint16_t)(G1_TOUCH_ADDR_7BIT<<1U),3U,20U);
    s_touch_hal_last_status=(uint8_t)result;
    g_g1_board.touch_last_probe_status=(uint32_t)result;
    if(result!=HAL_OK && !touch_bb_scan()){touch_fail(now);return;}
  }else if(!touch_bb_probe(s_touch_sw_address)){
    touch_fail(now);return;
  }
  ++g_g1_board.touch_acks;
  g_g1_board.touch_stage=2U;
  /* Anna: stop, soft reset, settle, then configure. */
  if (!touch_write(0x5EU,0U)){touch_fail(now);return;}
  if (!touch_write(0x80U,0x63U)){touch_fail(now);return;}
  HAL_Delay(10U);
  static const uint8_t anna_regs[][2]={
    {0x36U,0x0FU},{0x37U,0x0FU},{0x38U,0x00U},{0x39U,0x00U},
    {0x3AU,0x01U},{0x3BU,0x01U},{0x3CU,0xFFU},{0x3DU,0xFFU},
    {0x59U,0x07U},{0x5AU,0x03U},{0x5BU,0x22U},
    {0x5CU,0x10U},{0x5DU,0x24U},
    {0x7BU,0x0BU},{0x7DU,0xC9U},{0x7EU,0x82U},{0x7FU,0xB5U}
  };
  g_g1_board.touch_stage=3U;
  for(unsigned i=0U;i<sizeof(anna_regs)/sizeof(anna_regs[0]);++i)
    if(!touch_write(anna_regs[i][0],anna_regs[i][1])){touch_fail(now);return;}
  for(uint8_t e=0U;e<12U;++e){
    /* Anna baseline is 60/30. Lower the four actual product buttons to
     * 42/20 for improved gloved/weak-contact response. Other pads retain
     * the proven Anna values; two-poll debounce handles induced noise. */
    uint8_t press=(e>=8U)?42U:60U;
    uint8_t release=(e>=8U)?20U:30U;
    if(!touch_write((uint8_t)(0x41U+2U*e),press) ||
       !touch_write((uint8_t)(0x42U+2U*e),release)) {touch_fail(now);return;}
  }
  /* This is 0xFC, NOT the old 0x8C: ELEPROX_EN=3 enables proximity
   * electrode bit 12, ELE_EN=12 enables the twelve physical inputs. */
  g_g1_board.touch_stage=4U;
  if(!touch_write(0x5EU,0xFCU) || !touch_read(0x5EU,&ecr,1U) ||
     !touch_read(0x00U,state,2U)){touch_fail(now);return;}
  g_g1_board.touch_ecr=ecr;
  if(ecr!=0xFCU){touch_fail(now);return;}
  /* Start with no key pressed; do not report an old held key as a new tap. */
  memset(s_touch_integrator,0,sizeof(s_touch_integrator));
  s_touch_latched=0U;
  uint16_t first=(uint16_t)((uint16_t)state[0]|((uint16_t)state[1]<<8));
  g_g1_board.touch_raw=(uint32_t)(first&0x0FFFU);
  g_g1_board.touch_proximity=(uint32_t)((first>>12)&1U);
  s_touch_ready=1U;
  g_g1_board.touch_present=1U;
  g_g1_board.touch_stage=5U;
  g_g1_board.touch_hal_error=0U;
  g_g1_board.touch_hal_state=(uint32_t)HAL_I2C_GetState(&hi2c1_product);
}
static void touch_i2c_recover(uint32_t now)
{
  /* The encoder/DFSDM/speaker are independent: reset I2C1 only. */
  if(s_touch_sw_bus){
    touch_bb_restore_hal();s_touch_sw_bus=0U;
    g_g1_board.touch_transport=0U;
  }
  (void)HAL_I2C_DeInit(&hi2c1_product);
  __HAL_RCC_I2C1_FORCE_RESET();
  __HAL_RCC_I2C1_RELEASE_RESET();
  __HAL_RCC_I2C1_CLK_ENABLE();
  hi2c1_product.State=HAL_I2C_STATE_RESET;
  if(HAL_I2C_Init(&hi2c1_product)!=HAL_OK){
    g_g1_board.init_error_bits|=1U;touch_fail(now);return;
  }
  if(HAL_I2CEx_ConfigAnalogFilter(&hi2c1_product,I2C_ANALOGFILTER_ENABLE)!=HAL_OK ||
     HAL_I2CEx_ConfigDigitalFilter(&hi2c1_product,0U)!=HAL_OK){
    g_g1_board.init_error_bits|=1U;touch_fail(now);return;
  }
  g_g1_board.init_error_bits &= ~1U;
  touch_init(now);
}

/* Caller: after HAL_Init/SystemClock_Config and before scheduler. */
void G1_BoardInit(void)
{
  GPIO_InitTypeDef p={0};
  if(s_board_init_count!=0U)return; /* one-time hardware setup */
  s_board_init_count++;
  memset(&g_g1_board,0,sizeof(g_g1_board));
  __HAL_RCC_GPIOA_CLK_ENABLE();__HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();__HAL_RCC_I2C1_CLK_ENABLE();
  __HAL_RCC_DMA1_CLK_ENABLE();__HAL_RCC_DFSDM1_CLK_ENABLE();
  /* PA0/PA1 are encoder GPIO. TIM2 counts internal cycles only; never
   * configure TIM2 encoder mode, which would break RTOS profiling. */
  p.Pin=GPIO_PIN_0|GPIO_PIN_1;p.Mode=GPIO_MODE_IT_RISING_FALLING;
  p.Pull=GPIO_PULLUP;p.Speed=GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA,&p);
  GroupEncoder_Init(&s_encoder,
    HAL_GPIO_ReadPin(GPIOA,GPIO_PIN_0)==GPIO_PIN_SET,
    HAL_GPIO_ReadPin(GPIOA,GPIO_PIN_1)==GPIO_PIN_SET);
  /* Anna individual demo uses 2 valid Gray transitions per step. */
  s_encoder.transitions_per_step=4U; /* full Gray-code cycle = one reliable detent */

  s_encoder_last_polled_phase=(uint8_t)(
    ((HAL_GPIO_ReadPin(GPIOA,GPIO_PIN_0)==GPIO_PIN_SET)?2U:0U)|
    ((HAL_GPIO_ReadPin(GPIOA,GPIO_PIN_1)==GPIO_PIN_SET)?1U:0U));
  /* Both EXTI lines run below FreeRTOS max-syscall IRQ priority and do not
   * invoke RTOS APIs. Quadrature edges are never sampled by the slow UI task. */
  HAL_NVIC_SetPriority(EXTI0_IRQn,7U,0U);
  HAL_NVIC_SetPriority(EXTI1_IRQn,7U,0U);
  HAL_NVIC_ClearPendingIRQ(EXTI0_IRQn);
  HAL_NVIC_ClearPendingIRQ(EXTI1_IRQn);
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);
  HAL_NVIC_EnableIRQ(EXTI1_IRQn);
  /* The microphone must be attempted before the I2C touch bus. A stuck
   * MPR121 SCL/SDA must never prevent bringing up the independent DFSDM. */
  (void)G1_MicReinit();
  /* Match Anna's demonstrated PB6/PB7 AF4 and peripheral clock source. */
  RCC_PeriphCLKInitTypeDef touch_clk={0};
  touch_clk.PeriphClockSelection=RCC_PERIPHCLK_I2C1;
  touch_clk.I2c1ClockSelection=RCC_I2C1CLKSOURCE_PCLK1;
  if(HAL_RCCEx_PeriphCLKConfig(&touch_clk)!=HAL_OK)
    g_g1_board.init_error_bits|=1U;
  p.Pin=GPIO_PIN_6|GPIO_PIN_7;p.Mode=GPIO_MODE_AF_OD;
  p.Pull=GPIO_NOPULL;p.Speed=GPIO_SPEED_FREQ_VERY_HIGH;
  p.Alternate=GPIO_AF4_I2C1;
  HAL_GPIO_Init(GPIOB,&p);
  hi2c1_product.Instance=I2C1;
  /* Anna's known demo timing for PCLK1 at 48MHz. */
  hi2c1_product.Init.Timing=0x10D19CE4U;
  hi2c1_product.Init.OwnAddress1=0U;
  hi2c1_product.Init.AddressingMode=I2C_ADDRESSINGMODE_7BIT;
  hi2c1_product.Init.DualAddressMode=I2C_DUALADDRESS_DISABLE;
  hi2c1_product.Init.OwnAddress2=0U;
  hi2c1_product.Init.OwnAddress2Masks=I2C_OA2_NOMASK;
  hi2c1_product.Init.GeneralCallMode=I2C_GENERALCALL_DISABLE;
  hi2c1_product.Init.NoStretchMode=I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1_product)!=HAL_OK) {
    g_g1_board.init_error_bits |= 1U;touch_fail(HAL_GetTick());
  }else if(HAL_I2CEx_ConfigAnalogFilter(&hi2c1_product,I2C_ANALOGFILTER_ENABLE)!=HAL_OK ||
           HAL_I2CEx_ConfigDigitalFilter(&hi2c1_product,0U)!=HAL_OK) {
    g_g1_board.init_error_bits |= 1U;touch_fail(HAL_GetTick());
  }else {
    /* Anna demo waits 100ms before the first MPR121 transaction. */
    HAL_Delay(100U);
    touch_init(HAL_GetTick());
  }


}

/* Microphone setup is independent of I2C/touch and may be explicitly retried
 * after boot. No success is asserted until the DFSDM channel, filter and
 * DMA have all returned HAL_OK. Calling this while capture is active is rejected. */
HAL_StatusTypeDef G1_MicReinit(void)
{
  GPIO_InitTypeDef p={0};
  if(g_g1_board.mic_running)return HAL_BUSY;
  ++g_g1_board.mic_init_attempts;
  g_g1_board.mic_init_stage=1U;
  g_g1_board.mic_last_hal_status=HAL_ERROR;
  s_hardware_ready=0U;g_g1_board.mic_ready=0U;
  g_g1_board.init_error_bits &= ~30U;
  /* HAL's DFSDM driver internally retains registered channel handles.  A
   * partial channel/filter init cannot be retried until both are deinit'ed.
   * R11 repeatedly re-initialised handles, leading to sticky retry failures.
   * Always release the filter BEFORE the channel, then reset both handles. */
  if (hmic_filter.State != HAL_DFSDM_FILTER_STATE_RESET) {
    (void)HAL_DFSDM_FilterDeInit(&hmic_filter);
  }
  if (hmic_channel.State != HAL_DFSDM_CHANNEL_STATE_RESET) {
    (void)HAL_DFSDM_ChannelDeInit(&hmic_channel);
  }
  __HAL_DFSDM_FILTER_RESET_HANDLE_STATE(&hmic_filter);
  __HAL_DFSDM_CHANNEL_RESET_HANDLE_STATE(&hmic_channel);
  s_mic_hp_state=0;s_mic_hp_prev_input=0;

  __HAL_RCC_GPIOB_CLK_ENABLE();__HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_DMA1_CLK_ENABLE();__HAL_RCC_DFSDM1_CLK_ENABLE();
  /* Explicit DFSDM clock mux: May 25 microphone prototype uses PCLK. */
  RCC_PeriphCLKInitTypeDef mic_clk={0};
  mic_clk.PeriphClockSelection=RCC_PERIPHCLK_DFSDM1;
  mic_clk.Dfsdm1ClockSelection=RCC_DFSDM1CLKSOURCE_PCLK;
  if(HAL_RCCEx_PeriphCLKConfig(&mic_clk)!=HAL_OK){
    g_g1_board.init_error_bits|=0x10U;
    g_g1_board.mic_last_hal_status=HAL_ERROR;
    return HAL_ERROR;
  }
  /* PDM DFSDM at 48k: 48MHz SYSCLK / 25 / 40 decimation = 48kHz;
   * PDM microphone clock = 1.92MHz. Requires physical PCB data/clock verification. */
  p.Pin=GPIO_PIN_2;p.Mode=GPIO_MODE_AF_PP;p.Pull=GPIO_NOPULL;
  p.Speed=GPIO_SPEED_FREQ_HIGH;p.Alternate=GPIO_AF6_DFSDM1;
  HAL_GPIO_Init(GPIOC,&p);
  p.Pin=GPIO_PIN_14;p.Mode=GPIO_MODE_AF_PP;p.Pull=GPIO_NOPULL;
  p.Speed=GPIO_SPEED_FREQ_HIGH;p.Alternate=GPIO_AF6_DFSDM1;
  HAL_GPIO_Init(GPIOB,&p);
  g_g1_board.mic_init_stage=2U;
  hmic_channel.Instance=DFSDM1_Channel2;
  hmic_channel.Init.OutputClock.Activation=ENABLE; /* Drive PDM clock on PC2. */
  hmic_channel.Init.OutputClock.Selection=DFSDM_CHANNEL_OUTPUT_CLOCK_SYSTEM;
  hmic_channel.Init.OutputClock.Divider=25U;
  hmic_channel.Init.Input.Multiplexer=DFSDM_CHANNEL_EXTERNAL_INPUTS;
  hmic_channel.Init.Input.DataPacking=DFSDM_CHANNEL_STANDARD_MODE;
  hmic_channel.Init.Input.Pins=DFSDM_CHANNEL_SAME_CHANNEL_PINS;
  hmic_channel.Init.SerialInterface.Type=DFSDM_CHANNEL_SPI_RISING;
  hmic_channel.Init.SerialInterface.SpiClock=DFSDM_CHANNEL_SPI_CLOCK_INTERNAL;
  hmic_channel.Init.Awd.FilterOrder=DFSDM_CHANNEL_FASTSINC_ORDER;
  hmic_channel.Init.Awd.Oversampling=1U;
  hmic_channel.Init.Offset=0;
  hmic_channel.Init.RightBitShift=0U;
  HAL_StatusTypeDef channel_status=HAL_DFSDM_ChannelInit(&hmic_channel);
  if (channel_status!=HAL_OK) {
    g_g1_board.init_error_bits|=2U;g_g1_board.mic_last_hal_status=HAL_ERROR;return HAL_ERROR;
  }
  g_g1_board.mic_init_stage=3U;
  hmic_filter.Instance=DFSDM1_Filter0;
  hmic_filter.Init.RegularParam.Trigger=DFSDM_FILTER_SW_TRIGGER;
  hmic_filter.Init.RegularParam.FastMode=ENABLE;
  hmic_filter.Init.RegularParam.DmaMode=ENABLE;
  hmic_filter.Init.InjectedParam.Trigger=DFSDM_FILTER_SW_TRIGGER;
  hmic_filter.Init.InjectedParam.ScanMode=DISABLE;
  hmic_filter.Init.InjectedParam.DmaMode=DISABLE;
  hmic_filter.Init.InjectedParam.ExtTrigger=DFSDM_FILTER_EXT_TRIG_TIM1_TRGO;
  hmic_filter.Init.InjectedParam.ExtTriggerEdge=DFSDM_FILTER_EXT_TRIG_RISING_EDGE;
  hmic_filter.Init.FilterParam.SincOrder=DFSDM_FILTER_SINC3_ORDER;
  hmic_filter.Init.FilterParam.Oversampling=40U;
  hmic_filter.Init.FilterParam.IntOversampling=1U;
  HAL_StatusTypeDef filter_status=HAL_DFSDM_FilterInit(&hmic_filter);
  if(filter_status!=HAL_OK){
    g_g1_board.init_error_bits|=4U;g_g1_board.mic_last_hal_status=filter_status;return filter_status;
  }
  g_g1_board.mic_init_stage=31U; /* filter ready, selecting channel */
  if(HAL_DFSDM_FilterConfigRegChannel(&hmic_filter,DFSDM_CHANNEL_2,
        DFSDM_CONTINUOUS_CONV_ON)!=HAL_OK) {
    g_g1_board.init_error_bits|=4U;g_g1_board.mic_last_hal_status=HAL_ERROR;return HAL_ERROR;
  }
  g_g1_board.mic_init_stage=4U;
  hdma_mic.Instance=DMA1_Channel4;
  hdma_mic.Init.Request=DMA_REQUEST_0; /* L476 DMA1_CH4 req0 is DFSDM1_FLT0 (RM0351). */
  hdma_mic.Init.Direction=DMA_PERIPH_TO_MEMORY;
  hdma_mic.Init.PeriphInc=DMA_PINC_DISABLE;
  hdma_mic.Init.MemInc=DMA_MINC_ENABLE;
  hdma_mic.Init.PeriphDataAlignment=DMA_PDATAALIGN_WORD;
  hdma_mic.Init.MemDataAlignment=DMA_MDATAALIGN_WORD;
  hdma_mic.Init.Mode=DMA_CIRCULAR;
  hdma_mic.Init.Priority=DMA_PRIORITY_HIGH;
  if (HAL_DMA_Init(&hdma_mic)!=HAL_OK) {
    g_g1_board.init_error_bits|=8U;g_g1_board.mic_last_hal_status=HAL_ERROR;return HAL_ERROR;
  }
  __HAL_LINKDMA(&hmic_filter,hdmaReg,hdma_mic);
  HAL_NVIC_SetPriority(DMA1_Channel4_IRQn,5U,0U);
  HAL_NVIC_EnableIRQ(DMA1_Channel4_IRQn);
  s_hardware_ready=1U;g_g1_board.mic_ready=1U;
  g_g1_board.mic_init_stage=5U;g_g1_board.mic_last_hal_status=HAL_OK;
  return HAL_OK;
}

HAL_StatusTypeDef G1_MicStart(void)
{
  ++g_g1_board.mic_start_attempts;
  if (!s_hardware_ready){g_g1_board.mic_last_hal_status=HAL_ERROR;return HAL_ERROR;}
  if (g_g1_board.mic_running) return HAL_OK;
  s_head=s_tail=0U;
  HAL_StatusTypeDef result=HAL_DFSDM_FilterRegularStart_DMA(&hmic_filter,
    s_pdm_dma,G1_MIC_FRAMES*2U);
  g_g1_board.mic_last_hal_status=(uint32_t)result;
  if (result==HAL_OK)g_g1_board.mic_running=1U;
  else ++g_g1_board.mic_errors;
  return result;
}
HAL_StatusTypeDef G1_MicStop(void)
{
  if (!g_g1_board.mic_running)return HAL_OK;
  HAL_StatusTypeDef result=HAL_DFSDM_FilterRegularStop_DMA(&hmic_filter);
  if (result==HAL_OK)g_g1_board.mic_running=0U;
  return result;
}
/* AudioTask ownership stays in demo_app.c; ISR only wakes it when ready. */
extern void Demo_NotifyMicReadyFromISR(void);
static void mic_push(const int32_t *raw)
{
  uint8_t next=(uint8_t)((s_head+1U)&(G1_MIC_DEPTH-1U));
  if (next==s_tail){++g_g1_board.mic_overflow;return;}
  uint32_t peak=0U;
  for (unsigned i=0U;i<G1_MIC_FRAMES;++i){
    /* Signed 32-bit DFSDM output is left-justified 24-bit. Conservative
     * post-decimation headroom; acoustical calibration still required. */
    int32_t v=raw[i]>>10;
    if (v>32767)v=32767;
    if (v<(-32768))v=-32768;
    s_mic_queue[s_head][i]=(int16_t)v;
    uint32_t av=(uint32_t)(v<0?-v:v);
    if (av>peak)peak=av;
  }
  g_g1_board.mic_peak=peak;
  g_g1_board.mic_last_ms=HAL_GetTick();
  __DMB();s_head=next;g_g1_board.mic_blocks++;
  Demo_NotifyMicReadyFromISR();
}
uint8_t G1_MicTakeBlock(int16_t dest[G1_MIC_FRAMES])
{
  if (dest==NULL||s_tail==s_head)return 0U;
  memcpy(dest,s_mic_queue[s_tail],sizeof(s_mic_queue[0]));
  /* May25 prototype used microphone high-pass signal conditioning. Apply a
   * lightweight DC blocker in AudioTask, never in the DFSDM/DMA ISR. */
  for(unsigned i=0U;i<G1_MIC_FRAMES;++i){
    int32_t input=dest[i];
    s_mic_hp_state=((s_mic_hp_state*32604L)>>15) + input - s_mic_hp_prev_input;
    s_mic_hp_prev_input=(int16_t)input;
    int32_t v=s_mic_hp_state;
    if(v>32767)v=32767;
    if(v<(-32768))v=-32768;
    dest[i]=(int16_t)v;
  }
  __DMB();s_tail=(uint8_t)((s_tail+1U)&(G1_MIC_DEPTH-1U));
  return 1U;
}
/* Authoritative microphone IRQ: DFSDM1 filter 0 uses DMA1 channel 4. */
void DMA1_Channel4_IRQHandler(void){HAL_DMA_IRQHandler(&hdma_mic);}
void HAL_DFSDM_FilterRegConvHalfCpltCallback(DFSDM_Filter_HandleTypeDef *h)
{
  if (h->Instance==DFSDM1_Filter0){++g_g1_board.mic_half;mic_push(s_pdm_dma);}
}
void HAL_DFSDM_FilterRegConvCpltCallback(DFSDM_Filter_HandleTypeDef *h)
{
  if (h->Instance==DFSDM1_Filter0){++g_g1_board.mic_full;mic_push(s_pdm_dma+G1_MIC_FRAMES);}
}
void HAL_DFSDM_FilterErrorCallback(DFSDM_Filter_HandleTypeDef *h)
{
  if (h->Instance==DFSDM1_Filter0)++g_g1_board.mic_errors;
}
/* The bus is owned by ControlTask. Serial requests only request a re-probe;
 * no blocking HAL I2C operations occur from USART callbacks. */
void G1_TouchReprobe(void)
{
  s_touch_ready=0U;
  g_g1_board.touch_present=0U;
  s_touch_reinit_requested=1U; /* Deferred to ControlTask. */
  s_next_probe_ms=0U;
}

void G1_BoardPoll(uint32_t now,G1PhysicalInput *out)
{
  if (out==NULL)return;
  out->encoder_clicks=0;out->touch_rising=0;
  /* Read and clear the IRQ accumulator atomically; the ISR owns the decoder. */
  uint32_t primask=__get_PRIMASK();
  __disable_irq();
  int16_t steps=s_encoder_pending;
  s_encoder_pending=0;
  if(!primask)__enable_irq();
  if(steps>127)steps=127;
  if(steps<(-127))steps=-127;
  out->encoder_clicks=(int8_t)steps;
  g_g1_board.encoder_phase=(uint32_t)(
    ((HAL_GPIO_ReadPin(GPIOA,GPIO_PIN_0)==GPIO_PIN_SET)?2U:0U)|
    ((HAL_GPIO_ReadPin(GPIOA,GPIO_PIN_1)==GPIO_PIN_SET)?1U:0U));
  /* Slow-turning encoder fallback for boards whose EXTI dispatch is missing.
   * The IRQ decoder remains authoritative when IRQ edges are flowing. */
  {
    uint8_t phase=(uint8_t)g_g1_board.encoder_phase;
    if(phase!=s_encoder_last_polled_phase &&
       (uint32_t)(now-s_encoder_last_irq_ms)>250U){
      int step=GroupEncoder_Update(&s_encoder,(phase&2U)!=0U,(phase&1U)!=0U);
      ++g_g1_board.encoder_poll_edges;
      if(step){
        ++g_g1_board.encoder_detents;
        /* This event is processed here by ControlTask, not through IRQ FIFO. */
        if(step>0 && out->encoder_clicks<127)++out->encoder_clicks;
        if(step<0 && out->encoder_clicks>(-127))--out->encoder_clicks;
      }
    }
    s_encoder_last_polled_phase=phase;
  }
  g_g1_board.encoder_bad_edges=s_encoder.bad_edges;
  if(s_touch_reinit_requested) {
    s_touch_reinit_requested=0U;
    touch_i2c_recover(now);
    return;
  }
  if (!s_touch_ready) {
    if ((g_g1_board.init_error_bits&1U)==0U && (int32_t)(now-s_next_probe_ms)>=0)touch_init(now);
    return;
  }
  if ((uint32_t)(now-s_last_touch_ms)<15U)return;
  s_last_touch_ms=now;
  uint8_t state[2];
  if (!touch_read(0x00U,state,2U)) {
    ++g_g1_board.touch_errors;s_touch_ready=0U;
    g_g1_board.touch_present=0U;s_next_probe_ms=now+2000U;return;
  }
  ++g_g1_board.touch_reads;
  uint16_t touched=(uint16_t)((uint16_t)state[0]|((uint16_t)state[1]<<8));
  g_g1_board.touch_raw=(uint32_t)(touched&0x0FFFU);
  g_g1_board.touch_proximity=(uint32_t)((touched>>12)&1U);
  /* Schmitt-like time integrator. This rejects short brush/glitches without
   * making a normal ~30 ms tap feel unresponsive. No blocking delay. */
  for(unsigned k=8U;k<=11U;++k){
    uint16_t bit=(uint16_t)(1U<<k);
    if(touched & bit){if(s_touch_integrator[k]<2U)++s_touch_integrator[k];}
    else {if(s_touch_integrator[k]>0U)--s_touch_integrator[k];}
    if(s_touch_integrator[k]==0U)s_touch_latched&=(uint16_t)~bit;
    if(s_touch_integrator[k]==2U && !(s_touch_latched&bit)){
      s_touch_latched|=bit;
      if((int32_t)(now-s_touch_retrigger_ms[k])>=0){
        out->touch_rising|=bit;
        s_touch_retrigger_ms[k]=now+90U;
      }
    }
  }
}

/* STM32L4 I2C HAL is compiled once from the conventional ST vendor
 * translation units Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_i2c.c
 * and stm32l4xx_hal_i2c_ex.c. Never embed them here: CubeIDE 1.19
 * discovers and compiles those sources, and embedding caused 44 duplicate
 * symbols at the final link in R14. Mic DFSDM is left exactly as tested.
 */
#include "g1_embedded_hal_dfsdm.inc"
