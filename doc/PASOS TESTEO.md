# Guía de Pruebas: Decodificación FT8 en ESP32

Este documento detalla los pasos para verificar que el sistema es capaz de escuchar, procesar y decodificar señales FT8.

## 1. Configuración de Hardware

Para esta prueba, necesitamos ingresar audio al ESP32.

- **Entrada de Audio (ADC):**
  - Conectar la salida de audio (PC o Celular) al **GPIO 34** (ADC1_CH6).
  - **Importante:** La señal debe estar acondicionada (offset DC de ~1.65V si es acoplamiento directo, o usar un circuito de bias). Si usas un módulo de micrófono (ej: MAX9814 o MAX4466), conecta su salida directamente al GPIO 34.
  - Conectar **GND** común entre la fuente de audio y el ESP32.

## 2. Preparación del Software

1.  Asegúrate de tener el proyecto limpio y compilado:
    ```bash
    idf.py build
    ```
2.  Flashea el ESP32:
    ```bash
    idf.py -p COMx flash monitor
    ```
    _(Reemplaza COMx con tu puerto serial)_

## 3. Ejecución de la Prueba

Una vez que el monitor serial esté corriendo, verás logs indicando el inicio de la tarea.

1.  **Generar Audio FT8:**

    - Usa una aplicación generadora de FT8 (como _WSJT-X_ en PC o _FT8CN_ en Android).
    - O reproduce un video de prueba de audio FT8 en YouTube (busca "FT8 audio sample").
    - Asegúrate de que el volumen sea adecuado (ni muy bajo que no se detecte, ni muy alto que sature el ADC).

2.  **Observar el Ciclo:**
    El ESP32 entrará en un bucle infinito con las siguientes fases:

    - **Fase 1: Captura**

      ```
      I (xxx) FT8_TASK: Capturando audio...
      ```

      Durará aproximadamente 14 segundos.

    - **Fase 2: Decodificación**

      ```
      I (xxx) FT8_TASK: Decodificando...
      ```

      Aquí el LED (si hubiera) o el log indicará actividad de CPU intensa.

    - **Resultado:**
      Si la señal es decodificada correctamente, verás:
      ```
      I (xxx) FT8_TASK: Candidatos encontrados: N
      I (xxx) FT8_DECODE: MENSAJE: CQ DX LU1AAA ... | SNR: X | DT: Y.YY
      ```

## 4. Solución de Problemas

- **No encuentra candidatos (0 candidatos):**

  - El volumen de entrada puede ser muy bajo o muy alto (clipping). Ajusta el volumen de la fuente.
  - La señal puede tener mucho ruido.
  - Verifica la conexión al GPIO 34.

- **Errores de memoria:**

  - Si ves errores de `malloc` o `stack overflow`, el ESP32 se reiniciará. Reporta esto para ajustar los tamaños de buffer.

- **Watchdog Timer (WDT) Reset:**
  - Si la decodificación tarda demasiado (más de unos segundos sin ceder control), el WDT podría dispararse. La tarea actual tiene `vTaskDelay` al final, pero el procesamiento es intensivo.
