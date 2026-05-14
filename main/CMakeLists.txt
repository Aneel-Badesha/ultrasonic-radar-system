idf_component_register(SRCS "radar_sensor.c"
                    INCLUDE_DIRS "."
                    REQUIRES ultrasonic ssd1351_driver wifi_sta http_uploader servo
                             nvs_flash esp_timer esp_system)
