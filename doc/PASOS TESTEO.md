# Guía de Pruebas: Decodificación FT8 en ESP32

Este documento detalla los pasos para verificar que el sistema es capaz de escuchar, procesar y decodificar señales FT8.

## 1. Prueba con Archivo WAV (Modo Test)

Esta prueba utiliza un archivo de audio real (`test/191111_110130.wav`) embebido en la memoria del ESP32 para verificar la decodificación sin necesidad de hardware de radio.

### Pasos:

1.  **Activar Modo Test:**

    - Abre `main/MAP-FT8_main.c`.
    - Asegúrate de que la línea `#define TEST_MODE_WAV` esté **descomentada**.
    - _(Si estaba comentada, descoméntala y guarda)._

2.  **Compilar y Flashear:**

    ```bash
    idf.py build flash monitor
    ```

3.  **Verificar Salida:**
    - El ESP32 iniciará y mostrará: `MODO TEST ACTIVADO: Leyendo audio desde memoria flash`.
    - Verás el ciclo de captura (acelerado) y luego la decodificación.
    - **Resultado Esperado:**
      Deberías ver mensajes decodificados del archivo WAV. Ejemplo:
      ```
      I (xxx) FT8_DECODE: MENSAJE: CQ DX ... | SNR: ...
      ```
    - El archivo se reproducirá en bucle.

---

## 2. Prueba con Hardware (Micrófono/Radio)

Para probar con audio en vivo.

### Pasos:

1.  **Desactivar Modo Test:**

    - Abre `main/MAP-FT8_main.c`.
    - **Comenta** la línea `// #define TEST_MODE_WAV`.

2.  **Configuración de Hardware:**

    - Conectar la salida de audio (PC o Celular) al **GPIO 34** (ADC1_CH6).
    - **Importante:** La señal debe estar acondicionada (offset DC de ~1.65V).
    - Conectar **GND** común.

3.  **Compilar y Flashear:**

    ```bash
    idf.py build flash monitor
    ```

4.  **Ejecución:**
    - Reproduce audio FT8 cerca del micrófono o inyéctalo por cable.
    - Observa los logs de `FT8_DECODE`.
