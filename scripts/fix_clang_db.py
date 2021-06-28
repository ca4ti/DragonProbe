#!/usr/bin/env python3

import json
import sys

with open("compile_commands.json", "r") as f:
    data = json.load(f)

include = " -I/usr/arm-none-eabi/include"

for i in range(len(data)):
    if include not in data[i]["command"]:
        data[i]["command"] += include

with open("compile_commands.json", "w") as f:
    json.dump(data, f)
