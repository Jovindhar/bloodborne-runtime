#!/usr/bin/env python3
"""Generate the in-game notification table from the public, reviewed trophy catalog."""
import json
import sys
from pathlib import Path


def quoted(value):
    return json.dumps(value, ensure_ascii=False)


catalog = json.loads(Path(__file__).with_name("trophy_catalog.json").read_text())
assert catalog["schema_version"] == 1 and catalog["default_locale"] == "en"
assert [t["id"] for t in catalog["trophies"]] == list(range(40))
rows = ["// Generated from trophy_catalog.json. Do not edit.",
        "static constexpr TrophyText kTrophyTexts[] = {"]
for trophy in catalog["trophies"]:
    for language, text in trophy["translations"].items():
        rows.append(f'    {{{trophy["id"]}, {quoted(language)}, {quoted(text["name"])}, '
                    f'{quoted(text.get("description", ""))}, {quoted(trophy["grade"])}, '
                    f'{quoted(trophy["group"])}}},')
rows.append("};")
Path(sys.argv[1]).write_text("\n".join(rows) + "\n")
