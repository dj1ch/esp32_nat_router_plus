idf_component_register(SRCS "cmd_router.c"
                    INCLUDE_DIRS .
                    REQUIRES console nvs_flash driver esp_wifi)
