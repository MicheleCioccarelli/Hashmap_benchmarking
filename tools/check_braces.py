#!/usr/bin/env python3
"""Reports unbalanced braces in a LaTeX file, with the line where the imbalance starts.

    usage: python3 tools/check_braces.py report.tex

Written because deleting a \\add{...} wrapper but leaving its opening brace does not stop
pdflatex: it only prints "(\\end occurred inside a group at level 1)" near the end of the log,
and everything after the stray brace silently sits inside a group.
"""
import re
import sys


def strip(line):
    line = re.sub(r'(?<!\\)%.*$', '', line)   # comments
    return re.sub(r'\\[{}]', '', line)        # escaped braces


def main(path):
    lines = open(path).read().split('\n')
    depth = 0
    last_balanced = 0
    problems = []

    for number, line in enumerate(lines, 1):
        code = strip(line)
        depth += code.count('{') - code.count('}')
        if depth < 0:
            problems.append((number, depth, line, 'closing brace with nothing open'))
            depth = 0
        elif line.strip() == '':
            if depth == 0:
                last_balanced = number
            elif not problems:
                problems.append((last_balanced + 1, depth,
                                 lines[last_balanced] if last_balanced < len(lines) else '',
                                 f'group still open ({depth}) at the blank line on {number}'))

    if depth != 0 and not problems:
        problems.append((len(lines), depth, '', 'file ends inside a group'))

    if not problems:
        print(f'{path}: braces balanced')
        return 0

    for number, level, line, why in problems:
        print(f'{path}:{number}: {why}')
        if line.strip():
            print(f'    {line[:100]}')
    return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else 'report.tex'))
