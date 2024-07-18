/* boards/0401D.h */


#define FANPICO_MODEL     "0401D"

#define FAN_COUNT     4   /* Number of Fan outputs on the board */
#define MBFAN_COUNT   1   /* Number of (Motherboard) Fan inputs on the board */
#define SENSOR_COUNT  3   /* Number of sensor inputs on the board */

#ifdef LIB_PICO_CYW43_ARCH
#define LED_PIN -1
#else
#define LED_PIN 25
#endif

/* Pins for Fan Signals */

#define TACHO_READ_MULTIPLEX 0

/* pins for multiplexed read */
#define FAN_TACHO_READ_PIN    -1
#define FAN_TACHO_READ_S0_PIN -1
#define FAN_TACHO_READ_S1_PIN -1
#define FAN_TACHO_READ_S2_PIN -1
/* pins for direct read / multiplexer ports (if multiplexer in use) */
#define FAN1_TACHO_READ_PIN 18
#define FAN2_TACHO_READ_PIN 19
#define FAN3_TACHO_READ_PIN 20
#define FAN4_TACHO_READ_PIN 21
#define FAN5_TACHO_READ_PIN -1
#define FAN6_TACHO_READ_PIN -1
#define FAN7_TACHO_READ_PIN -1
#define FAN8_TACHO_READ_PIN -1

#define FAN1_PWM_GEN_PIN 4  /* PWM2A */
#define FAN2_PWM_GEN_PIN 5  /* PWM2B */
#define FAN3_PWM_GEN_PIN 6  /* PWM3A */
#define FAN4_PWM_GEN_PIN 7  /* PWM3B */
#define FAN5_PWM_GEN_PIN -1
#define FAN6_PWM_GEN_PIN -1
#define FAN7_PWM_GEN_PIN -1
#define FAN8_PWM_GEN_PIN -1


/* Pins for Motherboar Fan Connector Signals */

#define MBFAN1_TACHO_GEN_PIN 16
#define MBFAN2_TACHO_GEN_PIN -1
#define MBFAN3_TACHO_GEN_PIN -1
#define MBFAN4_TACHO_GEN_PIN -1

#define MBFAN1_PWM_READ_PIN 13 /* PWM6B */
#define MBFAN2_PWM_READ_PIN -1
#define MBFAN3_PWM_READ_PIN -1
#define MBFAN4_PWM_READ_PIN -1


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
#define SCK_PIN        10
#define MOSI_PIN       11
#define MISO_PIN      -1
#define CS_PIN         9

#define DC_PIN         8
#define LCD_RESET_PIN  12
#define LCD_LIGHT_PIN -1
#define ONEWIRE_PIN    15


#define OLED_DISPLAY 1
#define LCD_DISPLAY 1

#define TTL_SERIAL   1
#define TTL_SERIAL_UART uart0
#define TTL_SERIAL_SPEED 115200
