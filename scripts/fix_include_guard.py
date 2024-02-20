from tree_sitter_languages import get_language, get_parser
from pprint import pprint

language = get_language('c')
parser = get_parser('c')

#egp = '/home/alwa-local/zephyrproject/zephyr/subsys/bluetooth/host/att_internal.h'
egp = '/home/alwa-local/zephyrproject/zephyr/tests/bluetooth/common/testlib/include/testlib/adv.h'
with open(egp, 'rb') as file:
    code = file.read()

tree = parser.parse(code)
node = tree.root_node


pattern_with_guard = '''
(translation_unit
        .
        (comment) @copyright
        .
        (preproc_ifdef name: (identifier) @guard_name
            .
            (preproc_def
                name: (identifier) @guard_define_name
            ) @guard_define
            .
            (_)? @content_start
            "#endif" @endif
            .
        )
        .
        (comment) @guard_name_reminder
        .
)
(#match? @guard_name "^[A-Z_]_H_$")
(#match? @guard_define_name "^[A-Z_]_H_$")
(#match? @guard_name_reminder "^[A-Z_]_H_$")
'''

stmt_str_query = language.query(pattern_with_guard)
stmt_strs = stmt_str_query.captures(node)
points = {
    capture_name: node for node, capture_name in stmt_strs
}

cr = b''
if points.get('copyright'):
    cr = points['copyright']

content = b''
if points.get('content_start'):
    content_start = points['content_start'].byte_range[0]
    content_end = points['endif'].byte_range[0]
    content = code[content_start:content_end]
    content = content.strip()

print(node.sexp())
pprint(points)
print(cr)
print(content)
