//
// Created by Сергей Шкляр on 13.12.2025.
//
#include "logo.h"
#include "lvgl.h"
#include "images/hex_logo.c"
#include "images/logo_caption.c"

void show_logo(){
    lv_obj_t * img = lv_img_create(lv_scr_act());
    lv_img_set_src(img, &hex_logo);
    lv_obj_align(img, LV_ALIGN_TOP_MID, 0, 150); // поднимаем на 50 пикселей от верха
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_white(), 0);


    lv_obj_t * img2 = lv_img_create(lv_scr_act());
    lv_img_set_src(img2, &logo_caption);
    lv_obj_align(img2, LV_ALIGN_BOTTOM_MID, 0, -120); // 20 пикселей от нижнего края
}