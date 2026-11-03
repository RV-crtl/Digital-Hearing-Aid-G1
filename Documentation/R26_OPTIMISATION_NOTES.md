# G1 R26 DSP optimisation and real-time verification

The firmware already separates **DMA/ISR work** from AudioTask-level DSP and uses statically allocated audio buffers. Each production audio block contains 128 samples at a nominal 48 kSa/s, giving a **2.667 ms per-block interval**. Real-time safety requires all processing, scheduled work and interrupt interference to fit within the actual target deadline with margin.

**What changed in this repository update:** a deterministic portable C throughput benchmark was added for `g1_ambient_dsp.c`, `g1_scene_ml.c` and `tc_cleanup.c`. It compiles the *real source modules*, processes 12,000 blocks, reports host timing and validates state progression. It does not change the R24/R26 hardware audio algorithms, whose working behaviour must remain stable during presentation preparation.

**What this does not establish:** a desktop GCC throughput ratio cannot be substituted for Cortex-M4F CPU utilization or an acoustic response measurement. STM32CubeIDE ARM code generation, interrupt costs, flash wait states, FreeRTOS scheduling, output FIFO depth and codec clocking all affect the physical system.

## Follow-up optimisation criteria

| Target | Evidence needed | Rule |
| --- | --- | --- |
| Audio processing and DMA deadlines | TIM2/DWT or GPIO pulse measurements on actual STM32 | Record worst-case block time and margin against 2.667 ms; **no guessed values** |
| Stack and SRAM | Actual linker `.map` plus FreeRTOS high-water marks | Check AudioTask, ControlTask, BtTask, DMA rings and output FIFO separately |
| Noise and AGC | Recorded pre/post audio on a reference fixture | Compare SNR, RMS, crest and perceived clarity before tuning gain |
| Adaptive learning | Repeated labeled/controlled environmental test episodes | Three learned prototypes are **unlabelled**; don't report accuracy for car/speech classes |
| Fault recovery | Hardware-level source loss, button bounce, queue saturation | Show recovery without restart and with diagnostics optional |
| Production code structure | Native CubeIDE build and regression tests after extraction | Only split the large application task after measurable benefit; keep known working R24 rollback |

The root firmware's large `demo_app.c` remains historical code. A safe future refactor should extract the audio output FIFO, source decoder, control ownership and diagnostics in small verified commits. **No “1000× performance” claim is supported** without comparative, target-measured benchmarks.
