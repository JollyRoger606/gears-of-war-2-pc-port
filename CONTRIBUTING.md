# Contributing

Pull requests are welcome. One pull request per feature or fix.

## How to contribute

1. Fork the repository and branch from the current `master`.
2. Make one pull request per feature or fix.
3. Describe what you changed and why.
4. Describe how you tested it: platform, GPU, and numbers before and after if it is a performance change, using `gow2_frame_log` / `d3d12_stats_log`.

## License

The project is BSD-3-Clause (see LICENSE). By opening a pull request you agree your contribution is under that license.

Only add third-party code whose license is compatible (MIT, BSD, zlib, Apache-2.0) and list it in THIRD_PARTY_NOTICES.md. No GPL code.

## Game files

Never commit game files or anything derived from them: `default.xex`, generated code from it, Coalesced bundles, textures, shader caches, dumps, saves, ISO data. `game/` and `generated/` stay local.

## Code

Follow the style of the surrounding code.

Runtime (SDK) changes go as patches in `sdk/patches` with a row in `sdk/patches/README.md`: one change per patch file. See that README for the workflow.

## Tests

Run the host tests before submitting; all steps must pass:

```powershell
pwsh -File scripts/test-host.ps1
```

Android changes also need:

```text
android\gradlew.bat testDebugUnitTest
```

Build instructions are in BUILDING.md.

AI-assisted contributions are fine. You are responsible for reviewing and testing what you submit.

## Bug reports

Open an issue with the newest log (`gow2_*.log`; on Android: Launch options > Share last log) and your hardware.

## Resumen en español

Los pull requests son bienvenidos, uno por función o corrección. Haz fork, crea una rama desde el `master` actual y describe qué cambiaste, por qué y cómo lo probaste (plataforma, GPU y números antes y después con `gow2_frame_log` / `d3d12_stats_log` si es un cambio de rendimiento). El proyecto es BSD-3-Clause (ver LICENSE): al abrir un pull request aceptas que tu contribución queda bajo esa licencia. Solo código de terceros con licencia compatible (MIT, BSD, zlib, Apache-2.0), listado en THIRD_PARTY_NOTICES.md; nada de GPL. Nunca subas archivos del juego ni derivados (`default.xex`, código generado, bundles Coalesced, texturas, cachés de shaders, volcados, partidas, datos ISO): `game/` y `generated/` quedan en local. Sigue el estilo del código cercano; los cambios del runtime (SDK) van como parches en `sdk/patches` con una fila en `sdk/patches/README.md`. Pasa `pwsh -File scripts/test-host.ps1` (todo debe pasar) y, en Android, `android\gradlew.bat testDebugUnitTest`; cómo compilar en BUILDING.md. Las contribuciones con ayuda de IA están bien, pero eres responsable de revisar y probar lo que envías. Los errores se reportan en un issue con el registro más reciente y tu hardware.
