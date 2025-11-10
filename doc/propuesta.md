# Propuesta de Proyecto Final

**Título del proyecto:** Módem Autónomo y Portable para Modos Digitales (FT8) basado en ESP32 con Interfaz Web

**Participantes:**
* Aguirre, Gonzalo - Tauro, Manuel

## Descripción del proyecto

El proyecto consiste en el diseño y construcción de un módem digital autónomo (TNC) para radioaficionados, enfocado en el modo de señal débil FT8. El sistema utilizará un microcontrolador ESP32 como unidad central de procesamiento.

A diferencia de las soluciones tradicionales que requieren una PC, este dispositivo será completamente autónomo y portable. Se conectará directamente a la entrada de micrófono y salida de audio de un transceptor de radio estándar. El sistema utilizará la función VOX (Voice-Operated Transmission) de la radio para activar la transmisión, eliminando la necesidad de interfaces CAT.

El dispositivo obtendrá la hora y la ubicación de forma precisa mediante un módulo GPS. La hora, crítica para el protocolo FT8, se mantendrá con un módulo RTC (Real-Time Clock) como respaldo en caso de pérdida de señal del GPS.

Toda la operación (monitoreo de señales decodificadas, inicio de llamadas, configuración) se gestionará a través de una interfaz web alojada en el propio ESP32, accesible desde cualquier dispositivo móvil (teléfono o tablet) conectado a su punto de acceso Wi-Fi.

## Objetivos

* **Principal:** Desarrollar un dispositivo "todo en uno", de bajo costo y portable, capaz de decodificar y codificar señales FT8 de forma autónoma.
* **Hardware:** Diseñar y construir la interfaz de audio necesaria para conectar el ESP32 a la radio, asegurando el aislamiento galvánico y la atenuación de niveles de señal correctos.
* **Software (DSP):** Implementar o adaptar las librerías de software necesarias para el procesamiento de la señal de audio (muestreo, FFT) y la lógica del protocolo FT8 (codificación/decodificación FEC).
* **Sistema (Sincronización):** Crear un subsistema de gestión de tiempo que utilice el módulo GPS como fuente primaria y el RTC como respaldo.
* **Software (Drivers):** Desarrollar el firmware para leer e interpretar los datos del GPS y gestionar el RTC.
* **Software (UI):** Implementar un servidor web en el ESP32 que proporcione una interfaz de usuario clara para monitorear, configurar y operar el módem desde un teléfono móvil.
* **Pruebas:** Validar el sistema completo realizando un contacto (QSO) real en FT8.

## Hardware disponible

* Microcontrolador ESP32 (placa de desarrollo).
* Transceptor de radioaficionado con capacidad de VOX.

## Hardware a conseguir

* **Módulo GPS:**
* **Módulo RTC:**
* **Componentes de Interfaz de Audio:**
    * 2 Transformadores de aislamiento de audio (600:600 ohm).
    * Capacitores (para acoplamiento AC).
    * Resistencias y potenciómetros (para atenuadores de entrada/salida).
* **Conectores:** Jacks de audio (3.5mm) y conectores para la radio (Ej. RJ45, DIN).
* **Prototipado:** Protoboards, cables, placa PCB para la versión final.

## Aspectos del sistema a resolver en software y firmware

1.  **DSP y Protocolo FT8:** Es el núcleo del proyecto. Implica:
    * Muestreo de audio de alta velocidad desde el ADC.
    * Implementación de un FFT (Fast Fourier Transform) o similar para analizar el espectro de audio.
    * Adaptación de una librería existente para manejar la decodificación y codificación del protocolo FT8. *Implementarlo desde cero es inviable*.
2.  **Gestión de Tiempo y Sincronización:**
    * Un "manejador de tiempo" que priorice el GPS.
    * Uso de la interrupción del GPS para alinear el reloj del sistema con una precisión de milisegundos.
    * Configuración y lectura del RTC como respaldo.
3.  **Gestión de Tareas (RTOS):**
    * El ESP32 deberá correr múltiples tareas concurrentes (Web Server, procesamiento de audio RX, generación de audio TX, lectura de GPS). Se utilizará FreeRTOS (nativo en ESP-IDF/Arduino) para gestionar las prioridades, asegurando que el DSP en tiempo real nunca sea interrumpido.
4.  **Servidor Web y UI:**
    * Crear un Access Point (AP) Wi-Fi en el ESP32.
    * Servir archivos (HTML, CSS, JS) desde la memoria flash (SPIFFS/LittleFS).
    * Usar WebSockets para una comunicación bidireccional y en tiempo real entre el teléfono y el ESP32 (para ver decodificaciones en vivo).
5.  **Control de TX/RX:**
    * Implementar la lógica de intervalos de 15 segundos.
    * Generar un "tono de preámbulo" de 100-200ms antes de la trama FT8 para activar el VOX de la radio y evitar que se corte el inicio de la transmisión.

## Debilidades del proyecto

* **Complejidad del DSP:** La implementación del protocolo FT8 (específicamente la corrección de errores LDPC) es extremadamente compleja. El proyecto depende en gran parte de una librería en C/C++ para sistemas de baja potencia, como los utilizados en la catedra. 
https://github.com/kgoba/ft8_lib/tree/95473cda53f78fff9c242e7462ac44db3539b946
* **Rendimiento en Tiempo Real:** El ESP32 debe manejar simultáneamente el Wi-Fi, el stack de TCP/IP, el servidor web y el DSP de audio en tiempo real. Balancear las prioridades de las tareas en FreeRTOS será un desafío clave.
* **Ruido y Aislamiento (Hardware):** La interfaz de audio es muy sensible. El ruido de la fuente de alimentación del ESP32 o los bucles de tierra (ground loops) pueden impedir la decodificación de señales débiles. El diseño del PCB y el aislamiento galvánico son críticos.
* **Sincronización:** La lógica para gestionar el 1PPS del GPS y mantener la sincronización a nivel de milisegundos es un desafío de software de bajo nivel.

## Fortalezas del proyecto

* **Elección del Hardware (ESP32):** El ESP32 es ideal. Es potente (dual-core), de bajo costo, y ya incluye Wi-Fi. Su SDK (ESP-IDF) y el soporte de Arduino están muy maduros.
* **Bibliotecas Existentes:** Hay excelentes librerías para las partes "secundarias" del proyecto (servidores web, manejo de GPS con `TinyGPS++`, gestión de RTC con `RTClib`), lo que permite centrar el esfuerzo en el núcleo del problema (el DSP).
* **Modularidad:** El proyecto se divide naturalmente en subsistemas independientes (Audio, GPS, RTC, UI), lo que facilita el desarrollo y la depuración por partes.
* **Alto Interés y Comunidad:** Es un proyecto muy popular en la comunidad de radioaficionados y *makers*, por lo que existe mucha documentación, esquemáticos de referencia y proyectos similares de los cuales aprender.

## Referencias

* **Software Fuente (Referencia):** WSJT-X - El software oficial de Joe Taylor, K1JT. (Fuente de la lógica del protocolo).
* **Librería GPS:** Mikal Hart - TinyGPS++ (Librería popular para parsing de NMEA).
* **Esquemáticos de Interfaz:** Buscar "Ham Radio Digital Audio Interface Schematic" (para los circuitos de aislamiento con transformadores).
* **Proyectos Similares (Inspiración):** `MicroModem` (proyecto genérico de módem en microcontroladores), `ESP32-APRS` (proyectos similares que usan el ADC/DAC del ESP32 para radio).
* **Librería para decodificación y codificación del modo digital FT8:** biblioteca ligera en lenguaje C diseñada para la codificación y decodificación de tramas FT8 y FT4, destinada principalmente para su uso experimental en microcontroladores y sistemas embebidos.
* **Paper sobre el propio modo digital:** https://wsjt.sourceforge.io/FT4_FT8_QEX.pdf