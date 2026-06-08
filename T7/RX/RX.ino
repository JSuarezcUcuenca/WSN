#include "LoRaWan_APP.h"

#include "Arduino.h"

#include "HT_SSD1306Wire.h"



extern SSD1306Wire display;



#define RF_FREQUENCY            917500000 

#define LORA_BANDWIDTH          2         

#define LORA_CODINGRATE         4         

#define LORA_PREAMBLE_LENGTH    8

#define LORA_SYMBOL_TIMEOUT     0

#define LORA_FIX_LENGTH_PAYLOAD_ON  false

#define LORA_IQ_INVERSION_ON    false



#define BUFFER_SIZE             40 

char rxpacket[BUFFER_SIZE];



const int PIN_BOTON = 0; 



static RadioEvents_t RadioEvents;

bool lora_idle = true;



// Variables de Control Cíclico de SF (Sincronizado con el TX manual)

uint8_t listaSF[] = {7, 9, 12};

uint8_t indiceSF = 0; 



// Variables de Adquisición Acumulativa

uint32_t pktsReceived = 0;

uint32_t expectedPktCount = 0;

float pdr = 100.0;

float rssiSum = 0;

float snrSum = 0;



String ultimaTemp = "--.-";

String batPorcentajeTX = "100.0";

String batHorasTX = "--.-";



void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr);



void aplicarConfiguracionEscucha() {

  Radio.Sleep();

  

  // Reinicio total del hardware para limpiar la memoria intermedia del SX1262

  Radio.Init(&RadioEvents);

  Radio.SetChannel(RF_FREQUENCY);

  

  // CONFIGURACIÓN CORREGIDA: Subimos el Timeout de símbolos a 20 y ponemos IQ Inversion en FALSE (Coincidiendo con el TX)

  Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, listaSF[indiceSF],

                    LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,

                    20, LORA_FIX_LENGTH_PAYLOAD_ON, 

                    0, true, 0, 0, false, true); // <-- El antepenúltimo parámetro se cambia a false de forma explícita

  

  // Reseteo de métricas para el nuevo Hito

  pktsReceived = 0;

  expectedPktCount = 0;

  rssiSum = 0;

  snrSum = 0;

  pdr = 100.0;

  

  display.clear();

  display.drawString(0, 0, "LORA: ON | ESCUCHA");

  display.drawString(0, 20, "Modo Activo: SF" + String(listaSF[indiceSF]));

  display.drawString(0, 40, "Esperando ráfaga...");

  display.display();

  

  Serial.printf("\r\n[RADIO-RX] Escucha en SF%d sincronizada sin inversión de IQ.\r\n", listaSF[indiceSF]);

  lora_idle = true; 

}



void setup() {

  Serial.begin(115200);

  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);

  

  pinMode(PIN_BOTON, INPUT_PULLUP);

  

  RadioEvents.RxDone = OnRxDone;

  Radio.Init(&RadioEvents);

  Radio.SetChannel(RF_FREQUENCY);

  

  pinMode(Vext, OUTPUT);

  digitalWrite(Vext, LOW);

  delay(100);

  

  display.init();

  display.flipScreenVertically();

  display.setFont(ArialMT_Plain_10);



  aplicarConfiguracionEscucha();

}



void loop() {

  // Comprobar si el estudiante pide cambiar el SF en el receptor

  if (digitalRead(PIN_BOTON) == LOW) {

    delay(200);

    if (digitalRead(PIN_BOTON) == LOW) {

      indiceSF = (indiceSF + 1) % 3;

      aplicarConfiguracionEscucha();

      delay(800);

    }

  }



  if (lora_idle) {

    lora_idle = false;

    Radio.Rx(0); 

  }

  Radio.IrqProcess();

}



void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {

  Radio.Sleep();

  

  memcpy(rxpacket, payload, size);

  rxpacket[size] = '\0';

  String rxString = String((char*)rxpacket);



  int primeraComa = rxString.indexOf(',');

  int segundaComa = rxString.indexOf(',', primeraComa + 1);

  int terceraComa = rxString.indexOf(',', segundaComa + 1);



  if (primeraComa > 0 && segundaComa > primeraComa && terceraComa > segundaComa) {

    String pktIDStr = rxString.substring(0, primeraComa);

    ultimaTemp = rxString.substring(primeraComa + 1, segundaComa);

    batPorcentajeTX = rxString.substring(segundaComa + 1, terceraComa);

    batHorasTX = rxString.substring(terceraComa + 1);

    

    uint32_t currentPktID = pktIDStr.toInt();

    if (currentPktID > expectedPktCount) {

      expectedPktCount = currentPktID;

    }

  }



  pktsReceived++;

  rssiSum += rssi;

  snrSum += snr;



  if (expectedPktCount > 0) {

    pdr = ((float)pktsReceived / (float)expectedPktCount) * 100.0;

  }



  display.clear();

  display.drawString(0, 0,  "SF" + String(listaSF[indiceSF]) + " | RX T:" + ultimaTemp + " C");

  display.drawString(0, 16, "PDR: " + String(pdr, 1) + " % | Pkts: " + String(pktsReceived));

  display.drawString(0, 32, "RSSI: " + String(rssi) + "dBm | SNR: " + String(snr) + "dB");

  display.drawString(0, 48, "BatTX: " + batPorcentajeTX + "% | Rst: " + batHorasTX + "h");

  display.display();



  // Python lee el SF dinámico directo desde la cadena de texto serial

  Serial.print("SF");

  Serial.print(listaSF[indiceSF]);

  Serial.print(",");

  Serial.print(expectedPktCount);

  Serial.print(",");

  Serial.print(ultimaTemp);

  Serial.print(",");

  Serial.print(rssi);

  Serial.print(",");

  Serial.print(snr);

  Serial.print(",");

  Serial.print(pdr, 1);

  Serial.print(",");

  Serial.println(batHorasTX);



  lora_idle = true; 

} 


#!/usr/bin/env python3
import serial
import time
import csv
import os

PUERTO_RX = '/dev/ttyUSB0'  # Revisa cuál es con: ls /dev/ttyUSB*
BAUD_RATE = 115200
ARCHIVO_REPORTE = 'reporte_frecuencia_hopping.csv'

print("=== RECEPTOR Y MONITOR DE MÉTRICAS LoRa ===")
os.system(f"sudo chmod 666 {PUERTO_RX}")

# Crear la cabecera del archivo de reporte si no existe
if not os.path.exists(ARCHIVO_REPORTE):
    with open(ARCHIVO_REPORTE, mode='w', newline='', encoding='utf-8') as f:
        escritor = csv.writer(f)
        escritor.writerow(['Timestamp', 'Frecuencia_Canal', 'Seq_Esperada', 'Pkts_Recibidos', 'Msg_ID', 'Temp_C', 'Hum_Percent', 'RSSI_dBm', 'SNR_dB', 'PDR_Percent'])

try:
    ser_rx = serial.Serial(PUERTO_RX, BAUD_RATE, timeout=1)
    ser_rx.flushInput()
    print(f"[OK] Escuchando medio... Las métricas se guardarán en: {ARCHIVO_REPORTE}")
    print("-" * 100)
    print(f"{'TIME':<10} | {'FREQ':<7} | {'SEQ':<5} | {'RX':<4} | {'ID_MSG':<6} | {'RSSI':<5} | {'SNR':<4} | {'PDR':<6}")
    print("-" * 100)
    
    while True:
        if ser_rx.in_waiting > 0:
            linea = ser_rx.readline().decode('utf-8', errors='ignore').strip()
            
            # Procesar únicamente las cadenas que contienen datos de canal válidos
            if linea.startswith("CH_"):
                datos = linea.split(',')
                if len(datos) == 9:
                    timestamp = time.strftime('%H:%M:%S')
                    
                    # Imprimir en consola ordenadamente para el control de las Fases del laboratorio
                    print(f"{timestamp:<10} | {datos[0]:<7} | {datos[1]:<5} | {datos[2]:<4} | {datos[3]:<6} | {datos[6]:<5} | {datos[7]:<4} | {datos[8]}%")
                    
                    # Almacenar en el CSV de resultados cronológicos
                    with open(ARCHIVO_REPORTE, mode='a', newline='', encoding='utf-8') as f:
                        escritor = csv.writer(f)
                        escritor.writerow([
                            timestamp, datos[0], datos[1], datos[2], 
                            datos[3], datos[4], datos[5], datos[6], 
                            datos[7], datos[8]
                        ])
                        
except serial.SerialException:
    print(f"[ERROR] No se pudo abrir el puerto {PUERTO_RX}. Verifica la conexión.")
except KeyboardInterrupt:
    print(f"\n[INFO] Monitoreo finalizado. Datos consolidados en: {ARCHIVO_REPORTE}")
