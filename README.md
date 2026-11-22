# Aurora Bloom

Reverb algorítmica estéreo con pitch shifter granular dentro del lazo de retroalimentación
(efecto *shimmer*). Plugin VST3 / AU / Standalone construido con JUCE 8.

> Proyecto en desarrollo. La documentación completa (instalación en FL Studio, parámetros y presets)
> se añade al final del plan de trabajo.

## Compilar

```bash
git clone --recursive <url-del-repo>
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Licencia

AGPL-3.0 (ver `LICENSE`). JUCE 8 se distribuye bajo AGPLv3 o licencia comercial de JUCE: si vas a
distribuir el plugin comercialmente con código cerrado necesitas una licencia de JUCE.
