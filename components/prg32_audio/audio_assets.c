#include "audio_internal.h"
#include <string.h>

_Static_assert(sizeof(prg32_sample_desc_t) == 20, "sample ABI changed");
_Static_assert(sizeof(prg32_instrument_desc_t) == 8, "instrument ABI changed");
_Static_assert(sizeof(prg32_audio_event_t) == 4, "event ABI changed");
_Static_assert(sizeof(prg32_audio_track_desc_t) == 8, "track ABI changed");
_Static_assert(sizeof(prg32_audio_block_header_t) == 40, "audio block ABI changed");

static int range_ok(size_t size, uint32_t offset, size_t bytes) {
    return offset <= size && bytes <= size - offset;
}

int prg32_audio_register_sample(uint16_t sample_id,
                                const uint8_t *samples,
                                uint32_t length,
                                uint16_t base_note,
                                uint8_t flags,
                                uint32_t loop_start,
                                uint32_t loop_end) {
    if (sample_id >= PRG32_AUDIO_MAX_SAMPLES || !samples || length == 0) {
        return -1;
    }
    if ((flags & PRG32_AUDIO_SAMPLE_LOOP) != 0u) {
        if (loop_end <= loop_start || loop_end > length) {
            return -1;
        }
    }
    prg32_audio_lock();
    prg32_audio_sample_slot_t *slot = &g_prg32_audio.samples[sample_id];
    memset(slot, 0, sizeof(*slot));
    slot->data = samples;
    slot->desc.offset = 0;
    slot->desc.length = length;
    slot->desc.loop_start = loop_start;
    slot->desc.loop_end = loop_end;
    slot->desc.base_note = base_note ? base_note : 60;
    slot->desc.flags = flags;
    slot->present = true;
    prg32_audio_unlock();
    return 0;
}

int prg32_audio_register_instrument(uint16_t instrument_id,
                                    const prg32_instrument_desc_t *instrument) {
    if (instrument_id >= PRG32_AUDIO_MAX_INSTRUMENTS || !instrument ||
        (!PRG32_AUDIO_IS_SYNTH_ID(instrument->sample_id) &&
         instrument->sample_id >= PRG32_AUDIO_MAX_SAMPLES)) {
        return -1;
    }
    prg32_audio_lock();
    g_prg32_audio.instruments[instrument_id].desc = *instrument;
    g_prg32_audio.instruments[instrument_id].present = true;
    prg32_audio_unlock();
    return 0;
}

int prg32_audio_register_track(uint16_t track_id,
                               const prg32_audio_event_t *events,
                               uint32_t event_count) {
    if (track_id >= PRG32_AUDIO_MAX_TRACKS || !events || event_count == 0) {
        return -1;
    }
    prg32_audio_lock();
    g_prg32_audio.tracks[track_id].events = events;
    g_prg32_audio.tracks[track_id].event_count = event_count;
    g_prg32_audio.tracks[track_id].present = true;
    prg32_audio_unlock();
    return 0;
}

void prg32_audio_clear_assets(void) {
    prg32_audio_lock();
    memset(g_prg32_audio.samples, 0, sizeof(g_prg32_audio.samples));
    memset(g_prg32_audio.instruments, 0, sizeof(g_prg32_audio.instruments));
    memset(g_prg32_audio.tracks, 0, sizeof(g_prg32_audio.tracks));
    g_prg32_audio.tracker.active = false;
    prg32_audio_unlock();
}

int prg32_audio_load_block(const void *block, size_t block_size) {
    if (!block || block_size < sizeof(prg32_audio_block_header_t)) {
        return -1;
    }
    const uint8_t *base = (const uint8_t *)block;
    const prg32_audio_block_header_t *header =
        (const prg32_audio_block_header_t *)block;

    if (memcmp(header->magic, PRG32_AUDIO_BLOCK_MAGIC, 4) != 0 ||
        header->version != PRG32_AUDIO_BLOCK_VERSION ||
        header->header_size < sizeof(*header) ||
        header->block_size > block_size ||
        header->sample_count > PRG32_AUDIO_MAX_SAMPLES ||
        header->instrument_count > PRG32_AUDIO_MAX_INSTRUMENTS ||
        header->track_count > PRG32_AUDIO_MAX_TRACKS) {
        return -1;
    }

    size_t sample_bytes = (size_t)header->sample_count * sizeof(prg32_sample_desc_t);
    size_t instrument_bytes =
        (size_t)header->instrument_count * sizeof(prg32_instrument_desc_t);
    size_t track_bytes =
        (size_t)header->track_count * sizeof(prg32_audio_track_desc_t);

    if (!range_ok(header->block_size, header->samples_offset, sample_bytes) ||
        !range_ok(header->block_size, header->instruments_offset, instrument_bytes) ||
        !range_ok(header->block_size, header->tracks_offset, track_bytes) ||
        header->data_offset > header->block_size ||
        header->events_offset > header->block_size) {
        return -1;
    }

    const prg32_sample_desc_t *samples =
        (const prg32_sample_desc_t *)(base + header->samples_offset);
    const prg32_instrument_desc_t *instruments =
        (const prg32_instrument_desc_t *)(base + header->instruments_offset);
    const prg32_audio_track_desc_t *tracks =
        (const prg32_audio_track_desc_t *)(base + header->tracks_offset);
    const prg32_audio_event_t *events =
        (const prg32_audio_event_t *)(base + header->events_offset);
    const uint8_t *sample_data = base + header->data_offset;
    size_t sample_data_size = header->block_size - header->data_offset;

    prg32_audio_lock();
    memset(g_prg32_audio.samples, 0, sizeof(g_prg32_audio.samples));
    memset(g_prg32_audio.instruments, 0, sizeof(g_prg32_audio.instruments));
    memset(g_prg32_audio.tracks, 0, sizeof(g_prg32_audio.tracks));

    for (uint16_t i = 0; i < header->sample_count; ++i) {
        const prg32_sample_desc_t *desc = &samples[i];
        if (!range_ok(sample_data_size, desc->offset, desc->length)) {
            prg32_audio_unlock();
            prg32_audio_clear_assets();
            return -1;
        }
        g_prg32_audio.samples[i].desc = *desc;
        g_prg32_audio.samples[i].data = sample_data + desc->offset;
        g_prg32_audio.samples[i].present = desc->length > 0;
    }

    for (uint16_t i = 0; i < header->instrument_count; ++i) {
        g_prg32_audio.instruments[i].desc = instruments[i];
        g_prg32_audio.instruments[i].present = true;
    }

    size_t event_capacity =
        (header->data_offset - header->events_offset) / sizeof(prg32_audio_event_t);
    for (uint16_t i = 0; i < header->track_count; ++i) {
        if (tracks[i].event_offset > event_capacity ||
            tracks[i].event_count > event_capacity - tracks[i].event_offset) {
            prg32_audio_unlock();
            prg32_audio_clear_assets();
            return -1;
        }
        g_prg32_audio.tracks[i].events = events + tracks[i].event_offset;
        g_prg32_audio.tracks[i].event_count = tracks[i].event_count;
        g_prg32_audio.tracks[i].present = tracks[i].event_count > 0;
    }
    prg32_audio_unlock();
    return 0;
}
