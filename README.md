# Ultrasonic Radar System
A real-time ultrasonic radar system built on the ESP32 using the ESP-IDF (FreeRTOS) framework.

A real-time ultrasonic radar system built on the ESP32 using the ESP-IDF (FreeRTOS) framework.
This project interfaces an HC-SR04 ultrasonic sensor for distance measurement and an SSD1351 RGB OLED (SPI) for graphical output. It uses a multi-task FreeRTOS architecture to separate sensor acquisition from rendering, enabling smooth, non-blocking radar animation. The system performs polar-to-Cartesian coordinate conversion in C to draw a live 180° radar sweep with real-time object detection.
