# Resumen del Proyecto: Módem FT8 Autónomo con ESP32

## ¿Qué es este proyecto?

Este proyecto es un **dispositivo portátil y autónomo** que permite a los radioaficionados comunicarse usando el modo digital **FT8**, sin necesidad de una computadora. Piensa en él como un "traductor inteligente" entre una radio tradicional y el mundo digital, todo contenido en un pequeño chip ESP32.

## ¿Para qué sirve?

Normalmente, para usar modos digitales como FT8 en radioafición, necesitas:
- Una radio (transceptor)
- Una computadora con software especializado (WSJT-X)
- Cables y adaptadores complejos
- Conocimientos técnicos avanzados

**Este proyecto elimina la computadora** y simplifica todo el sistema. Solo necesitas:
- Tu radio
- Este dispositivo (ESP32 + circuitos de audio)
- Tu teléfono móvil (para controlar el sistema vía WiFi)

## ¿Qué es FT8 y por qué es especial?

FT8 es un **protocolo de comunicación digital** diseñado para trabajar con señales extremadamente débiles. Imagina poder comunicarte con alguien al otro lado del mundo usando menos potencia que una bombilla LED, incluso cuando la señal es tan débil que un humano no podría escucharla.

**Características clave:**
- Transmisiones de **15 segundos** (muy cortas)
- Requiere **sincronización de tiempo precisa** (milisegundos)
- Usa matemática avanzada para **corregir errores** automáticamente
- Funciona en condiciones donde otros modos fallan

## ¿Cómo está integrado el sistema?

El proyecto se compone de **cuatro subsistemas principales** que trabajan juntos:

### 1. **Núcleo de Procesamiento (ESP32)**
- **Cerebro del sistema**: Microcontrolador de doble núcleo
- **Sistema Operativo**: FreeRTOS (permite hacer múltiples tareas simultáneamente)
- **Memoria**: Suficiente para procesar audio en tiempo real (~83 KB para datos)

### 2. **Subsistema de Audio**
Maneja la entrada y salida de sonido:

- **Entrada (Recepción)**:
  - Conectado al puerto de audio de la radio
  - Convierte el sonido analógico en números digitales (ADC)
  - Muestrea a 12,000 veces por segundo (12 kHz)
  - Usa **DMA** (acceso directo a memoria) para no sobrecargar el procesador

- **Salida (Transmisión)**:
  - Genera tonos de audio para transmitir
  - Convierte números digitales en sonido analógico (DAC)
  - Activa la radio usando su función VOX (activación por voz)

- **Aislamiento**: Transformadores de audio para proteger el equipo

### 3. **Motor de Decodificación FT8**
El "corazón matemático" del sistema:

**Fase 1 - Análisis de Frecuencias (FFT)**:
- Toma bloques de audio de 0.16 segundos (1920 muestras)
- Aplica una "ventana" matemática (Hanning) para suavizar los bordes
- Usa **FFT** (Transformada Rápida de Fourier) para convertir el sonido en un espectro de frecuencias
- Genera un "waterfall" (cascada): una imagen visual de cómo cambian las frecuencias en el tiempo

**Fase 2 - Búsqueda de Señales**:
- Busca patrones especiales llamados "Costas arrays" (marcas de sincronización)
- Identifica hasta 10 candidatos de señales FT8

**Fase 3 - Decodificación**:
- Para cada candidato, intenta extraer el mensaje
- Usa **LDPC** (corrección de errores avanzada) para recuperar datos corruptos
- Verifica con **CRC** (suma de verificación) que el mensaje es válido

### 4. **Interfaz Web WiFi**
Control remoto desde tu teléfono:

- **Punto de Acceso WiFi**: El ESP32 crea su propia red WiFi
- **Servidor Web**: Sirve una página HTML/CSS/JavaScript
- **WebSockets**: Comunicación en tiempo real (ves las decodificaciones al instante)
- **Funciones**:
  - Ver mensajes decodificados en vivo
  - Configurar tu indicativo (callsign) y ubicación (grid)
  - Iniciar transmisiones
  - Monitorear el estado del sistema

## ¿Por qué se desarrolló así?

### **Decisiones de Diseño Clave:**

1. **ESP32 como plataforma**:
   - Económico (menos de $5 USD)
   - WiFi integrado (no necesita módulos externos)
   - Suficientemente potente para DSP en tiempo real
   - Doble núcleo (un núcleo para WiFi, otro para procesamiento)

2. **Librería FT8 externa** (`kgoba/ft8_lib`):
   - Implementar FT8 desde cero es extremadamente complejo
   - Esta librería está optimizada para microcontroladores
   - Probada y confiable

3. **Modo de prueba con archivos WAV embebidos**:
   - Permite probar el algoritmo sin hardware de radio
   - Archivos de audio reales grabados de transmisiones FT8
   - Embebidos en la memoria flash del chip

4. **FreeRTOS para multitarea**:
   - Permite que el WiFi, el servidor web y el procesamiento de audio corran simultáneamente
   - Prioridades configurables (el audio siempre tiene prioridad)
   - Bloqueo inteligente (no desperdicia CPU esperando datos)

## Flujo de Funcionamiento (Modo Recepción)

```
1. La radio recibe una señal → 
2. Audio entra al ESP32 (ADC) →
3. Se acumulan 15 segundos de audio →
4. FFT convierte audio a frecuencias (87 bloques) →
5. Se construye el waterfall (mapa de frecuencias vs tiempo) →
6. Se buscan patrones de sincronización →
7. Se decodifican los mensajes encontrados →
8. Los mensajes se envían a la interfaz web vía WebSocket →
9. Aparecen en tu teléfono en tiempo real
```

## Componentes del Código

### Archivos Principales:
- **`MAP-FT8_main.c`**: Punto de entrada, inicializa todo y crea las tareas
- **`audio_driver.c`**: Maneja ADC/DAC con DMA
- **`ft8_decode_task.c`**: Lógica de procesamiento FFT y decodificación
- **`web_interface/`**: Servidor HTTP, WebSockets y archivos HTML/CSS/JS
- **`lib/ft8_lib/`**: Librería matemática del protocolo FT8

### Tecnologías Utilizadas:
- **Lenguaje**: C (firmware del ESP32)
- **Framework**: ESP-IDF (SDK oficial de Espressif)
- **RTOS**: FreeRTOS
- **DSP**: KissFFT (librería de FFT optimizada)
- **Web**: HTML5, CSS3, JavaScript (WebSockets)

## Desafíos Técnicos Resueltos

1. **Procesamiento en Tiempo Real**:
   - El ESP32 debe procesar 12,000 muestras por segundo sin perder datos
   - Solución: DMA + prioridades de tareas en FreeRTOS

2. **Memoria Limitada**:
   - El waterfall completo ocupa ~83 KB
   - Solución: Optimización de tipos de datos (uint8_t en lugar de float)

3. **Sincronización de Tiempo**:
   - FT8 requiere precisión de milisegundos
   - Solución: Uso de GPS (planificado) + RTC como respaldo

4. **Balanceo de Carga**:
   - WiFi y DSP compitiendo por recursos
   - Solución: Asignación de núcleos separados y prioridades

## Estado Actual del Proyecto

✅ **Implementado**:
- Captura de audio con ADC/DMA
- Procesamiento FFT en tiempo real
- Decodificación FT8 funcional (probado con archivos WAV)
- Interfaz web con WiFi
- WebSockets para comunicación en tiempo real

🚧 **En desarrollo**:
- Generación de audio para transmisión (DAC)
- Integración con GPS para sincronización
- Módulo RTC como respaldo
- Interfaz de audio física (transformadores, conectores)

## Aplicaciones Prácticas

Este dispositivo es ideal para:
- **Radioaficionados móviles**: Operación desde el auto o campo
- **Emergencias**: Comunicaciones de largo alcance sin infraestructura
- **Experimentación**: Aprender sobre DSP y comunicaciones digitales
- **Bajo costo**: Alternativa económica a equipos comerciales ($500+ USD)

## Conclusión

Este proyecto demuestra cómo un microcontrolador económico puede realizar tareas que tradicionalmente requerían una computadora completa. Combina procesamiento digital de señales, sistemas operativos en tiempo real, desarrollo web y electrónica analógica en un solo dispositivo integrado y portable.

La clave del éxito está en la **modularidad** del diseño: cada subsistema (audio, DSP, web) puede desarrollarse y probarse independientemente, facilitando el debugging y las mejoras futuras.
