# Resoluciones y presentación

Las opciones de vídeo originales permiten elegir modos de 640×480 a 2560×1440, incluidos 1920×1080 y 2340×1080. Los modos se ofrecen en 16 y 32 bits, hasta las 22 entradas que admite la tabla del motor. El lift verifica las instrucciones originales antes de ampliar los límites de anchura y altura; no modifica la copia del ejecutable.

El juego conserva sus menús en píxeles: al subir la resolución se ven más pequeños. La resolución aumenta el detalle de la escena 3D, pero no añade detalle a los gráficos originales del menú. El escalado final utiliza filtrado lineal cuando lo admite la GPU.

El ratón original usa ganancias de anchura/640 y altura/480. La entrada táctil compensa estas ganancias y el recentrado al cambiar de modo. Los arrastres mantienen la pulsación inicial durante un fotograma presentado, envían el desplazamiento y esperan otro fotograma antes de soltar; así una selección breve no desaparece cuando el renderizado se ralentiza.

El renderizador conserva los buffers, framebuffers y descriptores de cada dibujo hasta completar el lote. Espera antes de leer o modificar imágenes, liberarlas o presentar; también limita los lotes a 128 dibujos. Cada dibujo conserva su propia selección de texturas. La prueba de píxeles comprueba dos dibujos pendientes con texturas distintas.

Se evita la lectura y escritura completas de la imagen en el Lock/Unlock de sincronización de la ruta original `0x004200F0`, que no accede a los píxeles. El resto de bloqueos conserva su sincronización normal.

Validación en S25 Ultra: entrenamiento y prueba de gestos completados; muestra de diez segundos de aproximadamente 101 FPS a 640×480. No representa el rendimiento de toda la campaña ni de Full HD. Full HD se comprobó durante el cambio de modo y la entrada al entrenamiento, sin una medición equivalente de FPS.
