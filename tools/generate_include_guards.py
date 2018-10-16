#!/bin/env python

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# This Source Code Form is "Incompatible With Secondary Licenses", as
# defined by the Mozilla Public License, v. 2.0.

import os

HEADER_EXTENSION = '.hpp'

for root, dirs, files in os.walk('include'):
    relpath = os.path.relpath(root, 'include')
    for file in files:
        path = os.path.join(relpath, file)
        if path.endswith(HEADER_EXTENSION):
            full_path = os.path.join(root, file)
            guard_name = path.replace('/', '__').replace('.', '_').upper() + '_'

            print('%s: %s' % (full_path, guard_name))

            content = []
            with open(full_path, 'r') as f:
                content = [line.strip('\r').strip('\n') for line in f]

            # strip trailing line
            if content and content[-1] == '':
                content = content[:-1]

            # separate comments at the beginning
            comment_end = 0
            while comment_end < len(content) and \
                    (content[comment_end].lstrip(' ').startswith('/*') or
                        content[comment_end].lstrip(' ').startswith('*') or
                        content[comment_end].lstrip(' ').startswith('//') or
                        content[comment_end].lstrip(' ') == ''):
                comment_end += 1
            comments = content[:comment_end]
            content = content[comment_end:]

            # remove old guards
            if len(content) >= 4 and content[0].startswith('#ifndef ') and \
                    content[1].startswith('#define ') and \
                    content[2] == '' and \
                    content[-2] == '' and \
                    content[-1] == '#endif':
                content = content[3:-2]

            content = comments + \
                      ['#ifndef ' + guard_name,
                       '#define ' + guard_name,
                       ''] + \
                      content + \
                      ['',
                       '#endif']

            with open(full_path, 'w') as f:
                for line in content:
                    f.write(line + '\n')
