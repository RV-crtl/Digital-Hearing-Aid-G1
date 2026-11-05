# Printable hardware + software integration diagram (A3 landscape)

Render this Mermaid with a vector-capable renderer and print **two A3 copies**; this repository file alone is *not* a physically printed diagram. **This draws current Nucleo target functions only.** Custom PCB path and PDM microphone remain unvalidated. Do not present the topology as a fabricated and tested full group board.

```mermaid
flowchart LR
 subgraph OUTSIDE["External hardware"]
  BT["BT401 Bluetooth receiver / analog output"]
  TC["Telecoil front-end / analogue conditioning"]
  ADC1["PCM1808 audio ADC #1"]
  ADC2["PCM1808 audio ADC #2"]
  UI["Nucleo PC13 button (not group PCB)"]
  AMP["MAX98357A digital amplifier"]
  TR["Bone conduction transducer"]
 end
 subgraph MCU["NUCLEO-L476RG / STM32L476RG"]
  RXBT["SAI1A RX DMA"]
  RXTC["SAI2A RX DMA"]
  IRQ["Half/full callbacks: bounded PCM ring publication"]
  AUDIO["AudioTask P7: DSP, source routing, guard"]
  CTRL["ControlTask P6: user events / queues"]
  BTTASK["BluetoothTask P5: UART / source supervision"]
  FSM["Source FSM M1-M4: validity/precedence"]
  TX["SAI2B TX DMA"]
  TIM["TIM2 run-time stats + TIM6 HAL tick"]
 end
 BT --> ADC1 --> RXBT --> IRQ
 TC --> ADC2 --> RXTC --> IRQ
 IRQ -->|"Direct task notification (G5)"| AUDIO
 UI --> CTRL -->|"FreeRTOS GroupEvent queue"| AUDIO
 BTTASK --> FSM --> AUDIO
 AUDIO --> TX --> AMP --> TR
 TIM -. "engineering instrumentation only" .-> AUDIO
```

**Hardware accuracy warning:** This abstraction uses MCU-side logical labels and original analog frontend components. Confirm physical pin assignments, SAI wiring, exact component variants, supply and output routing from the assembled specimen before printing the *final* revision.

**Interface contracts to add beside diagram:** DMA PCM blocks 128 frames at nominal 48 kHz; 16-bit signed audio at processing interfaces; data ring ownership ISR producer/AudioTask consumer; FreeRTOS command queue ControlTask producer/AudioTask consumer; source validity may be inferred from nonzero PCM rather than actual Bluetooth connection; faults cause mute/degrade and timed recovery. Exact wire/clock pin numbers must come from the chosen board's validated pin map.
