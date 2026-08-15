# Command-Line Reference {#cli-reference}

Flags are parsed by `Sleak::CommandLine::Parse` in `Client/src/main.cpp` and read with
`Sleak::CommandLine::GetValue("-flag")`. This table matches `PrintHelp` in that file.

| Flag | Value | Description |
| :--- | :--- | :--- |
| `-r` | `vulkan \| d3d11 \| d3d12 \| opengl` | Graphics backend. `d3d11` is the default on Windows. |
| `-w` | `<pixels>` | Window width (default 1200). |
| `-h` | `<pixels>` | Window height (default 800). |
| `-t` | `<name>` | Window title; use `_` for spaces. |
| `--fullscreen` | | Start in fullscreen. |
| `-world` | `<name>` | Debug builds only (`Game::Begin`, `#ifdef DEBUG`). Loads the named save if one exists, otherwise creates it; skips the main menu. |
| `-seed` | `<n>` | Seed for a new world; random if omitted. Ignored when `-world` opens an existing save. |
| `-rd` | `<n>` | Initial render distance in chunks (default 8). |
| `-msaa` | `1 \| 2 \| 4 \| 8` | MSAA sample count. |
| `--vsync` | | Enable VSync on launch. |
| `--no-vsync` | | Disable VSync on launch. |
| `--bench` | | Start benchmark recording immediately. This only starts the recorder; it does not load a world. Combine with `-world <name> -seed <n>` for an unattended gameplay run. |
| `--validate` | | Enable the Vulkan validation layer. |
| `--help` | | Print this table and exit. |

## Examples

```bash
# Launch straight into a new deterministic world for a benchmark run
./SleakCraft -r vulkan --bench -world BenchRun -seed 1337 -rd 12 --no-vsync

# Windowed OpenGL session with validation off, a fixed title, and a small render distance
./SleakCraft -r opengl -w 1280 -h 720 -t My_Test_Window -rd 4
```

See @ref player-and-ui for in-game key bindings.
