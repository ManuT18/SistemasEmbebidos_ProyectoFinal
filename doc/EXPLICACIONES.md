# Explicaciones Técnicas del Proyecto MAP-FT8

Este documento consolida la información técnica sobre la arquitectura, drivers y sistema operativo del proyecto.

## 1. Estructura General del Proyecto

El proyecto ha evolucionado de un simple "Hello World" a un sistema estructurado para procesamiento de audio en tiempo real.

- **`main/MAP-FT8_main.c`**: Es el punto de entrada y "cerebro" de la aplicación. Contiene la lógica principal y la gestión de tareas.
- **`main/audio_driver.c` y `.h`**: Capa de Abstracción de Hardware (HAL). Aísla la complejidad de los periféricos del ESP32 (ADC/DAC, DMA, registros) del resto del código.
- **`lib/ft8_lib`**: Librería de DSP (Procesamiento Digital de Señales). Contiene la matemática pura para codificar y decodificar el protocolo FT8.

---

## 2. Driver de Audio (`audio_driver.c`)

El driver maneja la entrada y salida de audio utilizando los periféricos internos del ESP32 junto con **DMA (Direct Memory Access)**. Esto es crítico para no saturar el CPU moviendo datos byte por byte.

### Diseño Unificado

Se decidió mantener un solo driver (`audio_driver`) en lugar de separar ADC y DAC porque, en este contexto, ambos forman una única entidad lógica: el subsistema de audio. Comparten configuraciones de frecuencia de muestreo y ciclo de vida.

### Funcionamiento Interno (ESP-IDF v5.x)

- **Entrada (RX) - ADC Continuous:**

  - Utiliza el **ADC1** (GPIO 34).
  - Configurado en **Modo Continuo** con DMA. El hardware toma muestras automáticamente a 12 kHz y las deposita en la memoria RAM.
  - `audio_read()`: Es la función que consume estos datos. Si no hay datos listos, bloquea la ejecución hasta que el DMA llene el buffer.

- **Salida (TX) - DAC Continuous:**
  - Utiliza el **DAC** (GPIO 25).
  - También usa DMA para leer de memoria y generar voltaje.
  - **Conversión de Bits:** El audio interno del sistema es de **16 bits** (estándar), pero el DAC del ESP32 es de **8 bits**. El driver realiza una conversión al vuelo (escalado y desplazamiento) antes de enviar los datos al hardware.

---

## 3. Integración con FreeRTOS

El ESP32 corre sobre el sistema operativo en tiempo real **FreeRTOS**. Nuestra implementación aprovecha esto para manejar el audio de forma determinista.

### Flujo de Tareas

1.  **`app_main` (Tarea de Inicialización):**

    - Es la primera tarea que se ejecuta.
    - Su responsabilidad es inicializar el hardware (`audio_driver_init`) y crear las tareas de trabajo.
    - Una vez que lanza `ft8_task`, `app_main` termina (o puede quedarse haciendo otras cosas de baja prioridad).

2.  **`ft8_task` (Tarea de Procesamiento):**
    - Creada con `xTaskCreate` con una prioridad media (5) y un stack de 4KB.
    - Es un **Bucle Infinito (`while(1)`)**, típico de sistemas embebidos. Nunca debe retornar.

### Sincronización y Bloqueo

La magia de FreeRTOS aquí está en cómo `ft8_task` interactúa con el driver:

- **Lectura Bloqueante (`audio_read`):**
  - Cuando la tarea llama a `audio_read`, no se queda haciendo un bucle "ocupado" (gastando CPU) preguntando "¿ya hay datos?".
  - En su lugar, si el buffer del ADC no tiene suficientes datos, la función pone a la tarea en estado **BLOCKED**.
  - FreeRTOS aprovecha ese tiempo para ejecutar otras tareas (como el WiFi o el stack TCP/IP).
  - En el instante en que el DMA termina de llenar el buffer, se dispara una interrupción que "despierta" a `ft8_task`, pasando a estado **READY** y luego **RUNNING**.

### Loopback (Estado Actual)

Actualmente, la tarea implementa un "eco":

1.  Espera a tener un bloque de audio del micrófono (`audio_read`).
2.  Inmediatamente lo escribe al parlante (`audio_write`).
3.  Repite.

Esto verifica que toda la cadena (Hardware -> DMA -> Driver -> FreeRTOS Task -> Driver -> DMA -> Hardware) funciona correctamente y en tiempo real.
