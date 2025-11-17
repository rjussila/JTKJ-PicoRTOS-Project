
#include <stdio.h>
#include <string.h>

#include <pico/stdlib.h>

#include <FreeRTOS.h>
#include <queue.h>
#include <task.h>

#include "tkjhat/sdk.h"
#include <usb_serial_debug>
#define DEFAULT_STACK_SIZE 2048
#define CDC_ITF_TX      1

//tehtävä 3
enum state {Waiting = 1, DATA_READY};

enum state myState = Waiting;
uint32_t ambientLight;

static void print_task(void *arg) {
    (void)arg;
    /*while(1) {
        if (myState == DATA_READY){
            usb_serial_print("Valoisuus: %lu lux\n", ambientLight);
        }
        
        uint32_t lux_value = veml6030_read_light();
        printf("Valoisuus: %lu lux\n", lux_value); 

        vTaskDelay(pdMS_TO_TICKS(2000));
    }*/
   while(1) {
        if (myState == DATA_READY) {
            
            usb_serial_print("Valoisuus: %lu lux\n", ambientLight); 

            if (tud_cdc_n_connected(CDC_ITF_TX)) {
                uint32_t timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
                snprintf(csv_buffer, sizeof(csv_buffer), "%lu,%lu\n", timestamp, ambientLight);
                tud_cdc_n_write_str(CDC_ITF_TX, csv_buffer);
                tud_cdc_n_write_flush(CDC_ITF_TX);
            }

            myState = WAITING;
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
}

//tehtävä 1
static void btn_fxn(uint gpio, uint32_t eventMask){
    if (gpio == SW1_PIN){
        bool myState = gpio_get(RED_LED_PIN);
        gpio_put(RED_LED_PIN, !myState);
    }
}
//tehtävä 2
static void sensor_task(void *arg) {
    (void)arg;
    init_veml6030();
    usb_serial_print("VEML6030 sensori alustettu\n");

    while(1) {
        uint32_t lux_value = veml6030_read_light();
        ambientLight = lux_value; 
        myState = DATA_READY;
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
static void leds_task(void *arg) {
    (void)arg;

    // Initialize LED
    init_led();
    printf("Initializing on board led\n");

    while (1) {
        toggle_led();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

static void display_task(void *arg) {
    (void)arg;

    init_display();
    printf("Initializing display\n");
    static int counter = 0; 

    while(1) {
        
        clear_display();
        char buf[5]; //Store a number of maximum 5 figures 
        sprintf(buf,"%d",counter++);
        write_text(buf);
        vTaskDelay(pdMS_TO_TICKS(4000));
    }

}

static void buzzer_task(void *arg) {
    (void)arg;

    //Initialize the buzzer
    init_buzzer();
    printf("Initializing buzzer\n");

    while(1){
        buzzer_play_tone (440, 500);
        vTaskDelay(pdMS_TO_TICKS(6000));
    }
}


int main() {
    //tehtävä 1
    init_button1(); 

    gpio_init(RED_LED_PIN);
    gpio_set_dir(RED_LED_PIN, GPIO_OUT);
    gpio_put(RED_LED_PIN, 0);

    gpio_set_irq_enabled_with_callback(
        SW1_PIN,
        GPIO_IRQ_EDGE_RISE,
        true,
        &btn_fxn
    );
    stdio_init_all();
    // Uncomment this lines if you want to wait till the serial monitor is connected
    // Specially for debugging
    while (!stdio_usb_connected()){
        sleep_ms(10);
    } 
    init_hat_sdk();
    sleep_ms(400); 

    TaskHandle_t ledsTaskHandle, displayTaskHandle, buzzerTaskHandle, sensorTaskHandle, printTaskHandle = NULL;
   
    // Create the single blinky/printf task
    // (en) We create a task
    BaseType_t result = xTaskCreate(leds_task,       // (en) Task function
                "led",              // (en) Name of the task 
                1024,               // (en) Size of the stack for this task (in words). Generally 1024 or 2048
                NULL,               // (en) Arguments of the task 
                2,                  // (en) Priority of this task
                &ledsTaskHandle);   // (en) A handle to control the execution of this task

    if(result != pdPASS) {
        printf("Led Task created failed\n");
        return 0;
    }
    //tehtävä 2
    result = xTaskCreate(sensor_task,
                "sensor",
                1024,
                NULL,
                2,
                &sensorTaskHandle);
                
    if(result != pdPASS) {
        printf("Sensor Task created failed\n");
        return 0;
    }

    result = xTaskCreate(buzzer_task,   // (en) Task function
            "buzzer",                   // (en) Name of the task 
                1024,                   // (en) Size of the stack for this task (in words). Generally 1024 or 2048
                NULL,                   // (en) Arguments of the task 
                2,                      // (en) Priority of this task
                &buzzerTaskHandle);     // (en) A handle to control the execution of this task

    if(result != pdPASS) {
        printf("Buzzer Task created failed\n");
        return 0;
    }
    result = xTaskCreate(display_task,   // (en) Task function
            "display",                   // (en) Name of the task 
                1024,                    // (en) Size of the stack for this task (in words). Generally 1024 or 2048
                NULL,                    // (en) Arguments of the task 
                2,                       // (en) Priority of this task
                &displayTaskHandle);     // (en) A handle to control the execution of this task
    //tehtävä 3
    result = xTaskCreate(print_task,
            "print",
            1024,
            NULL,
            2,
            &printTaskHandle);
    if(result != pdPASS) {
        printf("Display Task created failed\n");
        return 0;
    }
    // Start the scheduler (never returns)
    vTaskStartScheduler();



    
    return 0;
}

