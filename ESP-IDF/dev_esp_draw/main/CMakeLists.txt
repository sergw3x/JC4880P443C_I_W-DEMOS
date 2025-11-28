idf_component_register(
    SRCS "main.c"
         "command_processor.c"
         "display_manager.c"
         "lvgl_utils.c"
         "qr_commands.c"
         "text_commands.c"
         "clear_commands.c"
         "qr_text_commands.c"
         "usb_rx_improved.c"
         "hardware_manager.c"
    INCLUDE_DIRS ".")

idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(
    ${LVGL_LIB} 
    PRIVATE 
        -DLV_LVGL_H_INCLUDE_SIMPLE
        -DLV_USE_DEMO_MUSIC
)
