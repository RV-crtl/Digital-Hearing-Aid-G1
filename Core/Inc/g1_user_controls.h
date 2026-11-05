#ifndef G1_USER_CONTROLS_H
#define G1_USER_CONTROLS_H
#include <stdint.h>

/* Hardware-neutral, deterministic controller shared by physical and UART UI.
 * Only ControlTask mutates this structure; AudioTask consumes applied mode/gain. */
#define G1_UI_AUTO 0U
#define G1_UI_BT 1U
#define G1_UI_TC 2U
#define G1_UI_OFF 3U
#define G1_UI_MIC 4U
#define G1_UI_MAX_GAIN_Q15 24576U /* 75% digital full-scale; NOT an SPL limit */
#define G1_UI_STEP_Q15 1229U /* approximately 5 percentage points per detent */
#define G1_UI_SOURCE_CHANGED (1U<<0)
#define G1_UI_MUTE_CHANGED (1U<<1)
#define G1_UI_GAIN_CHANGED (1U<<2)
#define G1_UI_DSP_CHANGED (1U<<3)
#define G1_UI_PREF_CHANGED (1U<<4)
#define G1_UI_SPATIAL_CHANGED (1U<<5)
#define G1_UI_NOISE_CHANGED (1U<<6)
#define G1_UI_UNKNOWN (1UL<<31)
#define G1_UI_INVALID (1UL<<30)

typedef struct {
  uint8_t source;   /* G1_UI_* equals DEMO_MODE_* for routing */
  uint8_t muted;
  uint8_t dsp;
  uint8_t adaptive;
  uint8_t spatial;       /* mono room ambience */
  uint8_t noise_reduce;  /* microphone rumble filtering, not ANC */
  uint16_t gain_q15;
} G1UiState;
void G1Ui_Init(G1UiState *s);
uint32_t G1Ui_Touch(G1UiState *s, uint16_t rising);
uint32_t G1Ui_Encoder(G1UiState *s, int signed_detents);
uint32_t G1Ui_Command(G1UiState *s, const char *line);
uint32_t G1Ui_Percent(const G1UiState *s);
const char *G1Ui_SourceName(uint8_t source);
#endif
