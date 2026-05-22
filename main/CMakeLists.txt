idf_component_register(
    SRCS "main.c"
    REQUIRES prg32
    PRIV_REQUIRES esp_driver_gpio
    INCLUDE_DIRS "."
)
