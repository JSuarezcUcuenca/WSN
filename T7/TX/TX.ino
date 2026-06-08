#include "LoRaWan_APP.h"
#include "Arduino.h"
#include "HT_SSD1306Wire.h"

extern SSD1306Wire display;

// --- CONFIGURACIÓN BASE DEL LABORATORIO ---
#define RF_FREQUENCY 917500000 // Canal 2 (917.5 MHz)
#define TX_OUTPUT_POWER 14     // 14 dBm asignado por la guía
#define LORA_BANDWIDTH 2       // 2 = 500 kHz
#define LORA_CODINGRATE 1      // 1 = 4/5
#define LORA_PREAMBLE_LENGTH 8
#define LORA_FIX_LENGTH_PAYLOAD_ON false
#define LORA_IQ_INVERSION_ON false

#define BUFFER_SIZE 40
char txpacket[BUFFER_SIZE];

// Pin del botón USER integrado en la placa Heltec V3
const int PIN_BOTON = 0;

// Variables de Control de Estado y Muestra
uint32_t packetCounter = 1;
const uint32_t MAX_PAQUETES = 100;
unsigned long lastSendTime = 0;
bool lora_idle = true;

// Arreglo cíclico de Spreading Factors exigidos por la guía
uint8_t listaSF[] = {7, 9, 12};
uint8_t indiceSF = 0; // Inicia en SF7

// --- PARÁMETROS MATEMÁTICOS DE BATERÍA ---
const float CAPACIDAD_INICIAL = 1200.0;
float capacidadRestante = CAPACIDAD_INICIAL;

unsigned long tiempoAnteriorMillis = 0;

float obtenerConsumoPromedioMA(uint8_t sf)
{
    if (sf == 7) return 10.4;
    if (sf == 9) return 14.8;
    return 25.6;
}

void OnTxDone(void);
void OnTxTimeout(void);

static RadioEvents_t RadioEvents;

void aplicarConfiguracionRadio()
{
    Radio.Sleep();

    // Reinicio completo de la radio para asegurar que carguen los registros de SF12
    Radio.Init(&RadioEvents);
    Radio.SetChannel(RF_FREQUENCY);

    Radio.SetTxConfig(
        MODEM_LORA,
        TX_OUTPUT_POWER,
        0,
        LORA_BANDWIDTH,
        listaSF[indiceSF],
        LORA_CODINGRATE,
        LORA_PREAMBLE_LENGTH,
        LORA_FIX_LENGTH_PAYLOAD_ON,
        true,
        0,
        0,
        false,
        3000);

    packetCounter = 1; // Reinicia la ráfaga de 100 para el nuevo SF

    Serial.printf(
        "\r\n[RADIO-TX] Cambiado con éxito a SF%d. Reiniciando ráfaga...\r\n",
        listaSF[indiceSF]);
}

void setup()
{
    Serial.begin(115200);

    Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);

    pinMode(PIN_BOTON, INPUT_PULLUP);

    RadioEvents.TxDone = OnTxDone;
    RadioEvents.TxTimeout = OnTxTimeout;

    Radio.Init(&RadioEvents);
    Radio.SetChannel(RF_FREQUENCY);

    aplicarConfiguracionRadio();

    pinMode(Vext, OUTPUT);
    digitalWrite(Vext, LOW);

    delay(100);

    display.init();
    display.flipScreenVertically();
    display.setFont(ArialMT_Plain_10);

    tiempoAnteriorMillis = millis();
}

void loop()
{
    // 1. Monitorear el botón USER (Detección de flanco de bajada)
    if (digitalRead(PIN_BOTON) == LOW)
    {
        delay(200); // Anti-rebote por software

        if (digitalRead(PIN_BOTON) == LOW)
        {
            indiceSF = (indiceSF + 1) % 3;

            aplicarConfiguracionRadio();

            display.clear();
            display.drawString(0, 20, "CAMBIANDO CONFIG...");
            display.drawString(
                0,
                35,
                "Nuevo: SF" + String(listaSF[indiceSF]));
            display.display();

            delay(800);
        }
    }

    // 2. Cálculo de consumo de batería en base al tiempo real
    unsigned long tiempoActualMillis = millis();

    float horasTranscurridas =
        (tiempoActualMillis - tiempoAnteriorMillis) / 3600000.0;

    tiempoAnteriorMillis = tiempoActualMillis;

    float consumoActualMA =
        obtenerConsumoPromedioMA(listaSF[indiceSF]);

    capacidadRestante -= (consumoActualMA * horasTranscurridas);

    if (capacidadRestante < 0)
        capacidadRestante = 0;

    float porcentajeBat =
        (capacidadRestante / CAPACIDAD_INICIAL) * 100.0;

    float horasAutonomia =
        capacidadRestante / consumoActualMA;

    // 3. Proceso cíclico de envío limitado a 100 muestras
    if (packetCounter <= MAX_PAQUETES)
    {
        if (lora_idle && (millis() - lastSendTime > 2500))
        {
            // 2.5 segundos para dar margen al ToA de SF12

            lastSendTime = millis();

            lora_idle = false;

            float temp = random(180, 260) / 10.0;

            sprintf(
                txpacket,
                "%d,%.1f,%.1f,%.1f",
                packetCounter,
                temp,
                porcentajeBat,
                horasAutonomia);

            display.clear();
            display.drawString(0, 0, "=== ENVIANDO LORA TX ===");
            display.drawString(
                0,
                16,
                "Config: SF" +
                String(listaSF[indiceSF]) +
                " | BW 500k");

            display.drawString(
                0,
                32,
                "Temp: " +
                String(temp, 1) +
                " C | Pkt: " +
                String(packetCounter));

            display.drawString(
                0,
                48,
                "Bat: " +
                String(porcentajeBat, 1) +
                "% | Aut: " +
                String(horasAutonomia, 1) +
                "h");

            display.display();

            // TRUCO DE ESTABILIZACIÓN REQUERIDO PARA SF12
            Radio.Standby();
            delay(50);

            Serial.printf(
                "\r\nEnviando paquete: \"%s\" en SF%d\r\n",
                txpacket,
                listaSF[indiceSF]);

            Radio.Send(
                (uint8_t *)txpacket,
                strlen(txpacket));

            packetCounter++;
        }
    }
    else
    {
        static uint8_t ultimoSFMostrado = 0;

        if (ultimoSFMostrado != listaSF[indiceSF])
        {
            display.clear();
            display.drawString(0, 0, "=== HITO COMPLETADO ===");
            display.drawString(
                0,
                20,
                "Finalizado SF" +
                String(listaSF[indiceSF]) +
                " (100 Pkts)");

            display.drawString(
                0,
                40,
                "Presione USER para sgte");

            display.display();

            ultimoSFMostrado = listaSF[indiceSF];
        }
    }

    Radio.IrqProcess();
}

void OnTxDone(void)
{
    lora_idle = true;
}

void OnTxTimeout(void)
{
    Radio.Sleep();
    lora_idle = true;
}