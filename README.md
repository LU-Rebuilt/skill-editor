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

## Behavior Schema

The editor uses `behavior_schema.json` to define parameter names, types, and dynamic parameter patterns for each behavior template. This drives the node property editor — without a schema, behaviors are shown with raw database fields only.

**Loading order:**
1. `behavior_schema.json` next to the `skill_editor` executable
2. `behavior_schema.json` in the current working directory
3. `behavior_schema.json` in the source directory (development)
4. **Built-in fallback** — a copy of the schema is embedded in the executable at compile time. If no external file is found, the built-in version is used and a warning dialog is shown.

To customize the schema, place your own `behavior_schema.json` next to the executable. The file format is:

```json
{
  "behaviors": {
    "BehaviorName": {
      "template_id": 1,
      "params": [
        {"name": "param_name", "type": "float"},
        {"name": "ref_param", "type": "behavior", "back_ref": true},
        {"prefix": "dynamic_", "type": "float"},
        {"group": [{"prefix": "x_", "type": "float"}, {"prefix": "y_", "type": "float"}]}
      ]
    }
  }
}
```

**Parameter types:** `float`, `int`, `behavior` (reference to another behavior), `string`

**Special fields:**
- `back_ref: true` — marks a parameter as a behavior back-reference (shown in the tree graph)
- `prefix` — dynamic parameter: matches any column starting with this prefix
- `group` — paired dynamic parameters that repeat together
- Parameters tagged with `[B]` in the schema were confirmed from binary RE and should not be removed

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

