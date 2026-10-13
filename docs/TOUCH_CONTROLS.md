# Controles táctiles RTS

El punto de partida es la semántica de Army Men RTS para PC, descrita en el [manual original de 3DO](https://manualzz.com/doc/6747186/3do-army-men-rts-instruction-booklet): la selección y las órdenes contextuales comparten el botón izquierdo. El derecho cancela; su arrastre desplaza la cámara. La rueda modifica la altura. Por eso el port no utiliza un modo persistente de «orden = clic derecho».

## Gestos

| Gesto | Acción |
| --- | --- |
| Toque con un dedo | Selección u orden contextual; también botones, construcción y minimapa |
| Arrastre con un dedo | Rectángulo de selección del motor |
| Arrastre con dos dedos | Desplazamiento de cámara mediante arrastre derecho |
| Pellizco | Zoom mediante el eje Z de DirectInput |
| Toque con dos dedos | Cancelación mediante clic derecho |
| Mantener un dedo 450 ms y soltar | Cancelación contextual, útil también en las colas del HUD |

El único elemento permanente es el botón de menú, arriba a la izquierda. Su panel reúne pausa/objetivos, cancelar selección, detener, cuartel general, último combate, pelotones, Intro y ayuda. Deja visibles el minimapa, las colas de construcción y los pelotones originales. La sensibilidad de cámara se conserva entre sesiones y admite valores de 25 % a 200 %.

## Evitar órdenes accidentales

Un toque comienza como movimiento de cursor, sin pulsar inmediatamente el botón izquierdo. El controlador decide entre toque, selección y gesto de dos dedos. Un arrastre cruza un umbral de 10 dp; el pellizco usa 14 dp y compara el cambio de distancia con el desplazamiento del centro. Una vez decidido el gesto no cambia de tipo hasta levantar los dedos.

El segundo dedo convierte un toque pendiente en cámara/zoom. Si ya comenzó una selección, la cierra y suprime acciones adicionales hasta terminar ese contacto. El último dedo de un gesto múltiple no genera otro clic. La cancelación libera cualquier botón de arrastre. Los clics y atajos se mantienen 90 ms para que el motor pueda observarlos incluso entre fotogramas.

Las coordenadas se normalizan contra la superficie visible y se convierten a la resolución actual del juego. El movimiento y los botones llegan tanto al estado inmediato como al buffer DirectInput. Sus formatos con `ANYINSTANCE` se resuelven contra los ejes y botones virtuales, y la rueda entrega incrementos de 120. Ningún evento modifica registros x86 desde el hilo de UI: una cola protegida los entrega al hilo del juego.

## Pruebas

- JVM: toque, selección, cámara, pellizco, cancelación con dos dedos, pulsación larga y cancelación de arrastre; comprueba que cámara/zoom no emitan una orden izquierda.
- Pruebas nativas de arranque: formatos comodín, estado de botones, orden de transiciones, lectura sin consumir, liberación y eje Z.
- Instrumentación en S25 Ultra: abre entrenamiento, carga la misión e inyecta eventos de selección, cámara, pellizco y cancelación, comprobando que el juego siga presentando imágenes. Última ejecución: 115 fotogramas y resultado satisfactorio. Esta prueba verifica continuidad, no completa el tutorial ni certifica toda la campaña.

```powershell
./scripts/test_host.ps1
./scripts/test_touch.ps1 -Serial R5GL70TXWMP
```

La instrumentación inicia una sesión nueva de entrenamiento. La salida y los registros locales quedan en `work/`, fuera del repositorio. Audio Android y vídeos Bink siguen pendientes. Al cambiar de aplicación se detiene esta versión de la sesión; volver arranca desde cero.
