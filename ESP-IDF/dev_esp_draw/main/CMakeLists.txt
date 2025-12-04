file(GLOB FONT_SOURCES "fonts/*.c")

idf_component_register(
    SRCS "main.c"
        ${FONT_SOURCES}
    INCLUDE_DIRS ".")

target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)

idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(
    ${LVGL_LIB} 
    PRIVATE 
        -DLV_LVGL_H_INCLUDE_SIMPLE
        -DLV_USE_DEMO_MUSIC
)
