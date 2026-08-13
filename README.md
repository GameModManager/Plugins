# Plugins

Plugin system and plugin implementations for GameModManager.

## Plugin System Architecture

The plugin system provides:
- Plugin discovery and loading (`.so`/`.dll`)
- Game support registration
- Feature and tool plugin interfaces
- Python plugin support via ABI bridge

## Directory Structure

```
include/engine/
├── plugins/                    # Core plugin interfaces
│   ├── plugin_info.h          # GamePlugin struct (mod metadata)
│   ├── plugin_file.h          # Plugin file type detection
│   ├── plugin_database.h      # Plugin database management
│   └── esp_header.h           # TES4 header parsing
└── plugin_host/                # Plugin hosting infrastructure
    ├── plugin_loader.h        # Plugin loading and management
    ├── diagnostics_registry.h # Diagnostic provider registration
    └── python_loader.h        # Python plugin support

src/
├── plugin_database.cpp        # Plugin database implementation
├── esp_header.cpp             # TES4 header parsing implementation
└── plugin_host/
    ├── plugin_loader.cpp      # Plugin loading implementation
    ├── diagnostics_registry.cpp
    └── python_loader.cpp      # Python plugin loader
```

## Plugin Types

### Game Plugins
Register game support via `register_identity()`. Provide:
- Game detection
- Instance creation
- Mod management
- Plugin sorting

### Tool Plugins
Provide utility features:
- Image diff/merge
- Mod sorting algorithms
- Custom diagnostics

### Feature Plugins
Extend existing functionality:
- Custom source providers
- Additional file handlers

## Plugin Interface

### GamePlugin Struct
Represents a discovered plugin file (.esm/.esp/.esl):
- `name` - Filename
- `owner_mod` - Mod providing this plugin
- `masters` - Required master files
- `is_master_flagged` - ESM header flag
- `is_light_flagged` - ESL header flag
- `is_medium_flagged` - ESH header flag (Starfield)

### Plugin Database
Manages plugin discovery and load order:
- `refresh()` - Discover plugins from game data
- `sort_load_order()` - Topological sort by masters
- `load_profile()` / `save_profile()` - MO2-compatible profiles
- `write_plugins_txt_for_launch()` - Generate plugins.txt

## Building

This module requires:
- C++17 compiler
- CMake 3.16+
- Qt 6 (for UI integration)

## Dependencies

- `engine::GameKnowledge` - Game metadata
- `engine::PlatformInterface` - Platform-specific paths
- `engine::PluginDatabase` - Plugin management