#!/usr/bin/env python3

import json
import argparse


def read_json(file_path):
    with open(file_path, 'r') as file:
        return json.load(file)


def merge_json(base, overlay):
    """Recursively merge an overlay into a base JSON object."""
    if not isinstance(base, dict) or not isinstance(overlay, dict):
        return overlay

    merged = base.copy()
    for key, value in overlay.items():
        if key in merged:
            merged[key] = merge_json(merged[key], value)
        else:
            merged[key] = value

    return merged


def filter_json(data):
    """Recursively remove fields starting with an underscore from the JSON data."""
    if isinstance(data, dict):
        return {k: filter_json(v) for k, v in data.items() if not k.startswith('_')}
    elif isinstance(data, list):
        return [filter_json(item) for item in data]
    else:
        return data


def write_cpp_header(data, file_path):
    with open(file_path, 'w') as file:

        file.write("#pragma once\n\n")
        file.write("#include <stdint.h>\n\n")

        json_string = json.dumps(data, separators=(',', ':'))
        json_bytes = [f"0x{ord(char):02X}" for char in json_string]

        file.write(f"static constexpr uint16_t DEFAULT_GATAS_CONFIG_SIZE = {len(json_bytes) + 1};\n")
        file.write("static constexpr uint8_t DEFAULT_GATAS_CONFIG[] = {")

        for i, byte in enumerate(json_bytes):
            if i % 20 == 0:
                file.write("\n")
            file.write(f"{byte},")

        file.write("0x00};\n")  # Null terminator


def main():
    parser = argparse.ArgumentParser(description='Convert JSON file to a C++ constexpr array.')
    parser.add_argument('input_file', type=str, help='The input JSON file')
    parser.add_argument('output_file', type=str, help='The output C++ header file')
    parser.add_argument('--overlay', type=str,
                        help='Optional JSON file recursively merged over the input')
    args = parser.parse_args()

    data = read_json(args.input_file)
    if args.overlay:
        data = merge_json(data, read_json(args.overlay))

    filtered_data = filter_json(data)
    write_cpp_header(filtered_data, args.output_file)


if __name__ == '__main__':
    main()
