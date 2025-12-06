# Informe de Proyecto: Módem Autónomo para Modos Digitales (FT8)

## 1. Resumen

Este proyecto presenta el desarrollo de un módem autónomo basado en el microcontrolador ESP32 capaz de decodificar y codificar señales del protocolo FT8, utilizado en radioafición para comunicaciones de largo alcance y baja potencia. El sistema elimina la necesidad de una computadora personal (PC), integrando todo el procesamiento digital de señales (DSP) y ofreciendo una interfaz de control inalámbrica accesible desde cualquier dispositivo móvil (smartphone/tablet) vía WiFi.

## 2. El Problema Atacado, Objetivos y Motivaciones

**Problema:** La práctica tradicional de modos digitales en radioafición requiere un transceptor de radio conectado a una PC mediante interfaces de audio y control, sumado a múltiples cables. Esto resulta en una configuración voluminosa, compleja de instalar y poco práctica para operaciones portables o de emergencia (SOTA/POTA).

**Objetivos:**

- **Portabilidad:** Condensar la capacidad de procesamiento de una PC en un dispositivo de bolsillo, facilitando su transporte y despliegue en cualquier ubicación, ideal para operaciones de campo.
- **Autonomía:** Lograr un funcionamiento completamente independiente de una computadora personal, utilizando únicamente alimentación estándar (USB/Batería) para maximizar la flexibilidad operativa.
- **Usabilidad:** Desarrollar una interfaz de usuario moderna, intuitiva y responsiva, accesible a través de un navegador web desde cualquier dispositivo móvil (smartphone/tablet) sin necesidad de instalar software adicional.

**Motivación:** Facilitar el acceso a las comunicaciones digitales globales a radioaficionados que gustan de realizar operaciones de campo o actividades similares, reduciendo la barrera de entrada en términos de equipamiento y complejidad técnica. El proyecto está pensado para ofrecer un dispositivo de bajo coste y fácil acceso, con la visión de ser software libre, fácilmente configurable y de armar por cualquiera con mínimos conocimientos técnicos, permitiendo a los operadores realizar comunicaciones digitales de manera eficiente y cómoda.

## 3. Plataforma de Software y Hardware Adoptados

**Hardware:**

- **SoC:** Espressif **ESP32** (Dual Core 240MHz) por su capacidad de cómputo, conectividad WiFi/Bluetooth y bajo costo.
- **Audio:** Periférico **I2S** integrado con conversores ADC/DAC internos para la digitalización y síntesis de señales de audio.

**Software:**

- **Framework:** **ESP-IDF** (basado en **FreeRTOS**) para un control granular de tareas en tiempo real.
- **DSP:** Implementación optimizada de algoritmos de Transformada Rápida de Fourier (FFT), filtros y decodificación LDPC/Costas en C.
- **Interfaz:** Servidor Web embebido y **WebSockets** para comunicación full-duplex de baja latencia. Frontend desarrollado en HTML5, CSS3 y JavaScript puro, con soporte para **WebRTC** para transmisión de audio en tiempo real.

## 4. Arquitectura Adoptada

Se diseñó una arquitectura de firmware multi-tarea que aprovecha los dos núcleos del ESP32:

1.  **Núcleo 0 (Radio & DSP):** Dedicado a tareas críticas de tiempo real. Gestiona la adquisición de audio vía drivers I2S (DMA) y ejecuta la cadena de decodificación FT8 (sincronización temporal, demodulación AFSK, corrección de errores).
2.  **Núcleo 1 (Conectividad & UI):** Maneja la pila WiFi y el servidor HTTP/WebSocket.
3.  **Sincronización:** El sistema implementa una gestión estricta de "slots" de tiempo de 15 segundos (mandatorio en FT8), utilizando sincronización NTP y mecanismos de ajuste de latencia (RTT) con el navegador cliente.

La interfaz web ofrece una visualización avanzada tipo "Waterfall" (cascada espectral), permitiendo al usuario operar el modo FT8 completamente desde su teléfono celular con una experiencia fluida, robusta y moderna.

## 5. Planes a Futuro

- **Soporte para Múltiples Modos Digitales:** Extender la funcionalidad para incluir otros modos digitales populares como JS8Call, WSPR, RTTY, etc.
- **Integración con GPS:** Añadir un módulo GPS para una sincronización de tiempo más precisa y para habilitar funciones de geolocalización para operaciones portables.
- **Mejoras en la Interfaz de Usuario:** Implementar características adicionales en la interfaz web, como un logbook integrado, control de transceptor (CAT) básico y opciones de personalización avanzadas.
- **Optimización de Consumo Energético:** Reducir aún más el consumo de energía para prolongar la duración de la batería en operaciones de campo.
- **Desarrollo de una Carcasa Impresa en 3D:** Diseñar y ofrecer planos para una carcasa compacta y robusta que proteja el dispositivo y mejore su portabilidad.

## 6. Faltantes del Proyecto

Debido a las limitaciones de tiempo en la fase de desarrollo, algunas funcionalidades y mejoras planificadas no pudieron ser implementadas, quedando como tareas pendientes para futuras iteraciones:

-   **Aislamiento galvánico para la interfaz de audio:** La implementación de un aislamiento galvánico completo entre el ESP32 y el transceptor de radio para eliminar bucles de tierra y ruido no se pudo integrar en esta fase.
-   **Pruebas de rendimiento y estabilidad a largo plazo en condiciones de campo:** Las pruebas se centraron en la funcionalidad principal, pero un análisis exhaustivo de la estabilidad y el consumo energético en operaciones prolongadas en exteriores no se pudo realizar.
-   **Logbook integrado en la interfaz web:** La capacidad de registrar automáticamente los contactos realizados directamente desde la interfaz de usuario, con exportación a formatos estándar (ej. ADIF), no fue implementada.
-   **Control CAT (Computer Aided Transceiver) completo:** Aunque se contempla un control básico y ajeno al transceptor en sí, la integración de un protocolo CAT robusto y compatible con una amplia gama de transceptores es una característica que se planea implementar en futuras iteraciones.
-   **Soporte inicial para modos digitales adicionales:** La prioridad fue asegurar la funcionalidad completa de FT8. La inclusión de otros modos como JS8Call o WSPR desde el lanzamiento inicial no fue posible.
-   **Documentación exhaustiva para el ensamblaje y la configuración:** Si bien se proveerá una guía básica, una documentación detallada paso a paso para el montaje del hardware y la configuración del software queda pendiente.
