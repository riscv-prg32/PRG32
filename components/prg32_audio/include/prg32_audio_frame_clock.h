#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The audio frame clock: one scheduling boundary between cartridge frames and audio
 * buffers, used where nothing else paces the audio (QEMU, PRG32_QEMU_AUDIO_REDIRECT).
 *
 * While a cartridge runs, its frames drive the engine: frame N (counting from 0 after
 * init) starts at N x PRG32_FRAME_MS, every audio call made by init or by that frame takes
 * effect at its start, and the engine then renders whole 20 ms buffers up to the start of
 * frame N + 1. A sound started in frame N therefore begins with the first buffer rendered
 * after N x 33 ms, on every run - the same rule the PRG32 Construction Kit's browser core
 * and PRG32-QT follow, so their recordings equal QEMU's sample for sample.
 *
 * On the console the I2S DMA paces the audio task and these calls do nothing: the
 * physical console's scheduling is unchanged.
 */

/** The loaded cartridge is about to run init: its frames drive the audio from now on. */
void prg32_audio_cartridge_start(void);

/** One cartridge frame of `ms` has run: render the audio up to the next frame's start. */
void prg32_audio_cartridge_frame(uint32_t ms);

/** No cartridge runs any more: the audio task renders on its own again. */
void prg32_audio_cartridge_stop(void);

#ifdef __cplusplus
}
#endif
