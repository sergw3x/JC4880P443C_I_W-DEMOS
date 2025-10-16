/*
* SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
*
* SPDX-License-Identifier: ESPRESSIF MIT
*/

#include <string.h>
#include "esp_ipa.h"

typedef struct esp_video_ipa_index {
    const char *name;
    const esp_ipa_config_t *ipa_config;
} esp_video_ipa_index_t;

static const esp_ipa_awb_config_t s_ipa_awb_OV02C10_config = {
    .model = ESP_IPA_AWB_MODEL_0,
    .min_counted = 2000,
    .min_red_gain_step = 0.3400,
    .min_blue_gain_step = 0.3400,
    .range = {
        .green_max = 190,
        .green_min = 81,
        .rg_max = 0.9096,
        .rg_min = 0.5730,
        .bg_max = 0.9634,
        .bg_min = 0.5368
    },
    .green_luma_env = "dummy_awb_luma",
    .green_luma_init = 91,
    .green_luma_step_ratio = 0.3000
};

static const esp_ipa_agc_meter_light_threshold_t s_ipa_agc_meter_light_thresholds_OV02C10[] = {
    {
        .luma_threshold = 20,
        .weight_offset = 1,
    },
    {
        .luma_threshold = 55,
        .weight_offset = 2,
    },
    {
        .luma_threshold = 95,
        .weight_offset = 3,
    },
    {
        .luma_threshold = 155,
        .weight_offset = 4,
    },
    {
        .luma_threshold = 235,
        .weight_offset = 5,
    },
};

static const esp_ipa_agc_config_t s_ipa_agc_OV02C10_config = {
    .exposure_frame_delay = 1,
    .exposure_adjust_delay = 0,
    .gain_frame_delay = 1,
    .min_gain_step = 0.0300,
    .inc_gain_ratio = 0.3200,
    .dec_gain_ratio = 0.4200,
    .anti_flicker_mode = ESP_IPA_AGC_ANTI_FLICKER_PART,
    .ac_freq = 50,
    .luma_low = 62,
    .luma_high = 105,
    .luma_target = 79,
    .luma_low_threshold = 14,
    .luma_low_regions = 5,
    .luma_high_threshold = 239,
    .luma_high_regions = 3,
    .luma_weight_table = {
        1, 1, 2, 1, 1, 1, 2, 3, 2, 1, 1, 3, 5, 3, 1, 1, 2, 3, 2, 1, 1, 1, 2, 1, 1, 
    },
    .meter_mode = ESP_IPA_AGC_METER_HIGHLIGHT_PRIOR,
    .high_light_prior_config = {
        .luma_high_threshold = 202,
        .luma_low_threshold = 119,
        .weight_offset = 5,
        .luma_offset = -1
    },
    .low_light_prior_config = {
        .luma_high_threshold = 64,
        .luma_low_threshold = 49,
        .weight_offset = 5,
        .luma_offset = 1
    },
    .light_threshold_config = {
        .table = s_ipa_agc_meter_light_thresholds_OV02C10,
        .table_size = ARRAY_SIZE(s_ipa_agc_meter_light_thresholds_OV02C10)
    },
};

static const esp_ipa_ian_luma_ae_config_t s_esp_ipa_ian_luma_ae_OV02C10_config = {                 
    .weight = {
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    },
};

static const esp_ipa_ian_luma_config_t s_esp_ipa_ian_luma_OV02C10_config = {
    .ae = &s_esp_ipa_ian_luma_ae_OV02C10_config,
};

static const esp_ipa_ian_config_t s_ipa_ian_OV02C10_config = {
    .luma = &s_esp_ipa_ian_luma_OV02C10_config,
};

static const esp_ipa_acc_sat_t s_ipa_acc_sat_OV02C10_config[] = {
    {
        .color_temp = 0,
        .saturation = 136
    },
};

static const esp_ipa_acc_ccm_unit_t s_esp_ipa_acc_ccm_OV02C10_table[] = {
    {
        .color_temp = 0,
        .ccm = {
            .matrix = {
                { 1.4080, -0.0940, -0.3140 },
                { -0.1300, 1.2800, -0.1500 },
                { -0.0720, -0.1730, 1.2450 }
            }
        }
    },
};

static const esp_ipa_acc_ccm_config_t s_esp_ipa_acc_ccm_OV02C10_config = {
    .model = 0,
    .luma_env = "ae.luma.avg",
    .luma_low_threshold = 28.0000,
    .luma_low_ccm = {
        .matrix = {
            { 1.0000, 0.0000, 0.0000 },
            { 0.0000, 1.0000, 0.0000 },
            { 0.0000, 0.0000, 1.0000 }
        }
    }
    ,
    .ccm_table = s_esp_ipa_acc_ccm_OV02C10_table,
    .ccm_table_size = 1,
};

static const esp_ipa_acc_config_t s_ipa_acc_OV02C10_config = {
    .sat_table = s_ipa_acc_sat_OV02C10_config,
    .sat_table_size = ARRAY_SIZE(s_ipa_acc_sat_OV02C10_config),
    .ccm = &s_esp_ipa_acc_ccm_OV02C10_config,
};

static const esp_ipa_adn_bf_t s_ipa_adn_bf_OV02C10_config[] = {
    {
        .gain = 1000,
        .bf = {
            .level = 3,
            .matrix = {
                {2, 4, 2},
                {4, 5, 4},
                {2, 4, 2}
            }
        }
    },
    {
        .gain = 4000,
        .bf = {
            .level = 3,
            .matrix = {
                {1, 3, 1},
                {3, 5, 3},
                {1, 3, 1}
            }
        }
    },
    {
        .gain = 8000,
        .bf = {
            .level = 4,
            .matrix = {
                {1, 3, 1},
                {3, 4, 3},
                {1, 3, 1}
            }
        }
    },
    {
        .gain = 16000,
        .bf = {
            .level = 5,
            .matrix = {
                {1, 3, 1},
                {3, 5, 3},
                {1, 3, 1}
            }
        }
    },
    {
        .gain = 24000,
        .bf = {
            .level = 6,
            .matrix = {
                {1, 2, 1},
                {2, 3, 2},
                {1, 2, 1}
            }
        }
    },
    {
        .gain = 32000,
        .bf = {
            .level = 7,
            .matrix = {
                {1, 2, 1},
                {2, 4, 2},
                {1, 2, 1}
            }
        }
    },
    {
        .gain = 64000,
        .bf = {
            .level = 7,
            .matrix = {
                {1, 2, 1},
                {2, 2, 2},
                {1, 2, 1}
            }
        }
    },
};

static const esp_ipa_adn_dm_t s_ipa_adn_dm_OV02C10_config[] = {
    {
        .gain = 1000,
        .dm = {
            .gradient_ratio = 1.5000
        }
    },
    {
        .gain = 4000,
        .dm = {
            .gradient_ratio = 1.2500
        }
    },
    {
        .gain = 8000,
        .dm = {
            .gradient_ratio = 1.0500
        }
    },
    {
        .gain = 12000,
        .dm = {
            .gradient_ratio = 1.0000
        }
    },
};

static const esp_ipa_adn_config_t s_ipa_adn_OV02C10_config = {
    .bf_table = s_ipa_adn_bf_OV02C10_config,
    .bf_table_size = ARRAY_SIZE(s_ipa_adn_bf_OV02C10_config),
    .dm_table = s_ipa_adn_dm_OV02C10_config,
    .dm_table_size = ARRAY_SIZE(s_ipa_adn_dm_OV02C10_config),
};

static const esp_ipa_aen_gamma_unit_t s_esp_ipa_aen_gamma_OV02C10_table[] = {
    {
        .luma = 71.1000,
        .gamma_param = 0.5180,
    },
};

static const esp_ipa_aen_gamma_config_t s_ipa_aen_gamma_OV02C10_config = {
    .use_gamma_param = true,
    .luma_env = "ae.luma.avg",
    .luma_min_step = 16.0000,
    .gamma_table = s_esp_ipa_aen_gamma_OV02C10_table,
    .gamma_table_size = 1,
};

static const esp_ipa_aen_sharpen_t s_ipa_aen_sharpen_OV02C10_config[] = {
    {
        .gain = 1000,
        .sharpen = {
            .h_thresh = 25,
            .l_thresh = 5,
            .h_coeff = 1.9250,
            .m_coeff = 1.8250,
            .matrix = {
                {1, 2, 1},
                {2, 2, 2},
                {1, 2, 1}
            }
        }
    },
    {
        .gain = 8000,
        .sharpen = {
            .h_thresh = 20,
            .l_thresh = 5,
            .h_coeff = 1.8250,
            .m_coeff = 1.4250,
            .matrix = {
                {2, 2, 2},
                {2, 1, 2},
                {2, 2, 2}
            }
        }
    },
    {
        .gain = 12000,
        .sharpen = {
            .h_thresh = 16,
            .l_thresh = 6,
            .h_coeff = 1.6250,
            .m_coeff = 1.3250,
            .matrix = {
                {1, 1, 1},
                {1, 1, 1},
                {1, 1, 1}
            }
        }
    },
    {
        .gain = 65000,
        .sharpen = {
            .h_thresh = 20,
            .l_thresh = 5,
            .h_coeff = 1.6250,
            .m_coeff = 1.2250,
            .matrix = {
                {1, 1, 1},
                {1, 1, 1},
                {1, 1, 1}
            }
        }
    },
};

static const esp_ipa_aen_con_t s_ipa_aen_con_OV02C10_config[] = {
    {
        .gain = 1000,
        .contrast = 132
    },
    {
        .gain = 16000,
        .contrast = 130
    },
    {
        .gain = 24000,
        .contrast = 128
    },
    {
        .gain = 65000,
        .contrast = 126
    },
};

static const esp_ipa_aen_config_t s_ipa_aen_OV02C10_config = {
    .gamma = &s_ipa_aen_gamma_OV02C10_config,
    .sharpen_table = s_ipa_aen_sharpen_OV02C10_config,
    .sharpen_table_size = ARRAY_SIZE(s_ipa_aen_sharpen_OV02C10_config),
    .con_table = s_ipa_aen_con_OV02C10_config,
    .con_table_size = ARRAY_SIZE(s_ipa_aen_con_OV02C10_config),
};

static const char *s_ipa_OV02C10_names[] = {
    "esp_ipa_awb",
    "esp_ipa_agc",
    "esp_ipa_ian",
    "esp_ipa_acc",
    "esp_ipa_adn",
    "esp_ipa_aen",
};

static const esp_ipa_config_t s_ipa_OV02C10_config = {
    .names = s_ipa_OV02C10_names,
    .nums = ARRAY_SIZE(s_ipa_OV02C10_names),
    .version = 1,
    .awb = &s_ipa_awb_OV02C10_config,
    .agc = &s_ipa_agc_OV02C10_config,
    .ian = &s_ipa_ian_OV02C10_config,
    .acc = &s_ipa_acc_OV02C10_config,
    .adn = &s_ipa_adn_OV02C10_config,
    .aen = &s_ipa_aen_OV02C10_config,
};

static const esp_video_ipa_index_t s_video_ipa_configs[] = {
    {
        .name = "OV02C10",
        .ipa_config = &s_ipa_OV02C10_config
    },
};

const esp_ipa_config_t *esp_ipa_pipeline_get_config(const char *name)
{
    for (int i = 0; i < ARRAY_SIZE(s_video_ipa_configs); i++) {
        if (!strcmp(name, s_video_ipa_configs[i].name)) {
            return s_video_ipa_configs[i].ipa_config;
        }
    }
    return NULL;
}

/* Json file: /Users/serg/Downloads/JC4880P443C_I_W/1-Demo/idf_examples/ESP-IDF/video_lcd_display/managed_components/espressif__esp_cam_sensor/sensors/ov02c10/cfg/ov02c10_default.json */

