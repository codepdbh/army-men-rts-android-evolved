<div align="center">
  <img src="icono/icono.png" alt="Army Men RTS Android Evolved" width="180">
  <h1>Army Men RTS Â· Android Evolved</h1>
  <p>InvestigaciÃ³n y port comunitario del juego de PC a Android ARM64 con Vulkan 1.1.</p>
  <p><strong>Estado: prototipo tÃ©cnico. AÃºn no ejecuta ni renderiza el juego.</strong></p>
</div>

## Estado del trabajo

| Componente | Estado |
| --- | --- |
| AnÃ¡lisis del ejecutable de Windows x86 | Completado para la copia local |
| CatÃ¡logo y traducciÃ³n estÃ¡tica a C | 29.278 funciones generadas localmente; artefactos excluidos de Git |
| CompilaciÃ³n de las unidades generadas para Android ARM64 | En validaciÃ³n |
| APK de diagnÃ³stico, permisos y detecciÃ³n de datos | Funcional en dispositivo Android |
| ReimplementaciÃ³n de servicios Win32, DirectDraw y Direct3D 7 sobre Vulkan | Pendiente |
| Audio, vÃ­deo, entrada y juego completo | Pendiente |

El APK actual muestra si encuentra los datos necesarios y consulta Vulkan. **No es una versiÃ³n jugable**. El ejecutable de PC no puede iniciarse directamente en Android: la traducciÃ³n necesita aÃºn los servicios de Windows y el renderizador DirectDraw/Direct3D 7 que usa este juego. Es una ruta distinta del puente Direct3D 9 empleado en [NFSU2 Android Evolved](https://github.com/codepdbh/nfsu2-recompiled).

## Datos del juego

Se necesita una copia propia de Army Men RTS para PC. Copia sus archivos a `Memoria interna/armymenrts/`; el diagnÃ³stico comprueba `AMRTS.EXE`, `base.x`, `binkw32.dll` y `mss32.dll`. La aplicaciÃ³n pide acceso a los archivos para leer esa carpeta. Los datos, ejecutables y bibliotecas del juego no estÃ¡n incluidos en este repositorio ni en el APK.

## Compilar el APK de diagnÃ³stico

Requisitos: JDK 17, Android SDK 35, Android NDK 28.2.13676358 y CMake 3.30.5.

```powershell
cd android
./gradlew.bat assembleDebug
```

El archivo resultante queda en `android/app/build/outputs/apk/debug/app-debug.apk`. El icono usa el arte de `icono/icono.png` a pantalla completa.

## Regenerar la traducciÃ³n del ejecutable

La traducciÃ³n utiliza el proyecto hermano `pcrecomp` del trabajo de NFSU2, instalado en `../pcrecomp`, ademÃ¡s de Python y Capstone. La copia propia del juego se supone en `../Army Men RTS/`.

```powershell
python scripts/run_lift.py catalog '../Army Men RTS/AMRTS.EXE' config/seeds.json work/catalog.json
python scripts/run_lift.py lift '../Army Men RTS/AMRTS.EXE' work/catalog.json src/recomp/gen --split 300
```

El catÃ¡logo, los C generados y los archivos del juego permanecen fuera de Git. El objetivo opcional `amrts_guest` de CMake permite comprobar la compilaciÃ³n ARM64 de los C generados, pero todavÃ­a no los enlaza al APK.

## PrÃ³ximos hitos

1. Cargar la imagen PE x86 y hacer funcionar los servicios de memoria, archivos y tiempo de Win32.
2. Implementar DirectDraw/Direct3D 7 sobre Vulkan 1.1.
3. Adaptar audio, vÃ­deo, controles tÃ¡ctiles y mandos.
4. Entrar al menÃº y despuÃ©s al gameplay, medir rendimiento y corregir fallos en dispositivos reales.

Este proyecto no estÃ¡ afiliado al editor original. Conserva tus archivos del juego y distribuye Ãºnicamente cÃ³digo propio y arte con derechos de uso.

\n