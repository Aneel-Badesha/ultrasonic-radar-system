idf_component_register(SRCS "radar_sensor.c"
                    INCLUDE_DIRS "."
                    REQUIRES ultrasonic ssd1351_driver)
