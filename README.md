<div align="center">
  <img src="icono/icono.png" alt="Army Men RTS Android Evolved" width="180">
  <h1>Army Men RTS · Android Evolved</h1>
  <p>Port comunitario del juego de PC a Android ARM64, con Vulkan 1.1 como objetivo.</p>
  <p><strong>El arranque recompilado ya alcanza la inicialización de DirectDraw. Todavía no hay gameplay.</strong></p>
  <img src="https://img.shields.io/badge/Android-ARM64-3DDC84" alt="Android ARM64">
  <img src="https://img.shields.io/badge/Vulkan-objetivo_1.1-B71C1C" alt="Vulkan 1.1 como objetivo">
  <img src="https://img.shields.io/badge/estado-en_desarrollo-D6A44B" alt="En desarrollo">
</div>

## Avance comprobado

| Componente | Estado |
| --- | --- |
| Traducción estática del ejecutable x86 | 29.280 funciones; 98 unidades ARM64 compiladas y enlazadas |
| Cargador PE32 y enlaces de importación | Probado con 4 secciones y 241 importaciones |
| Arranque real del código recompilado | Crea la ventana, carga configuraciones y llega a `DirectDrawEnumerateExA` |
| Memoria, heap, TLS y sincronización Win32 | Implementación inicial con pruebas en Android |
| Archivos y mapeo de paquetes | Acceso a la copia del usuario; mapeo directo y copy-on-write comprobados |
| Texto, idioma, reloj y versión del PE | CP1252/UTF-8, español y recursos del ejecutable |
| DirectDraw/Direct3D 7 sobre Vulkan | En desarrollo |
| Audio, vídeo, controles y gameplay | Pendiente |

El APK local ejecuta el inicio del juego mediante el código ARM64 generado. La prueba termina de forma controlada al encontrar una función pendiente o al superar su límite de tiempo. **No es una versión jugable todavía**: falta el puente gráfico, además de completar los servicios que necesiten las siguientes etapas. El renderizador debe traducir DirectDraw/Direct3D 7; esa interfaz es distinta del puente Direct3D 9 de [NFSU2 Android Evolved](https://github.com/codepdbh/nfsu2-recompiled).

## Tu copia del juego

Se requiere una copia propia de Army Men RTS para PC. Sus datos se leen desde `Memoria interna/armymenrts/`. El repositorio y el APK no incluyen el ejecutable ni los archivos comerciales.

Con el teléfono conectado y ADB disponible, puedes copiar los datos desde Windows:

```powershell
./scripts/copy_data.ps1 -GameFolder 'C:/ruta/Army Men RTS'
```

El script copia el ejecutable, los paquetes `.x` y `.x-s`, las bibliotecas originales y los directorios de recursos. Conserva las configuraciones ya existentes en Android. La selección de idioma de la copia probada es `spanish`.

## Compilar

Requisitos: JDK 17 o 21, Android SDK 35, NDK 28.2.13676358 y CMake 3.30.5. El objetivo actual es Android 10 o posterior y ARM64.

```powershell
cd android
./gradlew.bat assembleDebug
```

Este comando construye el diagnóstico mínimo, sin código derivado del juego. El APK queda en `android/app/build/outputs/apk/debug/app-debug.apk` y utiliza `icono/icono.png` a tamaño completo.

Para construir el arranque recompilado, genera primero los C locales y usa:

```powershell
cd android
./gradlew.bat assembleDebug -PamrtsLinkGuest
```

## Generar el código ARM64

Se necesitan Python con Capstone y pefile, y el proyecto hermano `pcrecomp` en `../pcrecomp`. Estos comandos suponen la copia del juego en `../Army Men RTS/`:

```powershell
python scripts/run_lift.py catalog '../Army Men RTS/AMRTS.EXE' config/seeds.json work/catalog.json
python scripts/run_lift.py lift '../Army Men RTS/AMRTS.EXE' work/catalog.json src/recomp/gen --split 300
```

El análisis incluye los constructores y terminadores del CRT definidos en `config/seed_ranges.json`. El catálogo, los C generados y los archivos del juego quedan fuera de Git. La versión del ejecutable debe coincidir con las direcciones usadas por la traducción.

## Pruebas en el teléfono

```powershell
./scripts/test_device.ps1
```

El script compila, instala y ejecuta la prueba en segundo plano mediante un receptor restringido a ADB. Comprueba memoria, heap, TLS, sincronización, conversión de texto, archivos, mapeos, recursos de versión y conservación del estado durante callbacks ARM64. El punto alcanzado queda en `work/device-boot-result.txt`. Con varios teléfonos, especifica `-Serial`.

## Siguiente trabajo

1. Implementar DirectDraw y Direct3D 7 sobre Vulkan 1.1.
2. Completar hilos y las llamadas de Windows que aparezcan después del arranque gráfico.
3. Adaptar audio, vídeos y controles táctiles para un RTS.
4. Llegar al menú y al gameplay; medir rendimiento y corregir fallos reales.

Proyecto no afiliado al editor original. Distribuye únicamente código propio y arte con derechos de uso; conserva tu copia del juego para aportar los datos.
