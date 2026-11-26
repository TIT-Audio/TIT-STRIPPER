# TIT STRIPPER v0.1

Channel strip gratuito con tres módulos: **Gate/Expander**, **TIT COMP** (el compresor con todos sus modos) y **TIT Q** (el ecualizador de 10 bandas con dinámica). Formatos: **VST3 (Windows y macOS)** y **AU (macOS)**. Sin AAX, sin Standalone.

## Cómo se usa
- Barra superior: los tres módulos en el orden de la cadena. Cada uno tiene un botón **On/Off**, un botón con su nombre (muestra su ventana) y dos flechas **< >** para cambiar su posición en la cadena. Los seis órdenes posibles están permitidos.
- **Presets** de todo el strip (los tres módulos, los On/Off y el orden): menú desplegable, botón *Save* y el menú "..." para guardarlos o cargarlos en cualquier carpeta (pendrive, nube) o abrir la carpeta de presets (`Documentos/TIT STRIPPER/Presets`, extensión `.titstrip`). "Initialize" lo deja todo en los valores de fábrica. Cada módulo conserva además sus propios presets.
- **Sidechain**: una sola entrada de sidechain externa por instancia, compartida; el Gate, el compresor y las bandas dinámicas del EQ eligen individualmente entre interno y externo.
- Todos los mandos de los tres módulos se pueden automatizar desde el DAW (nombres con prefijo `Gate`, `Comp`, `EQ`).
- Al arrancar: Gate y Comp apagados, EQ encendido (sin bandas, transparente). Orden inicial: Gate > Comp > EQ.
- La latencia que informa al DAW es la suma de la de los módulos encendidos (oversampling, lookahead).

## Módulos
- **Gate/Expander**: Threshold, Range (hasta 90 dB), Ratio (1:1 a infinito; infinito = gate duro), Attack, Hold, Release, Hysteresis, Lookahead (hasta 10 ms), filtros de paso alto y bajo en la señal de detección con **pendiente ajustable de 6 a 96 dB/oct** (los mismos filtros de corte que TIT Q: mandos HP Slope y LP Slope, o la rueda del ratón sobre sus nodos en la gráfica), EQ gráfico de 3 bandas con analizador FFT, Stereo Link, sidechain interno/externo y **SC Listen**. Gráfica de la curva con el punto de trabajo y medidor de reducción.
- **TIT COMP**: ver el repositorio TIT-COMP (modos Clean, VCA TIT, FET TIT, FET TIT B, OP TIT, FAIR TIT, TIT MU, DXX TIT y TIT DIGITAL), con el interruptor **Color** (apagado de serie: saturación y timbre medidos de FAIR, FET, FET B, OP y DXX).
- **TIT Q**: ver el repositorio TIT-Q (v0.5: TIT 250 con Q ajustable, TIT SKY con curvas medidas, BOOBTEC con pasos fijos de graves, fase **Min / Linear / Mixed**, dinámica con coeficientes interpolados muestra a muestra, diseño fiel cerca de Nyquist y bandas encadenadas en 64 bits).

## Compilar
`cmake -B build && cmake --build build --config Release --target TITSTRIPPER_VST3`. El flujo de GitHub genera el artefacto `TITSTRIPPER-all` (Windows, macOS y código fuente).
macOS sin firmar: `xattr -cr "TIT STRIPPER.vst3"` (o .component).

## Estructura
`strip/` cadena y ventana, `gate/`, `comp/`, `q/` módulos, `common/` código compartido, `tests/` pruebas del núcleo (sin JUCE) y `tests/headless/` una prueba con JUCE de la cadena completa (parámetros, sidechain, latencia, estado, orden) que se compila a mano.

## Licencia
TIT STRIPPER es gratuito y se distribuye bajo la licencia GNU AGPLv3 (ver el archivo LICENSE). Está construido con JUCE 8. El código fuente está disponible en este repositorio.

## Orden de arranque (archivo de configuración)
El orden de los módulos con el que arranca una instancia nueva se guarda en `config.txt`
(Windows: `%APPDATA%\TIT Audio\TIT STRIPPER\`; macOS: `~/Library/Application Support/TIT Audio/TIT STRIPPER/`).
Se cambia desde el menú `...` → "Start order for new instances" / "Use the current order as start order", o editando el archivo:

    order = EQ, GATE, COMP
    color = on

`color` (on/off) es el estado del botón Color del compresor al arrancar una instancia nueva; también se cambia desde el menú `...` → "Color on at start".

Los proyectos ya guardados conservan su propio orden.
