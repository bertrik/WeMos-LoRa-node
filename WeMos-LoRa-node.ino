#include <stdbool.h>
#include <stdint.h>

#include "cmdproc.h"
#include "editline.h"

#include "lmic.h"
#include <hal/hal.h>
#include "arduino_lmic_hal_boards.h"

#include <FastLED.h>

#include <EEPROM.h>
#include <Arduino.h>
#include <SPI.h>

#define SAVEDATA_MAGIC  0xCAFEBABE
#define printf Serial.printf

#define PIN_LORA_SS     D0
#define PIN_LORA_SCK    D5
#define PIN_LORA_MISO   D6
#define PIN_LORA_MOSI   D7
#define PIN_LORA_IRQ    D8

#define PIN_SCL         D1
#define PIN_SDA         D2

#define PIN_LED_RGB     D3
#define PIN_SWITCH      D4

// structure with non-volatile data, saved in simulated EEPROM
static struct savedata_t {
    uint8_t deveui[8];
    uint8_t appeui[8];
    uint8_t appkey[16];
    uint32_t magic;
} savedata;

static CRGB leds[2];
static char line[120];

// Pin mapping
const lmic_pinmap lmic_pins = {
    .nss = PIN_LORA_SS,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = LMIC_UNUSED_PIN,
    .dio = { PIN_LORA_IRQ, LMIC_UNUSED_PIN, LMIC_UNUSED_PIN }
};

void os_getDevEui(u1_t * buf)
{
    for (int i = 0; i < 8; i++) {
        buf[i] = savedata.deveui[7 - i];
    }
}

void os_getArtEui(u1_t * buf)
{
    for (int i = 0; i < 8; i++) {
        buf[i] = savedata.appeui[7 - i];
    }
}

void os_getDevKey(u1_t * buf)
{
    memcpy(buf, savedata.appkey, 16);
}

static void printhex(const uint8_t * buf, int len)
{
    for (int i = 0; i < len; i++) {
        printf("%02X", buf[i]);
    }
    printf("\n");
}

const char *event_names[] = { LMIC_EVENT_NAME_TABLE__INIT };

static void onEventCallback(void *user, ev_t ev)
{
    Serial.print(os_getTime());
    Serial.print(": ");
    Serial.println(event_names[ev]);
}

static void show_help(const cmd_t * cmds)
{
    for (const cmd_t * cmd = cmds; cmd->cmd != NULL; cmd++) {
        printf("%10s: %s\n", cmd->name, cmd->help);
    }
}

static int do_help(int argc, char *argv[]);

static int do_otaa(int argc, char *argv[])
{
    if (argc == 1) {
        // no arg: show current values
        if (savedata.magic != SAVEDATA_MAGIC) {
            printf("No valid data!\n");
        } else {
            printf("Dev EUI = ");
            printhex(savedata.deveui, 8);
            printf("App EUI = ");
            printhex(savedata.appeui, 8);
            printf("App key = ");
            printhex(savedata.appkey, 16);
        }
        return CMD_OK;
    }
    if (argc < 4) {
        return CMD_ARG;
    }

    char *deveui = argv[1];
    char *appeui = argv[2];
    char *appkey = argv[3];
    if ((strlen(deveui) != 16) || (strlen(appeui) != 16) || (strlen(appkey) != 32)) {
        return CMD_ARG;
    }

    char tmp[4];
    for (int i = 0; i < 8; i++) {
        strncpy(tmp, deveui, 2);
        deveui += 2;
        uint8_t b = strtoul(tmp, NULL, 16);
        savedata.deveui[i] = b;
    }
    for (int i = 0; i < 8; i++) {
        strncpy(tmp, appeui, 2);
        appeui += 2;
        uint8_t b = strtoul(tmp, NULL, 16);
        savedata.appeui[i] = b;
    }
    for (int i = 0; i < 16; i++) {
        strncpy(tmp, appkey, 2);
        appkey += 2;
        uint8_t b = strtoul(tmp, NULL, 16);
        savedata.appkey[i] = b;
    }
    savedata.magic = SAVEDATA_MAGIC;

    EEPROM.put(0, savedata);
    EEPROM.commit();

    return CMD_OK;
}

static int do_reboot(int argc, char *argv[])
{
    ESP.restart();
    return CMD_OK;
}

static int do_led(int argc, char *argv[])
{
    if (argc < 2) {
        return CMD_ARG;
    }
    char *color = argv[1];
    uint32_t rgb = strtoul(color, NULL, 16);
    FastLED.showColor(rgb);
    return CMD_OK;
}

const cmd_t commands[] = {
    { "help", do_help, "Show help" },
    { "otaa", do_otaa, "[<dev eui> <app eui> <app key>] Show/configure OTAA parameters" },
    { "reboot", do_reboot, "Reboot ESP" },
    { "led", do_led, "<RRGGBB> Set the LED to a specific value (hex)" },
    { NULL, NULL, NULL }
};

static int do_help(int argc, char *argv[])
{
    show_help(commands);
    return CMD_OK;
}

void setup(void)
{
    Serial.begin(115200);
    printf("\nWeMos-LoRa-node\n");
    EditInit(line, sizeof(line));

    // switch
    pinMode(PIN_SWITCH, INPUT_PULLUP);

    // LED init
    FastLED.addLeds < WS2812B, PIN_LED_RGB, RGB > (leds, 2).setCorrection(TypicalSMD5050);

    // init config
    EEPROM.begin(sizeof(savedata));
    EEPROM.get(0, savedata);

    // LMIC init
    SPI.begin();
#ifndef NO_HARDWARE_PRESENT
    os_init();
    LMIC_reset();
    LMIC_registerEventCb(onEventCallback, NULL);
    if (savedata.magic == SAVEDATA_MAGIC) {
        LMIC_startJoining();
    }
#endif                          /* NO_HARDWARE_PRESENT */
    FastLED.showColor(CRGB::Black);
}

void loop(void)
{
    // parse command line
    bool haveLine = false;
    if (Serial.available()) {
        char c;
        haveLine = EditLine(Serial.read(), &c);
        Serial.write(c);
    }
    if (haveLine) {
        int result = cmd_process(commands, line);
        switch (result) {
        case CMD_OK:
            printf("OK\n");
            break;
        case CMD_NO_CMD:
            break;
        case CMD_ARG:
            printf("Invalid arguments\n");
            break;
        case CMD_UNKNOWN:
            printf("Unknown command, available commands:\n");
            show_help(commands);
            break;
        default:
            printf("%d\n", result);
            break;
        }
        printf(">");
    }

#ifndef NO_HARDWARE_PRESENT
    // do something once in a while, e.g transmit
    static int last_period = -1;
    int period = millis() / 20000;
    if (period != last_period) {
        last_period = period;
        if ((LMIC.opmode & (OP_TXDATA | OP_TXRXPEND)) == 0) {
            uint8_t buf[1];
            buf[0] = 0;
            // Prepare upstream data transmission at the next possible time.
            LMIC_setTxData2(1, buf, sizeof(buf), 0);
        }
    }

    // run LoRa stack
    os_runloop_once();
#endif                          /* NO_HARDWARE_PRESENT */
}
