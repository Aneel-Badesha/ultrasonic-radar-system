idf_component_register(SRCS "radar_sensor.c"
                    INCLUDE_DIRS "."
                    REQUIRES ultrasonic ssd1351_driver nvs_flash esp_wifi esp_http_client)
