/* boards/0804D.h */


#define FANPICO_MODEL     "0804D"

#define FAN_COUNT     8   /* Number of Fan outputs on the board */
#define MBFAN_COUNT   4   /* Number of (Motherboard) Fan inputs on the board */
#define SENSOR_COUNT  3   /* Number of sensor inputs on the board */

#ifdef LIB_PICO_CYW43_ARCH
#define LED_PIN -1
#else
#define LED_PIN 25
#endif

/* Pins for Fan Signals */

#define TACHO_READ_MULTIPLEX 1

/* pins for multiplexed read */
#define FAN_TACHO_READ_PIN    26
#define FAN_TACHO_READ_S0_PIN 22
#define FAN_TACHO_READ_S1_PIN 21
#define FAN_TACHO_READ_S2_PIN 20
/* pins for direct read / multiplexer ports (if multiplexer in use) */
#define FAN1_TACHO_READ_PIN 7
#define FAN2_TACHO_READ_PIN 5
#define FAN3_TACHO_READ_PIN 3
#define FAN4_TACHO_READ_PIN 0
#define FAN5_TACHO_READ_PIN 1
#define FAN6_TACHO_READ_PIN 2
#define FAN7_TACHO_READ_PIN 6
#define FAN8_TACHO_READ_PIN 4

#define FAN1_PWM_GEN_PIN 4  /* PWM2A */
#define FAN2_PWM_GEN_PIN 5  /* PWM2B */
#define FAN3_PWM_GEN_PIN 6  /* PWM3A */
#define FAN4_PWM_GEN_PIN 7  /* PWM3B */
#define FAN5_PWM_GEN_PIN 8  /* PWM4A */
#define FAN6_PWM_GEN_PIN 9  /* PWM4B */
#define FAN7_PWM_GEN_PIN 10 /* PWM5A */
#define FAN8_PWM_GEN_PIN 11 /* PWM5B */


/* Pins for Motherboar Fan Connector Signals */

#define MBFAN1_TACHO_GEN_PIN 12
#define MBFAN2_TACHO_GEN_PIN 14
#define MBFAN3_TACHO_GEN_PIN 16
#define MBFAN4_TACHO_GEN_PIN 18

#define MBFAN1_PWM_READ_PIN 13 /* PWM6B */
#define MBFAN2_PWM_READ_PIN 15 /* PWM7B */
#define MBFAN3_PWM_READ_PIN 17 /* PWM0B */
#define MBFAN4_PWM_READ_PIN 19 /* PWM1B */


/* Pins for temperature sensors */

#define SENSOR1_READ_PIN  27  /* ADC1 / GPIO27 */
#define SENSOR2_READ_PIN  28  /* ADC2 / GPIO28 */

#define SENSOR1_READ_ADC  1  /* ADC1 / GPIO27 */
#define SENSOR2_READ_ADC  2  /* ADC2 / GPIO28 */
#define SENSOR3_READ_ADC  4  /* ADC4 / Internal Temperature sensor */


/* Interface Pins */

/* I2C */
#define I2C_HW    2  /* 1=i2c0, 2=i2c1, 0=bitbang... */
#define SDA_PIN   2
#define SCL_PIN   3

/* Serial */
#define TX_PIN    0
#define RX_PIN    1

/* SPI */
#define SCK_PIN        2
#define MOSI_PIN       3
#define MISO_PIN      -1
#define CS_PIN         1

#define DC_PIN         0
#define LCD_RESET_PIN -1
#define LCD_LIGHT_PIN -1


#define OLED_DISPLAY 1
#define LCD_DISPLAY 1

#define TTL_SERIAL   1
#define TTL_SERIAL_UART uart0
#define TTL_SERIAL_SPEED 115200
