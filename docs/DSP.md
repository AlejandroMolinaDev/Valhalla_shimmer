# Aurora Bloom — Arquitectura DSP y decisiones de diseño

Este documento describe cómo está construido el motor de audio de Aurora Bloom y por qué se tomó
cada decisión que la especificación dejaba abierta. Todo el código DSP vive en `source/dsp/`, es
C++20 puro (sin JUCE) y se prueba con `tests/DspTests.cpp`.

## 1. Flujo de señal

```
input ─┬──────────────────────────────────────────────────── dry ──────┐
       │                                                                │
       │   (Mono: (L+R)/2 a ambos canales)                              │
       └─► u = in + g_fb · Rot2x2(fb[n-1]) ─► DiffuserNetwork ─► softClip ─► wet ─► Mix (equal-power) ─► out
                        ▲                                                │
                        │                                                ▼
                        └── DC block ◄── LoopFilters ◄── GrainPitchShifter
                                     (LowCut → HighCut → Color)
```

- El **pitch shifter está dentro del lazo**: cada recirculación vuelve a pasar por los difusores y
  por el shifter. Con +12 st aparecen octavas ascendentes acumulativas a medida que crece la
  densidad de ecos. Con Feedback = 0 la ganancia del lazo es exactamente 0 y el shifter no se oye
  (test 4: la octava queda a −127 dB respecto a la fundamental).
- **No hay predelay**: la sensación de predelay depende sólo de Diffusion (con g = 0 la red es un
  retardo puro de la longitud total de la cascada).
- Los filtros del lazo están en el camino de retorno, así que su efecto es **acumulativo**:
  con Feedback = 0 no actúan; con feedback alto, cada pasada oscurece/adelgaza más la cola.
- El procesamiento es **estrictamente por muestra**: todos los suavizadores, LFOs, granos y el
  conmutador de modos avanzan una vez por muestra. Por eso la salida es idéntica bit a bit con
  cualquier partición en bloques (test 8: bloques de 1, 7, 64, 513 y 4096 muestras → diferencia 0).

## 2. DiffuserNetwork (`DiffuserNetwork.*`, `ModulatedAllpass.h`, `ModulatedDelay.h`)

Cada canal es una cascada de N allpass de Schroeder en su forma de una sola línea de retardo:

```
v[n] = x[n] + g · v[n−D]
y[n] = −g · v[n] + v[n−D]          H(z) = (z^−D − g) / (1 − g z^−D),  |H| = 1
```

- **Diffusion = g**, limitado a 0,98 para estabilidad numérica.
  - 0,00 → cada etapa es un retardo puro; la red es un delay (L y R con longitudes ~4 % distintas:
    con feedback suena como un ping-pong).
  - ~0,618 → envolvente de ataque lento ("bloom"): la energía se reparte en el tiempo.
  - 0,80–0,91 → la densidad llega antes y el decaimiento es más largo incluso con feedback bajo.
  - > 0,91 → coloración metálica; la energía se reparte tanto que la salida en ventanas cortas es más
    baja (la difusión redistribuye energía, no la añade).
  El test *"Diffusion shapes the impulse response envelope"* verifica estas tendencias.
- **Retardo fraccional con Lagrange de 3er orden** (4 puntos). Se descartó la interpolación lineal:
  dentro de un lazo largo actúa como un low-pass modulado que apaga la cola en cada pasada.
  Retardo mínimo legal: 2 muestras.
- **Buffers potencia de 2 con máscara**, prealocados en `prepare()` para el modo más largo, Size
  100 %, la profundidad máxima de modulación y la frecuencia de muestreo actual.
- **Modulación**: un `RandomLfo` por etapa y canal (hasta 24 LFOs). Es ruido aleatorio interpolado
  con spline Catmull-Rom, no un seno; cada LFO tiene una tasa fija aleatoria entre 0,8× y 1,25× de
  Mod Rate. La profundidad es constante en muestras (0–64 @ 48 kHz, escalada por la frecuencia de
  muestreo); su efecto audible crece con Diffusion porque la señal recircula dentro de cada allpass.
- **Size** escala todas las longitudes con un factor exponencial `0,1 · 10^size` (0,1×–1×) y se
  suaviza con un one-pole de 250 ms: al moverlo en vivo se produce el típico barrido de pitch, sin
  clicks.

### Tablas de modos (`ReverbModes.h`)

Longitudes en muestras @ 48 kHz con Size = 100 %. Son primos sobre una progresión aproximadamente
geométrica; el canal derecho usa otro juego (intercalado y ~4 % más largo) para decorrelación.

| Modo | Etapas/canal | Total L / R | Entrada | Dark (Hz) | Ángulo de cruce |
|---|---|---|---|---|---|
| Mono | 12 | 1,00 s / 1,04 s | (L+R)/2 | 8800 | 0,70 rad |
| Big Stereo | 10 | 0,85 s / 0,89 s | estéreo | 9600 | 0,62 rad |
| Medium Stereo | 8 | 0,55 s / 0,57 s | estéreo | 10200 | 0,62 rad |
| Small Stereo | 5 | 0,18 s / 0,19 s | estéreo | 11000 | 0,55 rad |

**Cruce estéreo**: en el punto de inyección del feedback se aplica una rotación 2×2
(`[cos −sin; sin cos]`), que conserva la energía y reparte cada recirculación entre ambos canales
para una imagen envolvente.

**Cambio de modo en vivo**: el modo altera el número de etapas y todas las longitudes, así que no
se puede interpolar. Se hace un *duck* del wet de 25 ms → se reconfigura y se limpian las líneas →
fade-in de 25 ms. El seco no se interrumpe. (Alternativa descartada: dos redes en paralelo con
crossfade, el doble de CPU y memoria.)

## 3. GrainPitchShifter (`GrainPitchShifter.*`)

Pitch shifter de "cabezal giratorio" mejorado, dentro del lazo:

- Una línea de entrada por canal compartida por **dos voces de granos**: A lee a `2^(st/12)` y B a
  `2^(−st/12)` (modos Dual).
- Hasta 4 granos solapados por voz, ventana **Hann** (tabla de 1024 puntos interpolada).
- Grano base de **60 ms** con **jitter de ±25 %** en el tamaño, separación entre granos ≈ L/2 ± 20 %
  y desplazamiento de lectura aleatorio de hasta 10 ms. Romper la periodicidad evita el comb
  filtering de los shifters de dos cabezales y produce bandas laterales ruidosas que suenan a
  "ensemble" de cuerdas. No hay de-glitching por autocorrelación, a propósito.
- Lectura hacia delante: el retardo varía `(1 − r)` muestras por muestra; para subir (r > 1) el
  grano empieza con un retardo de `(r − 1)·L` extra para no alcanzar nunca al cabezal de escritura.
- **Reverse**: el puntero de lectura retrocede (`retardo = d0 + (1 + r)·edad`), así que cada grano
  reproduce su segmento invertido en el tiempo y transpuesto: menos transitorios, textura de órgano.
- Cada grano conserva el ratio y la dirección con la que nació. Los cambios de Pitch o de modo
  Reverse entran grano a grano y quedan fundidos por las propias ventanas → sin clicks.
- Modos como ganancias suavizadas 20 ms (`thru`, `A`, `B`):

| Pitch Mode | thru | A | B | Granos invertidos |
|---|---|---|---|---|
| Single | 0 | 1 | 0 | no |
| Dual | 0 | 0,707 (−3 dB) | 0,707 (−3 dB) | no |
| Single Reverse | 0 | 1 | 0 | sí |
| Dual Reverse | 0 | 0,707 | 0,707 | sí |
| Bypass | 1 | 0 | 0 | — |

- L y R usan semillas distintas (decorrelación). Una voz que no se usa no se calcula; al
  reactivarse empieza limpia (su ganancia sube desde 0, así que no hay saltos).
- **Compensación del solapamiento** (`OverlapMakeup` en `ShimmerEngine.h`): medido con ruido y
  senos, el solapamiento Hann de granos decorrelacionados pierde ≈ 1,2 dB de potencia por pasada,
  mientras que el contenido muy grave y coherente entre granos (< ~30 Hz, sobre todo con Pitch ≈ 0)
  pasa a ganancia ≈ 1. Una ganancia fija subiría esos subgraves por encima de 1 y el lazo dependería
  del limitador; por eso se usa un *shelf*: `y = x + 0,148 · HP60(x)` (+1,2 dB por encima de ~60 Hz,
  unidad en subgraves). Así el lazo llega a ganancia unitaria con Feedback al 100 %.
- **Colas "infinitas" y pitch**: en *Single* cada pasada desplaza toda la energía (p. ej. +1 octava),
  que acaba saliendo por High Cut / Nyquist: la cola termina aunque el Feedback sea 100 %. En *Dual*
  la energía hace un paseo aleatorio entre octavas y sólo se pierde por los bordes (≈ 0,7 dB/s con
  Low Cut 10 Hz y High Cut 20 kHz, Big, Size 80 %), lo que da pads casi infinitos. Con Pitch 0 o
  *Bypass* la cola es prácticamente infinita.

## 4. LoopFilters (`LoopFilters.*`)

Orden dentro del lazo: **Low Cut → High Cut → Color**.

- **Low Cut** (10 Hz–2 kHz) y **High Cut** (500 Hz–20 kHz): filtros de estado variable TPT
  (Zavalishin/Simper) de 12 dB/oct, Butterworth (Q = 0,707). Se eligieron frente a biquads en
  forma directa porque siguen estables y sin artefactos cuando se barre la frecuencia mientras la
  cola suena. Las frecuencias se suavizan 50 ms en el dominio logarítmico y los coeficientes sólo
  se recalculan mientras se deslizan. El corte se limita a 0,45·fs.
- **Color**:
  - *Bright*: ancho de banda completo; sólo limita High Cut.
  - *Dark*: Butterworth de **8º orden** (4 secciones SVF con Q = 0,510 / 0,601 / 0,900 / 2,563)
    a 8,8–11 kHz según el modo de reverb. Emula las reverbs digitales de finales de los 70 /
    principios de los 80 y atenúa el aliasing que el pitch shifting acumula en iteraciones altas.
  - Bright ↔ Dark con crossfade de 20 ms; el filtro Dark sólo se calcula cuando hace falta.

## 5. Feedback, estabilidad y seguridad

- **Ley de feedback**: 0–90 % del recorrido → ganancia 0–0,95 (curva `^0,8`, más resolución abajo);
  90–100 % → 0,95–0,995 para colas casi infinitas.
- **Soft-clipper** en el wet (que también alimenta el lazo): lineal hasta 0,75 (−2,5 dBFS) y por
  encima un hombro `tanh` con derivada continua que tiende a 1,0. Sólo actúa cerca de 0 dBFS.
  Garantiza que con Feedback al 100 % y pitch acumulado el lazo nunca crezca sin límite
  (test 1: ruido blanco a 0 dBFS durante 10 s → pico < +6 dBFS en los 20 modos).
- **DC blocker** de un polo a 5 Hz en el retorno del lazo.
- **Denormales**: el motor activa FTZ/DAZ (x86) o FZ (ARM64) durante `process()`, además del
  `juce::ScopedNoDenormals` del procesador. Así la cola nunca cae en el rango denormal (muy lento)
  aunque el host no lo haga.
- **NaN/Inf**: al final de cada bloque se comprueban los acumuladores; si algo no es finito se
  silencia el bloque y se resetea el motor.

## 6. Mix

`dry = cos(θ)`, `wet = sin(θ)`, `θ = mix·π/2` → `dry² + wet² = 1` en todo el recorrido (test 6, medido
también de punta a punta con un impulso). Ganancias suavizadas 30 ms.

## 7. Integración con el host (FL Studio y otros)

- Latencia 0. Buses mono→estéreo y estéreo→estéreo.
- Bloques de cualquier tamaño, incluidos bloques de 0 muestras (algunos hosts los envían sólo para
  actualizar parámetros) y mayores que el máximo anunciado: no hay buffers temporales dependientes
  del bloque.
- `prepareToPlay` es idempotente y sólo realoca si cambia la frecuencia de muestreo.
- `processBlock` antes de `prepareToPlay` → passthrough seguro.
- **Bypass del host** (`processBlockBypassed`): el motor recibe silencio, el feedback va a 0, el seco
  a 1 y el wet a 0 en 50 ms: la cola se vacía sin clicks y no reaparece al reactivar.
- `getTailLengthSeconds()` estima el tiempo hasta −90 dB a partir de Feedback, Size, Diffusion y
  modo (1–60 s; 120 s con Feedback ≥ 99 %) para que el render y *Smart Disable* de FL Studio no
  corten la cola.
- Sin estado global mutable: cada instancia tiene sus PRNG, tablas y buffers (FL procesa
  instancias en paralelo en varios hilos).
- Parámetros con `ParameterID` versionado (versión 1); los IDs y su orden no deben cambiar nunca.

## 8. Rendimiento

Medido con `ab_render` (Release, un núcleo, 48 kHz):

| Configuración | CPU |
|---|---|
| Por defecto (Medium, Single) | ~1,2 % |
| Big + Dual + Dark + Feedback 100 % | ~2,2 % |
| Mono + Dual Reverse + Dark + modulación máxima | ~2,5 % |
| Peor caso con bloques de 1 muestra | ~2,3 % |

A 192 kHz el coste escala aproximadamente con la frecuencia de muestreo (≈ 5–8 %).

## 9. Herramientas de análisis

- `build/tests/ab_render salida.wav input=chord pitch=12 feedback=0.7 ...` renderiza el motor a un
  WAV de 32 bits float (ver `tests/RenderTool.cpp` para todas las opciones).
- `python3 tools/spectrogram.py a.wav b.wav -o plot.png` dibuja espectrogramas y envolventes
  (requiere numpy y matplotlib).
