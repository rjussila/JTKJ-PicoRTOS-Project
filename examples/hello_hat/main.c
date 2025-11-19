#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include <pico/stdlib.h>
#include <hardware/i2c.h>
#include <hardware/gpio.h>

#include <FreeRTOS.h>
#include <queue.h>
#include <task.h>

#include "tkjhat/sdk.h"
#include "tusb.h"
#include "usbSerialDebug/helper.h"

// --- VAKIOT JA ASETUKSET ---
#define DEFAULT_STACK_SIZE 2048
#define EVENT_QUEUE_SIZE   10     // Jonon koko (montako merkkiä mahtuu puskuriin)

// Kiihtyvyysanturin kynnysarvot (riippuu anturin herkkyydestä, oletus n. 16000 = 1G)
#define ACCEL_THRESHOLD    12000 
// Aika (ms), joka asennon pitää pysyä vakaana ennen lähetystä
#define STABLE_TIME_MS     2000   

// Pinnit (Tarkista nämä SDK:sta / kytkentäkaaviosta)
#ifndef RED_LED_PIN
#endif
#ifndef SW1_PIN
#endif

// --- GLOBAALIT MUUTTUJAT ---
QueueHandle_t xMorseQueue; // Jono merkkien siirtoon tehtävien välillä

// Tilakoneen tilat
typedef enum {
    STATE_IDLE,         // Odotetaan asentoa
    STATE_tarkasta_viiva,// Vaaka-asento tunnistettu, odotetaan aikarajaa
    STATE_tarkasta_piste, // Pystyasento tunnistettu, odotetaan aikarajaa
    STATE_lahetetty          // Merkki lähetetty, odotetaan asennon vapautusta (nollausta)
} SystemState_t;

// --- APUFUNKTIOT: IMU (Kiihtyvyysanturi) ---
// HUOM: Tämä on esimerkki. Sinun täytyy tarkistaa oikeat rekisterit IMU:n (LSM6DSOX?) datalehdestä.
// Oletetaan tässä yksinkertainen I2C-luku kuten aiemmin.
void read_imu_accel(int16_t *x, int16_t *y, int16_t *z) {
    uint8_t buffer[6];
    uint8_t reg = 0x0B;
     // Esimerkki: OUTX_L_A (Tarkista datalehdestä!)
    
    // Oletetaan IMU_I2C_ADDR on määritelty SDK:ssa
    // i2c_write_blocking(i2c_default, IMU_I2C_ADDR, &reg, 1, true);
    // i2c_read_blocking(i2c_default, IMU_I2C_ADDR, buffer, 6, false);

    // Simuloidaan arvoja testausta varten (poista tämä kun I2C toimii):
    //*x = 0; *y = 0; *z = 16000; // Simuloi "litteänä pöydällä"
}

// --- TEHTÄVÄ: KESKEYTYSKÄSITTELIJÄ (NAPPI) ---
static void btn_fxn(uint gpio, uint32_t eventMask){
    if (gpio == SW1_PIN) {
        char merkki = ' '; // Välilyönti
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;

        // Lähetetään merkki jonoon keskeytyksestä
        xQueueSendFromISR(xMorseQueue, &merkki, &xHigherPriorityTaskWoken);

        // Pakotetaan context switch, jos vastaanottava tehtävä on korkeampi prioriteetti
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

// --- TEHTÄVÄ: IMU / ASENNON TUNNISTUS ---
static void imu_task(void *arg) {
    (void)arg;
    
    int16_t ax, ay, az;
    SystemState_t myState = STATE_IDLE;
    TickType_t aloitus = 0;

    // Alusta IMU tässä (i2c komennot)
    // init_imu(); 

    while (1) {
        read_imu_accel(&ax, &ay, &az);

        // 1. Tunnista asento
        bool littea     = (abs(az) > ACCEL_THRESHOLD); // Z-akseli dominoi -> Viiva
        bool pysty = (abs(ax) > ACCEL_THRESHOLD); // X-akseli dominoi -> Piste
        
        // 2. Tilakoneen logiikka
        switch (myState) {
            case STATE_IDLE:
            case STATE_lahetetty:
                if (pysty) {
                    myState = STATE_tarkasta_viiva;
                    aloitus = xTaskGetTickCount(); // Aloita ajanotto
                } else if (pysty) {
                    myState = STATE_tarkasta_piste;
                    aloitus = xTaskGetTickCount();
                } else {
                    myState = STATE_IDLE; // Epämääräinen asento
                }
                break;

            case STATE_tarkasta_viiva: // Odotetaan viivaa (-)
                if (!littea) {
                    myState = STATE_IDLE; // Asento muuttui, nollaa
                } else {
                    // Tarkista onko aika kulunut
                    if ((xTaskGetTickCount() - aloitus) * portTICK_PERIOD_MS >= STABLE_TIME_MS) {
                        char c = '-';
                        xQueueSend(xMorseQueue, &c, 0); // Lähetä jonoon
                        usb_serial_print("DEBUG: Viiva tunnistettu\n");
                        myState = STATE_lahetetty; // Estä toistuva lähetys
                    }
                }
                break;

            case STATE_tarkasta_piste: // Odotetaan pistettä (.)
                if (!pysty) {
                    myState = STATE_IDLE;
                } else {
                    if ((xTaskGetTickCount() - aloitus) * portTICK_PERIOD_MS >= STABLE_TIME_MS) {
                        char c = '.';
                        xQueueSend(xMorseQueue, &c, 0);
                        usb_serial_print("DEBUG: Piste tunnistettu\n");
                        myState = STATE_lahetetty;
                    }
                }
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(100)); // Tarkista 10 kertaa sekunnissa
    }
}

// --- TEHTÄVÄ: KOMMUNIKAATIO (USB LÄHETYS JA VASTAANOTTO) ---
static void comm_task(void *arg) {
    (void)arg;
    char received_char;
    char char_merkki;

    while (1) {
        
        // --- OSA A: Lähetä dataa PC:lle (tarkista jono) ---
        // xQueueReceive odottaa 10ms (ei blokkaa kokonaan, jotta vastaanotto toimii)
        if (xQueueReceive(xMorseQueue, &char_merkki, pdMS_TO_TICKS(10))) {
            
            // 1. Tulosta debug-kanavaan
            usb_serial_print("Sending: %c\n", char_merkki);
            
            // 2. Lähetä data-kanavaan (CDC 1)
            if (tud_cdc_n_connected(1)) {
                tud_cdc_n_write_char(1, char_merkki);
                tud_cdc_n_write_flush(1);
            }
        }

        // --- OSA B: Vastaanota dataa PC:ltä (toista merkki) ---
        if (tud_cdc_n_available(1)) {
            // Lue merkki PC:ltä
            received_char = tud_cdc_n_read_char(1);

            // Toista merkki LEDillä
            if (received_char == '.') {
                gpio_put(RED_LED_PIN, 1);
                vTaskDelay(pdMS_TO_TICKS(200)); // Lyhyt välähdys
                gpio_put(RED_LED_PIN, 0);
            } else if (received_char == '-') {
                gpio_put(RED_LED_PIN, 1);
                vTaskDelay(pdMS_TO_TICKS(600)); // Pitkä välähdys
                gpio_put(RED_LED_PIN, 0);
            }
        }

        // TinyUSB:n ylläpito (tärkeä jos usb_task ei ole erikseen)
        // tud_task(); // Jos usb_task on olemassa, tätä ei tarvita tässä
    }
}

// --- TEHTÄVÄ: USB DRIVER TASK ---
static void usb_task(void *arg) {
    (void)arg;
    while (1) {
        tud_task();
    }
}

// --- PÄÄOHJELMA ---
int main() {
    // Alustukset
    usb_serial_debug_init();
    tud_init(BOARD_TUD_RHPORT);
    init_hat_sdk();
    
    // Alusta LED ja Nappi
    gpio_init(RED_LED_PIN); gpio_set_dir(RED_LED_PIN, GPIO_OUT);
    gpio_init(SW1_PIN); gpio_set_dir(SW1_PIN, GPIO_IN); gpio_pull_down(SW1_PIN); // Tai pull_up riippuen kytkennästä

    // Keskeytys napille
    gpio_set_irq_enabled_with_callback(SW1_PIN, GPIO_IRQ_EDGE_RISE, true, &btn_isr_handler);

    // 1. Luo JONO (Queue)
    xMorseQueue = xQueueCreate(EVENT_QUEUE_SIZE, sizeof(char));

    // 2. Luo TEHTÄVÄT
    xTaskCreate(usb_task, "USB", 2048, NULL, 3, NULL);
    xTaskCreate(imu_task, "IMU", 2048, NULL, 2, NULL);
    xTaskCreate(comm_task, "COMM", 2048, NULL, 2, NULL);

    // Käynnistä
    vTaskStartScheduler();

    return 0;
}