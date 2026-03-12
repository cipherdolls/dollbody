#pragma once
#include <stdint.h>

void record_init(void);

// Live RMS level updated by i2s_reader_task during CONV_LISTENING/CONV_RECORDING.
// Read by LED task to drive brightness. Range 0–32767, typically 0–2000 for speech.
extern volatile uint16_t g_audio_rms;
