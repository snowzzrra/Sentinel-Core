"""Project AP marker identities into the native automap catalog."""
import argparse
import json
from pathlib import Path


def project(registry):
    rows = sorted({(entry['location_id'], entry['runtime_map']) for entry in registry['entries']})
    if len({code for code, _ in rows}) != len(rows):
        raise ValueError('Automap location belongs to multiple maps')
    return ('// Generated from checked_location_visuals.json; exact canonical map ownership.\n'
            'struct AutomapMarker { uint32_t location; const char* map; };\n'
            'constexpr AutomapMarker automap_markers[] = {\n' +
            ''.join(f'    {{{code}u, "{runtime_map}"}},\n' for code, runtime_map in rows) + '};\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('registry', type=Path)
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[1]/'src/automap_catalog.h')
    args = parser.parse_args()
    args.output.write_text(project(json.loads(args.registry.read_bytes())), encoding='utf8')
