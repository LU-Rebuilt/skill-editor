# skill-editor

Qt6 graphical editor for LEGO Universe skill behavior trees.

> **Note:** This project was developed with significant AI assistance (Claude by Anthropic). All code has been reviewed and validated by the project maintainer, but AI-generated code may contain subtle issues. Contributions and reviews are welcome.

Part of the [LU-Rebuilt](https://github.com/LU-Rebuilt) project.

## Usage

```
skill_editor [cdclient.sqlite]
```

**Features:**
- Load CDClient SQLite databases
- Skill list with search and goto-behavior
- Zoomable graph visualization of behavior trees
- Node property editor with parameter definitions from behavior_schema.json
- Full undo/redo support
- Clone skills and subtrees
- Save/load behavior templates as JSON
- Locale file loading for localized skill names

## Building

```bash
cmake -B build
cmake --build build -j$(nproc)
```

For local development:

```bash
cmake -B build -DFETCHCONTENT_SOURCE_DIR_TOOL_COMMON=/path/to/local/tool-common
```

## Acknowledgments

- **[DarkflameServer](https://github.com/DarkflameServer/DarkflameServer)** — Behavior tree structure and CDClient database schema reference
- **Ghidra reverse engineering** of the original LEGO Universe client binary — behavior parameter identification

Third-party libraries:
- **[nlohmann/json](https://github.com/nlohmann/json)** v3.11.3 — JSON parsing (MIT license)

## License

[GNU Affero General Public License v3.0](https://www.gnu.org/licenses/agpl-3.0.html) (AGPLv3)

