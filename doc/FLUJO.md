# Flujo de Ejecución: Modo Test (WAV Embebido)

Este documento explica cómo fluyen los datos cuando el sistema está compilado en modo de prueba (`TEST_MODE_WAV`).

## Diagrama de Flujo

```mermaid
graph TD
    A[Inicio ft8_task] --> B{Modo Test Activo?}
    B -- Sí --> C[Puntero a Flash Memory]
    B -- No --> D[Inicializar ADC/DMA]

    C --> E[Bucle Principal]

    subgraph "Fase 1: Captura y Procesamiento (14s)"
        E --> F{Faltan Bloques?}
        F -- Sí --> G[Leer Chunk de Flash]
        G --> H[Copiar a Buffer de Audio]
        H --> I[Aplicar Ventana Hanning]
        I --> J[Ejecutar FFT (KissFFT)]
        J --> K[Calcular Magnitud dB]
        K --> L[Guardar en Waterfall]
        L --> F
    end

    F -- No --> M[Fase 2: Decodificación]

    subgraph "Fase 2: Decodificación (~1s)"
        M --> N[ft8_find_sync]
        N --> O{Candidatos?}
        O -- Sí --> P[ft8_decode]
        P --> Q[Imprimir Mensaje Serial]
        Q --> O
        O -- No --> R[Fin Ciclo]
    end

    R --> E
```

## Descripción Detallada

### 1. Origen de Datos (Source)

En lugar de tomar muestras del mundo real a través del micrófono (ADC), el sistema utiliza un archivo de audio `.wav` que ha sido **embebido** dentro del propio chip ESP32 durante la compilación.

- **Ubicación:** Memoria Flash (Program Memory).
- **Acceso:** A través de punteros generados por el linker (`_binary_..._start`).
- **Ventaja:** Permite probar el algoritmo de procesamiento con una señal conocida, limpia y repetible, eliminando el ruido eléctrico o problemas de hardware como variables.

### 2. Simulación de Tiempo Real

El protocolo FT8 depende críticamente del tiempo (slots de 15 segundos).

- Como leer de la memoria es instantáneo (microsegundos), si no hiciéramos nada, el ESP32 procesaría los 15 segundos de audio en una fracción de segundo.
- Para simular el comportamiento real, introducimos un `vTaskDelay` artificial después de procesar cada bloque. Esto permite verificar que el sistema no se bloquea y que la carga de CPU es manejable.

### 3. Procesamiento (DSP)

El procesamiento matemático es **idéntico** al modo real:

1.  **FFT:** Convierte el audio (tiempo) a frecuencias.
2.  **Waterfall:** Construye una imagen espectral del tiempo vs frecuencia.
3.  **Decodificación:** Busca los patrones de sincronización (Costas arrays) y extrae los bits del mensaje.

### 4. Salida

Los mensajes decodificados se envían por el puerto serial (UART) a la PC, donde se pueden ver con `idf.py monitor`.
