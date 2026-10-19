# Controles táctiles RTS

## Fuente de los controles originales

Los controles de PC no se han supuesto: salen de los propios archivos del juego.

- **Teclado**: `if_game_keybind.cfg`, dentro de `client/p/ingame.x`.
- **Ratón**: los objetivos del entrenamiento en `library/language/spanish.dat`.
- **Contraste**: una [guía de controles del manual](https://manualzilla.com/doc/5876652/4---clan-hero-army-men-rts).

| PC | Acción |
| --- | --- |
| Clic izquierdo | Seleccionar; con selección, orden contextual (mover, atacar, recoger, entrar) |
| Arrastre izquierdo | Recuadro de selección |
| Mayús + clic | Añadir a la selección |
| Clic derecho | Deseleccionar o cancelar (punto de reunión, colas de producción) |
| Arrastre derecho | Desplazar la cámara (el motor recentra el cursor y usa sus deltas) |
| Rueda, o Ctrl + ratón | Altura de la cámara |
| Clic central, `,` `.` | Girar un edificio al colocarlo |
| Flechas | Desplazar la cámara |
| F1–F4 | Alturas de cámara 20/40/60/80 |
| 1–4 | Seleccionar pelotón (el juego tiene cuatro) |
| Ctrl + 1–4 | Crear pelotón con la selección |
| Alt + 1–4 | Añadir la selección al pelotón |
| Mayús + 1–4 | Centrar la cámara en el pelotón |
| E | Seleccionar todo |
| G | Seleccionar grupo (mismo tipo) |
| S | Detener |
| H | Cuartel general |
| U | Mejorar |
| Espacio | Ir al último aviso o combate |
| `[` `]` y Alt + `[` `]` | Unidad anterior/siguiente, por unidad o por tipo |
| Mayús + D | Autodestruir |
| P / Pausa | Pausa |
| Esc | Menú del juego y objetivos |
| F8 / F9 / F10 | Guardar o cargar, guardado rápido, carga rápida |
| C | Comunicaciones |

## Esquema táctil

### Gestos sobre el mapa

| Gesto | Acción |
| --- | --- |
| Toque | Clic izquierdo: seleccionar u orden contextual; también HUD, construcción y minimapa |
| Arrastrar un dedo | Cámara: el punto del terreno tocado queda exactamente bajo el dedo; al soltar con velocidad sigue con inercia y un toque la detiene |
| Mantener 0,5 s (vibra) y arrastrar | Recuadro de selección |
| Mantener y soltar sin mover | Igual que un toque: un toque lento nunca cancela |
| Toque con dos dedos | Clic derecho |
| Dos dedos | La cámara sigue al punto medio y la separación fija la altura (paneo y zoom a la vez, entre `camera.minheight` 20 y `camera.maxheight` 70) |
| Girar dos dedos | Rotar el edificio que se coloca, en pasos de 30° (`.` y `,`) |

Cada orden deja un anillo breve donde cayó el toque, porque el cursor del juego es diminuto en un teléfono.

### Barra superior

El HUD original ocupa las esquinas inferiores y la superior derecha, así que la barra va arriba a la izquierda.

- **≡**: menú.
- **✕**: clic derecho.
- **+**: modo añadir, que mantiene Mayús en cada toque.
- **Todo** (E), **Tipo** (G), **■** (S), **⌂** (H) y **⚑** (Espacio).
- **1–4**: pelotones.
  - Tocar: seleccionar.
  - Doble toque: centrar la cámara (Mayús+N).
  - Mantener: crear el pelotón con la selección (Ctrl+N).
  - Mantener con **+** activo: añadir al pelotón (Alt+N).

La barra se puede ocultar desde el menú.

### Menú ≡

- Menú del juego (Esc), pausa (P), mejorar (U) y unidad anterior/siguiente.
- Alturas de cámara F1–F4.
- Guardado y carga rápidos (F9/F10), guardar o cargar (F8) y comunicaciones (C).
- Autodestruir, con confirmación.
- Intro y volumen.
- Ayuda y velocidad de cámara.

## Cámara directa

Ya no se emula el arrastre con el botón derecho: el motor recentraba el cursor y el resultado dependía de los FPS. La cámara se controla en coordenadas del mundo.

- **Interceptación**: la simulación de cámara es un método virtual (`0x0056E7A0`) que el port intercepta mediante `recomp_lookup_manual`.
- **Campos medidos en el objeto cámara**:
  - Orientación: `+0x68` derecha, `+0x78` arriba, `+0x88` frente.
  - Posición dibujada: `+0x98`.
  - Altura actual y pedida: `+0xE0` y `+0xF4`.
  - Posición objetivo: `+0x1C0`.
- **Proyección**: es la matriz D3D de la cámara de vídeo. Se localiza en memoria por su forma (`m23 = 1`, `m32 < 0`); en 1024×768, tanX = 0,744 y tanY = 0,558.
- **Ancla**: al empezar el gesto se proyecta el punto tocado sobre el terreno. La altura del suelo la da la rutina del motor `0x00542AB0`, que también limita a los bordes del mapa.
- **Seguimiento**: en cada fotograma se desplazan la posición objetivo y la dibujada para que el ancla quede bajo el dedo. No hay retraso por el suavizado del motor y no se usa el ratón.
- **Gestos rápidos**: un deslizamiento que empieza y termina entre dos fotogramas del juego se aplica completo, aunque los FPS sean bajos.
- **Inercia**: la velocidad se mide en los últimos 80 ms; si el dedo estaba quieto 60 ms antes de soltar, no hay inercia. Decae exponencialmente (τ = 0,325 s) y se detiene al tocar o al llegar al borde del mapa.

Validación en el Redmi Note 8: Sarge sigue al dedo con un error máximo de ±11 px en desplazamientos de 500 px. El pellizco pasó la altura de 40 a 23,5. La inercia se comprobó grabando la pantalla.

## Implementación

1. `RtsTouchController` convierte gestos en acciones: clic, botón, desplazamiento relativo, rueda y giro. Es Java puro y se prueba en la JVM.
2. `GameInput` entrega esas acciones al motor sincronizadas con los fotogramas:
   - Cada pulsación y su liberación quedan separadas por dos fotogramas presentados, que equivalen a una lectura completa de DirectInput.
   - Cuando el juego se bloquea, un límite de 250 ms evita botones atascados.
   - Esto sustituye los antiguos 90 ms fijos, que perdían clics cuando bajaban los FPS.
3. El puente nativo traduce las posiciones absolutas a los contadores relativos del ratón del motor. Tiene en cuenta las ganancias de anchura/640 y altura/480.
   - Solo vuelve a leer el cursor del motor cuando este ha integrado todos los deltas anteriores. Así no aplica dos veces el mismo movimiento.
   - La cámara usa deltas relativos (`AndroidGuestInput::Motion`) y acumula los restos subpíxel, porque el motor recentra el cursor durante el arrastre derecho.
4. Al terminar cada gesto, el cursor queda fuera del 3,5 % de cada borde, que es la franja de desplazamiento por borde. Antes, un toque en el minimapa o en el HUD inferior dejaba la cámara desplazándose sola.

## Pruebas

- **JVM**: toque, toque en el borde, ancla y seguimiento de cámara, velocidad de inercia (deslizamiento y dedo quieto), parada al tocar, dos dedos con zoom alrededor del punto medio, recuadro tras mantener, cancelación, giro, transiciones entre gestos y liberación al cancelar. También la entrega sincronizada: retención por fotograma, límite ante un juego bloqueado, `clear()` y orden de Mayús+clic.
- **Pruebas nativas de arranque**: formatos de DirectInput y el caso del cursor obsoleto, en el que el movimiento en cola no debe duplicarse.
- **Instrumentación**: `./scripts/test_touch.ps1` en el teléfono, con los gestos sobre el entrenamiento.
