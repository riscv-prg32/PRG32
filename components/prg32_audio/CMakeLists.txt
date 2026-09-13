idf_component_register(
    SRCS
        "audio.c"
        "audio_assets.c"
        "audio_i2s.c"
        "audio_mixer.c"
        "audio_synth.c"
        "audio_tracker.c"
        "prg32_qemu_audio.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    PRIV_REQUIRES driver freertos esp_timer
)
