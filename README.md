<div align="center">
  <img src="icono/icono.png" alt="Army Men RTS Android Evolved" width="180">
  <h1>Army Men RTS · Android Evolved</h1>
  <p>Port comunitario del juego de PC a Android ARM64, recompilado estáticamente y con gráficos sobre Vulkan 1.1.</p>
  <p><strong>Jugable: menús, misiones, construcción, sonido, controles táctiles y multijugador LAN entre teléfonos.</strong></p>
  <img src="https://img.shields.io/badge/Android-10%2B_ARM64-3DDC84" alt="Android 10+ ARM64">
  <img src="https://img.shields.io/badge/Vulkan-1.1-B71C1C" alt="Vulkan 1.1">
  <img src="https://img.shields.io/badge/versión-0.6.0-D6A44B" alt="Versión 0.6.0">
  <img src="https://img.shields.io/badge/estado-en_desarrollo-D6A44B" alt="En desarrollo">
</div>

## Instalar

1. Descarga el APK de la [última release](https://github.com/codepdbh/army-men-rts-android-evolved/releases/latest) (Android 10 o superior, ARM64) e instálalo.
2. Copia tu Army Men RTS para PC en `Memoria interna/armymenrts/` (ver [Tu copia del juego](#tu-copia-del-juego)).
3. Abre la app, concede el acceso a los archivos y pulsa **▶ JUGAR**.

El launcher comprueba el acceso, la copia del juego y la GPU antes de empezar. Cada release incluye un archivo `SHA256SUMS` para verificar el APK.

## Estado

| Componente | Estado |
| --- | --- |
| Traducción estática del ejecutable x86 | 29.280 funciones traducidas a C y compiladas para ARM64 |
| Sistema Win32 emulado | Memoria, heap, TLS, sincronización, archivos, mapeos, ventanas, reloj y texto CP1252/UTF-8 |
| Hilos del juego | Hilos cooperativos con pila, TIB y TLS propios |
| Gráficos | DirectDraw/Direct3D 7 traducido a Vulkan 1.1: texturas, mezcla, profundidad, sombras y modos de borde |
| Audio | Miles Sound System sobre AAudio: efectos WAV y música/voces MP3 (minimp3, dominio público), con volumen propio del port |
| Controles táctiles | Esquema RTS completo a partir de los atajos del juego; ver [controles](docs/TOUCH_CONTROLS.md) |
| Multijugador LAN | Winsock sobre sockets Android; partida entre dos teléfonos en la misma WiFi comprobada |
| Cambiar de app | La partida queda en pausa y continúa al volver |
| Drivers gráficos personalizados | Experimental, con [libadrenotools](https://github.com/bylaws/libadrenotools); vuelve al driver del sistema si el importado falla |
| Vídeos Bink | Pendiente |
| Campaña completa | Sin validar de principio a fin |

Probado en un Galaxy S25 Ultra (Adreno 830) y un Redmi Note 8 (Adreno 610). El renderizador traduce DirectDraw/Direct3D 7 sobre Vulkan 1.1, distinto del puente Direct3D 9 de [NFSU2 Android Evolved](https://github.com/codepdbh/nfsu2-recompiled).

## Jugar en el teléfono

### Controles táctiles

| Gesto | Acción |
| --- | --- |
| Toque | Seleccionar u orden contextual (clic izquierdo) |
| Arrastrar un dedo | Mover la cámara; al soltar con impulso sigue con inercia |
| Mantener y arrastrar | Recuadro de selección (o cualquier arrastre con el botón **▭** activo) |
| Toque con dos dedos | Clic derecho |
| Dos dedos | Mover la cámara y acercar o alejar a la vez |

- **Construir:** al elegir un edificio aparece una barra con **⟲ / ⟳ Girar**, **✓ Colocar** y **✕ Cancelar**. Tocar el mapa lleva el edificio a ese punto.
- **Barra superior:** **≡** menú, **✕** clic derecho, **+** añadir a la selección, **Todo**, **Tipo**, **■**, **⌂**, **⚑** y pelotones **1–4**.
- **HUD:** en los paneles del juego, mantener pulsado hace clic derecho, por ejemplo para quitar una unidad de la cola de producción.
- **Otros dispositivos:** el ratón Bluetooth o USB funciona como en PC, y también un teclado físico.

El [análisis de controles](docs/TOUCH_CONTROLS.md) explica cada gesto, sus límites y las pruebas.

### Escribir texto

Para nombres de jugador o partidas: toca el campo de texto del juego y luego **≡ → Escribir texto (teclado)**.
- **Reemplazo completo:** el texto sustituye todo lo que tenía el campo, sin borrar letra por letra.
- **Borrar todo** vacía el campo.
- **Caracteres admitidos:** mayúsculas y signos de puntuación, pero no la ñ ni las tildes.

### Multijugador LAN

1. Conecta los teléfonos a la **misma red WiFi**.
2. Usa un nombre de jugador distinto en cada teléfono: **Multijugador → Cambiar nombre**.
3. Uno pulsa **Crear partida**; el otro la verá en la lista y podrá unirse.

Si un jugador sale a otra app, su juego queda en pausa y el otro lo espera.

### Driver gráfico (experimental)

El botón **Driver gráfico** del launcher importa drivers Vulkan alternativos:
- **Turnip:** para Adreno.
- **Mali:** para Dimensity y otros chips con GPU Mali.
- **Xclipse:** para Exynos 2200 o superior.

Acepta el `.zip` habitual (con `meta.json`) o un archivo `.so`.

Al elegir un driver la app se reinicia. Si el driver no arranca o no reconoce la GPU, el juego usa el del sistema y el launcher lo marca en rojo.

No todos los drivers soportan todas las GPU: Turnip v26.0.0 R8 se carga en la Adreno 610, pero no la reconoce. Todavía no se ha probado con Mali ni con Xclipse.

## Tu copia del juego

Se requiere una copia propia de Army Men RTS para PC. Sus datos se leen desde `Memoria interna/armymenrts/`. El repositorio y el APK no incluyen el ejecutable ni los archivos comerciales.

Con el teléfono conectado y ADB disponible, puedes copiar los datos desde Windows:

```powershell
./scripts/copy_data.ps1 -GameFolder 'C:/ruta/Army Men RTS'
```

El script copia el ejecutable, los paquetes `.x` y `.x-s`, las bibliotecas originales y los directorios de recursos. Conserva las configuraciones ya existentes en Android. La selección de idioma de la copia probada es `spanish`.

## Compilar

Requisitos: JDK 17 o 21, Android SDK 35, NDK 28.2.13676358 y CMake 3.30.5. El objetivo es Android 10 o posterior y ARM64.

libadrenotools es un submódulo: clona con `git clone --recursive` o ejecuta `git submodule update --init --recursive`.

```powershell
cd android
./gradlew.bat assembleDebug
```

Este comando construye el diagnóstico mínimo, sin código derivado del juego. El APK queda en `android/app/build/outputs/apk/debug/app-debug.apk`.

Para construir el juego completo, genera primero los C locales (siguiente sección) y usa:

```powershell
cd android
./gradlew.bat assembleDebug -PamrtsLinkGuest -PamrtsOptimizeGuest
```

## Generar el código ARM64

Se necesitan Python con Capstone y pefile, y el proyecto hermano `pcrecomp` en `../pcrecomp`. Estos comandos suponen la copia del juego en `../Army Men RTS/`:

```powershell
python scripts/run_lift.py catalog '../Army Men RTS/AMRTS.EXE' config/seeds.json work/catalog.json
python scripts/run_lift.py lift '../Army Men RTS/AMRTS.EXE' work/catalog.json src/recomp/gen --split 300
```

El análisis incluye los constructores y terminadores del CRT definidos en `config/seed_ranges.json`, y las entradas indirectas (tablas virtuales y de saltos) de `config/seeds.json`. El catálogo, los C generados y los archivos del juego quedan fuera de Git. La versión del ejecutable debe coincidir con las direcciones usadas por la traducción.

## Pruebas

```powershell
./scripts/test_device.ps1
```

El script compila, instala y ejecuta la prueba en segundo plano mediante un receptor restringido a ADB. Comprueba memoria, heap, TLS, sincronización, conversión de texto, archivos, mapeos, recursos de versión y conservación del estado durante callbacks ARM64. El punto alcanzado queda en `work/device-boot-result.txt`. Con varios teléfonos, especifica `-Serial`.

También puedes comprobar la entrada y el renderizador en Windows con Visual Studio y un controlador Vulkan:

```powershell
./scripts/test_host.ps1
```

Estas pruebas comparan píxeles reales de texturas, mezcla alfa, profundidad, borrado por regiones y cambios de viewport, además de comprobar en la JVM los gestos y el ritmo de la entrada. No sustituyen la comprobación del juego en Android.

La prueba de gestos en dispositivo carga el entrenamiento e inyecta selección, zoom, cámara y cancelación:

```powershell
./scripts/test_touch.ps1 -Serial R5GL70TXWMP
```

## Siguiente trabajo

1. Vídeos Bink.
2. Recorrer la campaña completa y corregir los fallos que aparezcan.
3. Crear una red WiFi desde el launcher para jugar en LAN sin router.
4. Medir el rendimiento en más GPU y validar drivers Mali y Xclipse.

Proyecto no afiliado al editor original. Distribuye únicamente código propio y arte con derechos de uso; conserva tu copia del juego para aportar los datos.
