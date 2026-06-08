import serial
import time
import os
import pandas as pd
import seaborn as sns
import matplotlib.pyplot as plt

# ================= CONFIGURACIÓN DE TU ENTORNO DE PRUEBA =================
PUERTO_COM = '/dev/ttyUSB0'         # Modificar según tu puerto activo ('/dev/ttyUSB0' en Ubuntu)
BAUD_RATE = 115200
DISTANCIA_ACTUAL = 600      # ¡IMPORTANTE!: Cambiar a 50, 100, 300, 600m antes de correr cada prueba
ARCHIVO_DATOS = "resultados_laboratorio_lora1.csv"
# =========================================================================

data_buffer = []
ultimo_timestamp = None  # Almacena el tiempo del paquete anterior para el cálculo en el CMD

print(f"--- SCRIPT ADQUISICIÓN METRICAS WSN - UCUENCA ---")
print(f"Punto de captura activo: {DISTANCIA_ACTUAL} metros.")
print("Instrucciones: Cuando termines de evaluar los SFs en esta distancia,")
print("presiona [Ctrl + C] para procesar y actualizar los gráficos consolidados.\n")

try:
    ser = serial.Serial(PUERTO_COM, BAUD_RATE, timeout=1)
    time.sleep(2)
    ser.reset_input_buffer()
    
    while True:
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        if line and line.startswith("SF"):
            # Capturar marca de tiempo instantánea en milisegundos al llegar a la PC
            timestamp_actual = time.time() * 1000 
            
            try:
                # Formato: SF7,Secuencia,Temperatura,RSSI,SNR,PDR,HorasAutonomia
                parts = line.split(',')
                if len(parts) == 7:
                    sf_label = parts[0]  # E.g., "SF7", "SF9", "SF12"
                    seq = int(parts[1])
                    temp = float(parts[2])
                    rssi = float(parts[3])
                    snr = float(parts[4])
                    pdr_val = float(parts[5])
                    horas_bat = float(parts[6])
                    
                    # Calcular el intervalo real transcurrido entre llegadas de paquetes
                    if ultimo_timestamp is not None:
                        delta_tiempo = timestamp_actual - ultimo_timestamp
                    else:
                        # Estimación inicial para el primer paquete de la ráfaga (Delay TX + ToA aproximado)
                        delta_tiempo = 2500.0 + (15.8 if sf_label == "SF7" else 53.2 if sf_label == "SF9" else 526.3)
                    
                    ultimo_timestamp = timestamp_actual
                    
                    data_buffer.append({
                        'SF': sf_label,
                        'Distancia': DISTANCIA_ACTUAL,
                        'RSSI': rssi,
                        'SNR': snr,
                        'PDR': pdr_val,
                        'Autonomia_Horas': horas_bat,
                        'Delta_Tiempo_ms': delta_tiempo  # Se guarda internamente para el resumen del CMD
                    })
                    
                    # MUESTRA EL TIEMPO EN EL CMD EN TIEMPO REAL
                    print(f"[{sf_label}] Dist: {DISTANCIA_ACTUAL}m | Pkt #{seq} | RSSI: {rssi} dBm | SNR: {snr} dB | T_Arribada: {delta_tiempo:.1f} ms")
            except Exception as e:
                print(f"Error interpretando línea: {e}")
except KeyboardInterrupt:
    print("\n--- Guardando ráfaga actual y procesando tiempos en el CMD ---")

# Almacenar y unificar datos dentro del CSV maestro del grupo
if data_buffer:
    df_nueva = pd.DataFrame(data_buffer)
    if os.path.exists(ARCHIVO_DATOS):
        df_existente = pd.read_csv(ARCHIVO_DATOS)
        df_consolidado = pd.concat([df_existente, df_nueva], ignore_index=True)
    else:
        df_consolidado = df_nueva
    df_consolidado.to_csv(ARCHIVO_DATOS, index=False)
    print(f"[OK]: Datos volcados correctamente en '{ARCHIVO_DATOS}'")
else:
    if os.path.exists(ARCHIVO_DATOS):
        df_consolidado = pd.read_csv(ARCHIVO_DATOS)
    else:
        df_consolidado = None
        print("[ALERTA]: No se capturaron datos nuevos ni existe un CSV previo.")

# === REPORTE DE TIEMPOS EXCLUSIVO PARA EL CMD ===
if data_buffer:
    print("\n=====================================================================")
    print("  ANÁLISIS DE TIEMPOS DE ARRIBADA Y TOA DEDUCIDO (CR 4/8 - BW 500kHz)")
    print("=====================================================================")
    
    # Valores teóricos Semtech para payload de ~20 bytes y CR=4/8 como referencia
    toa_teoricos = {"SF7": 15.82, "SF9": 53.25, "SF12": 526.34}
    df_resumen_tiempo = df_nueva.groupby('SF')['Delta_Tiempo_ms'].mean().reset_index()
    
    for index, row in df_resumen_tiempo.iterrows():
        sf = row['SF']
        tiempo_arribo_promedio = row['Delta_Tiempo_ms']
        # Se restan los 2500ms configurados en el bucle del TX para obtener el ToA neto
        toa_experimental_deducido = tiempo_arribo_promedio - 2500.0
        teorico = toa_teoricos.get(sf, 0.0)
        
        print(f"-> En {sf}:")
        print(f"   Intervalo promedio medido en PC: {tiempo_arribo_promedio:.2f} ms")
        print(f"   ToA Neto Experimental Deducido:  {toa_experimental_deducido:.2f} ms")
        print(f"   ToA Teórico (Fórmula Semtech):    {teorico:.2f} ms\n")
    print("=====================================================================\n")

# ================= SECCIÓN DE GRAFICACIÓN DE ALTO NIVEL (TUS 4 GRÁFICAS ORIGINALES) =================
if df_consolidado is not None and not df_consolidado.empty:
    df_resumen = df_consolidado.groupby(['SF', 'Distancia']).mean().reset_index()
    
    df_resumen['SF_Num'] = df_resumen['SF'].str.extract(r'(\d+)').astype(int)
    df_resumen = df_resumen.sort_values(by=['SF_Num', 'Distancia'])

    fig, axs = plt.subplots(2, 2, figsize=(15, 11))
    sns.set_theme(style="whitegrid")
    
    colores = {"SF7": "#d62728", "SF9": "#ff7f0e", "SF12": "#1b9e77"}
    
    # Gráfico 1: RSSI vs Distancia
    sns.lineplot(data=df_resumen, x='Distancia', y='RSSI', hue='SF', palette=colores, marker='o', linewidth=2.5, ax=axs[0, 0])
    axs[0, 0].set_title('RSSI vs Distancia', fontsize=12, fontweight='bold')
    axs[0, 0].set_ylabel('RSSI (dBm)')
    axs[0, 0].set_xlabel('Distancia (m)')
    
    # Gráfico 2: SNR vs Distancia
    sns.lineplot(data=df_resumen, x='Distancia', y='SNR', hue='SF', palette=colores, marker='s', linewidth=2.5, ax=axs[0, 1])
    axs[0, 1].axhline(y=0, color='gray', linestyle='--', linewidth=1, label="SNR = 0 dB")
    axs[0, 1].set_title('SNR vs Distancia', fontsize=12, fontweight='bold')
    axs[0, 1].set_ylabel('SNR (dB)')
    axs[0, 1].set_xlabel('Distancia (m)')
    axs[0, 1].legend()
    
    # Gráfico 3: PDR (%) vs Distancia
    sns.lineplot(data=df_resumen, x='Distancia', y='PDR', hue='SF', palette=colores, marker='^', linewidth=2.5, ax=axs[1, 0])
    axs[1, 0].axhline(y=90, color='blue', linestyle=':', linewidth=1.2, label="PDR = 90%")
    axs[1, 0].set_title('Packet Delivery Ratio (PDR) vs Distancia', fontsize=12, fontweight='bold')
    axs[1, 0].set_ylabel('PDR (%)')
    axs[1, 0].set_xlabel('Distancia (m)')
    axs[1, 0].set_ylim(-5, 105)
    axs[1, 0].legend()

    # Gráfico 4: TU HEATMAP ORIGINAL E INTACTO
    try:
        df_pivot = df_resumen.pivot(index='SF', columns='Distancia', values='RSSI')
        lista_ordenada = [sf for sf in ["SF7", "SF9", "SF12"] if sf in df_pivot.index]
        df_pivot = df_pivot.reindex(lista_ordenada)
        
        sns.heatmap(df_pivot, annot=True, fmt=".1f", cmap="RdYlGn", cbar_kws={'label': 'RSSI (dBm)'},
                    linewidths=1, linecolor='white', ax=axs[1, 1])
        axs[1, 1].set_title('Heatmap RSSI (SF x Distancia)', fontsize=12, fontweight='bold')
        axs[1, 1].set_ylabel('Factor de Esparcimiento (SF)')
        axs[1, 1].set_xlabel('Distancia (m)')
    except Exception as e:
        axs[1, 1].text(0.5, 0.5, f"Esperando más hitos de distancia\npara graficar Heatmap", 
                       ha='center', va='center', fontsize=11, color='gray')
        axs[1, 1].set_title('Heatmap RSSI (SF x Distancia)', fontsize=12, fontweight='bold')

    plt.suptitle("Caracterización del Enlace Radio LoRa P2P - Universidad de Cuenca\n(915 MHz, BW=500 kHz, Tx=14 dBm)", fontsize=14, fontweight='bold')
    plt.tight_layout()
    
    plt.savefig('Caracterizacion_Completa_LoRa_UCuenca.png', dpi=300)
    print("\n[ÉXITO]: Gráficas del laboratorio exportadas como 'Caracterizacion_Completa_LoRa_UCuenca.png'")
    plt.show()
