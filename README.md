# Intellightent

Intellightent is an SKSE plugin for Skyrim Special Edition that intelligently manages shadow-casting lights to overcome the engine's limit of 4 shadow-casting lights per scene.

If the number of shadow-casting lights in a scene exceeds this limit, the game arbitrarily selects four to be active and turns off the rest. Intellightent allows users to configure which four lights are chosen to cast shadows and converts the excess lights into normal, non-shadow-casting lights instead of disabling them.

Original mod by **meh321**.
Source code from [Nexus Mods](https://www.nexusmods.com/skyrimspecialedition/mods/172423).

## Requirements

- [Skyrim Special Edition](https://store.steampowered.com/app/489830/The_Elder_Scrolls_V_Skyrim_Special_Edition/) (SE 1.5.97 or AE 1.6.xxx)
- [SKSE64](https://skse.silverlock.org/)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
- [Visual C++ Redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170)

## Build Instructions

This project uses [xmake](https://xmake.io/) for building.

### Prerequisites

- [xmake](https://xmake.io/)
- [Visual Studio 2022](https://visualstudio.microsoft.com/) (with C++ Desktop Development workload)

### Building

1.  Clone the repository (including submodules):

    ```sh
    git clone --recurse-submodules https://github.com/alandtse/Intellightent.git
    cd Intellightent
    ```

    If you already cloned without `--recurse-submodules`, run:

    ```sh
    git submodule update --init --recursive
    ```

2.  Configure and build:

    ```sh
    xmake
    ```

    The build artifacts will be located in the `build` directory.

### Auto Deploy

Set the `SkyrimPluginTargets` environment variable to automatically copy the DLL and PDB to one or more destinations after each build. Separate multiple paths with `;`.

```sh
SkyrimPluginTargets=C:\Skyrim;D:\MO2\mods\Intellightent
```

Each path has `\SKSE\Plugins` appended automatically.

## Configuration

The plugin can be configured via `Data/SKSE/Plugins/Intellightent.ini` or `Data/SKSE/Plugins/Intellightent.user.ini`.

| Setting            | Default   | Description                                    |
| :----------------- | :-------- | :--------------------------------------------- |
| `iLightCount`      | `4`       | Number of shadow casting lights allowed.       |
| `bTryNormalLight`  | `true`    | Convert excess shadow lights to normal lights. |
| `iMaxConvertCount` | `32`      | Maximum number of lights to convert per frame. |
| `sScoreFormula`    | _see src_ | Formula to determine light priority.           |

## License

[GPL-3.0-or-later](COPYING) WITH [Modding Exception AND GPL-3.0 Linking Exception (with Corresponding Source)](EXCEPTIONS).

Specifically, the Modded Code includes:

- Skyrim (and its variants)

The Modding Libraries include:

- [SKSE](https://skse.silverlock.org/)
- Commonlib (and variants).
