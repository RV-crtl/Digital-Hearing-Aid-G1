# G1 — printable system architecture

**Revision:** 03 November 2026 · **Target:** custom PCB and R24/R26 series (not the legacy root CubeIDE target) · **Format:** A3 landscape, print two copies.

This diagram separates the **data plane** (solid connections) and **control plane** (dashed connections). Peripheral mappings below come from the PCB-series code. It is a design/implementation map, **not measured electrical or real-time validation**.

```mermaid
flowchart LR
  subgraph P["Standalone power and physical hardware"]
    PWR["Intended protected source<br/>USB-C / regulated rails"]
    MIC["Digital PDM microphone<br/>PC2 CKOUT · PB14 DATIN2"]
    BLE["Bluetooth receiver / SAI1A<br/>USART1 for module control"]
    LOOP["Telecoil + analogue front end"]
    ADC["PCM1808 ADC<br/>24-bit I2S framing"]
    TOUCH["MPR121 · I2C1 PB6/PB7<br/>E8 BT E9 TC E10 mute E11 MIC"]
    ENC["Rotary encoder PA0/PA1<br/>5% step per detent"]
  end

  subgraph M["STM32L476RG · FreeRTOS · production custom-PCB firmware"]
    DFSDM["DFSDM1 + DMA<br/>PDM → mono PCM"]
    RX1["SAI1A RX + DMA<br/>Bluetooth PCM"]
    RX2["SAI2A RX + DMA<br/>Telecoil LOW16 decode"]
    IRQ["DMA half/full IRQ<br/>snapshot + notify"]
    AUD["AudioTask · priority 7<br/>source routing · ambient AGC · TC filter<br/>gain limit · 3-cluster scene learner"]
    Q["Bounded control event queue"]
    CTRL["ControlTask · priority 6<br/>touch · encoder · source · mute"]
    BT["BtTask · priority 5<br/>Bluetooth status"]
    BG["Background task · priority 2<br/>noncritical maintenance"]
    FIFO["Bounded output FIFO<br/>rate matching / underflow guard"]
    TX["SAI2B TX DMA"]
  end

  AMP["Digital audio amplifier<br/>output transducer"]
  PWR --> MIC
  PWR --> M
  MIC --> DFSDM --> IRQ
  BLE --> RX1 --> IRQ
  LOOP --> ADC --> RX2 --> IRQ
  IRQ -->|"ISR→task notification"| AUD
  TOUCH -.-> CTRL
  ENC -.-> CTRL
  CTRL -.-> Q
  Q -.-> AUD
  BT -.-> AUD
  BG -.-> AUD
  AUD --> FIFO --> TX --> AMP
```

## Interface and ownership summary

| Interface | Producer → consumer | Owner / scheduling contract |
| --- | --- | --- |
| PDM / DFSDM + DMA | Microphone → PCM blocks | Product-board driver; completed blocks handed to AudioTask, never processed inside ISR |
| SAI1A receive DMA | BT PCM1808/serial data → capture ring | DMA callback snapshots completed half; AudioTask consumes; Bluetooth decoding independent of TC |
| SAI2A receive DMA | TC PCM1808 → capture ring | Signed **LOW16** decode is established by PCB test; AudioTask applies TC cleanup |
| Physical controls | MPR121 + encoder → mode / volume | ControlTask polls/debounces, applies bounded policy; control events use queue |
| Bluetooth module control | USART1 ↔ module | BtTask and board-specific module interface; not the optional debug console |
| Diagnostic console | USART2 (optional) | **Disabled in operating build**; not required for audio or UI |
| Audio output | AudioTask → FIFO → SAI2B DMA | DMA output; no busy-waiting/serial printing in real-time ISR |
| Adaptive DSP / ML | PCM audio → bounded scene features | AudioTask sole owner; three online **unlabelled** clusters; no safety-critical event recognition |

## Confirm before the assessor demonstration

- Verify the *actual* operating image has no test-load tasks enabled and no diagnostic UART requirement.
- Confirm independent board power, sensor supply, touch/encoder operation, TC FILTER ON, and correct audio output.
- Measure task headroom/overrun and acoustic peak on a safe fixture; display software-unit tests separately from physical evidence.
- The current circuit files are the original KiCad design and have not been revised to match any later CAD mechanical placement.

**Historical diagram disclaimer:** Older Nucleo B1, LD2, and “microphone pending integration” diagrams belong to the legacy code baseline, not the current board demonstration.
