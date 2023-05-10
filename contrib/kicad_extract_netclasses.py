#!/usr/bin/env python3
#
# kicad_extract_netclasses.py
#
#

import sys
import re
import json


def parse_kicad_project_json(json_data):
    j = json.loads(json_data)
    for netclass in j['net_settings']['classes']:
        name = re.sub(r'^.*_|ma$', '', netclass['name'])
        if 'nets' in netclass:
            for net in netclass['nets']:
                print('%s,"%s"' % (name, net))



for file in sys.argv[1:]:
    try:
        with open(file) as f:
            data = f.read()
            parse_kicad_project_json(data)
            exit(0)
    except IOError as e:
        print(e)


# eof
