#pragma once

void audio_init(void);
void audio_play_message(const char *message_id);
void audio_stop(void);
void audio_speaker_mute(void);
void audio_speaker_unmute(void);
