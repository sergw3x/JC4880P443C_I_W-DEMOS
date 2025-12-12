file(GLOB FONT_SOURCES "fonts/*.c")
file(GLOB IMAGE_SOURCES "images/*.c")

idf_component_register(
    SRCS "main.c"
        ${IMAGE_SOURCES}
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
