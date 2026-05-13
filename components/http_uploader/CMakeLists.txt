idf_component_register(SRCS "http_uploader.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_http_client esp_task_wdt esp_system wifi_sta)
