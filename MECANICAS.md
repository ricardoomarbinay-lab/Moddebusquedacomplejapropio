# Mecánicas de Geometry Dash y cómo las cubre PathBot

Idea central: PathBot **no modela** las mecánicas; las ejecuta con el propio motor del juego
(checkpoint -> simular -> restaurar). Esta tabla explica qué implica cada mecánica para el
solver y qué limitaciones quedan.

## Modos de juego (portales)
| Modo | Input | Qué necesita el solver |
|---|---|---|
| Cubo | Click = salto (buffer al aterrizar si se mantiene) | Distinguir tap vs mantener |
| Nave | Mantener = sube, soltar = baja | Control continuo; acciones largas hold/release |
| Bola | Click invierte gravedad en suelo/techo | Taps espaciados |
| UFO | Click = micro-salto en el aire | Taps frecuentes, stride bajo |
| Wave | Mantener = diagonal arriba, soltar = abajo | Stride 1 recomendado en corredores estrechos |
| Robot | Mantener = salto de altura variable | Duración del hold importa (acciones de longitud variable) |
| Spider | Click = teletransporte al lado opuesto | Taps; el estado cuantizado incluye gravedad/suelo |
| Swing | Click = invierte gravedad en el aire | Igual que UFO |

## Objetos interactivos
- **Pads**: se activan al tocarlos, sin input. El solver solo debe llegar con la posición correcta (dedup por y/velocidad).
- **Orbs** (amarillo, rosa, rojo, azul, verde, negro, dash, spider...): requieren un *flanco* de pulsación en el momento exacto -> acciones `tap` y `hold` con stride fino.
- **Portales**: gravedad, velocidad (0.5x–4x), tamaño (mini), espejo, dual, teletransporte. Los guarda el checkpoint; la clave de estado incluye modo, gravedad, dual.
- **Hitboxes**: sólido (bloques), peligro (pinchos, sierras), especial (orbs/pads/portales). Las resuelve el juego; el solver solo observa `destroyPlayer`.
- **Pendientes (slopes)**: afectan velocidad vertical y modo de aterrizaje; el estado cuantizado incluye `yVelocity`.

## Triggers
Move, rotate, alpha, color, pulse, spawn, toggle, stop, follow, shake, camera, etc. El estado de triggers va dentro del checkpoint de práctica. Limitaciones: triggers aleatorios, contadores de item e instant-count complejos pueden no restaurarse al 100%; por eso la reproducción verifica y reintenta.

## Físicas y tiempo
- 240 ticks de física por segundo; el solver avanza 1 tick por paso (`processCommands`).
- La reproducción reinyecta el input por tick exacto (no depende del framerate).

## No soportado / limitado
- Modo plataformero (2.2): no hay avance forzado, el solver actual no lo planifica.
- Niveles con aleatoriedad real en la física.
- Secciones de timing extremo pueden requerir `Paso de decisión = 1` y haz muy ancho.
