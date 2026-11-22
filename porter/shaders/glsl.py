from pathlib import Path
import re

DECLARATION = re.compile(r'^(uniform|(?:(?:flat|smooth|centroid) )?(?:in|out))\s+((?:(?:lowp|mediump|highp) )?)(\w+)\s+(\w+)\s*;$', re.M)


def locations(shaders, category):
    """Color co-occurrence graphs so shared varying names have shared locations.

    All inputs in this exact IPA are scalar/vector single-location declarations.
    Reject other layouts instead of accidentally overlapping matrix/array slots.
    """
    graph = {}
    for name, source in shaders.items():
        stage = Path(name).suffix[1:]
        names = []
        for declaration in DECLARATION.finditer(source):
            storage, precision, kind, symbol = declaration.groups()
            storage = storage.split()[-1]
            selected = (category == 'varying' and (stage, storage) in [('vert', 'out'), ('frag', 'in')]) or (category == 'attribute' and stage == 'vert' and storage == 'in')
            if not selected:
                continue
            if not re.fullmatch(r'(?:float|[iub]?vec[234])', kind):
                raise ValueError(f'Unsupported location width: {name}: {declaration.group()}')
            names.append(symbol)
        for symbol in names:
            graph.setdefault(symbol, set()).update(x for x in names if x != symbol)
    assigned = {}
    for symbol in sorted(graph, key=lambda x: (-len(graph[x]), x)):
        used = {assigned[x] for x in graph[symbol] if x in assigned}
        location = 0
        while location in used:
            location += 1
        assigned[symbol] = location
    return dict(sorted(assigned.items()))


def derive(name, source, varyings, attributes):
    if not source.startswith('#version 300 es') or '#include' in source:
        raise ValueError(f'Unsupported shader dialect/includes: {name}')
    declarations = list(DECLARATION.finditer(source))
    declared_lines = [line for line in source.splitlines() if re.match(r'^(uniform|in|out|flat|smooth|centroid)\b', line)]
    if len(declarations) != len(declared_lines):
        raise ValueError(f'Unparsed shader interface: {name}')
    stage = Path(name).suffix[1:]
    sampler_binding = 0
    output_location = 0
    interfaces = []
    samplers = []
    uniforms = []

    def rewrite(match):
        nonlocal sampler_binding, output_location
        storage, precision, kind, symbol = match.groups()
        io = storage.split()[-1]
        if storage == 'uniform':
            if kind.startswith('sampler'):
                descriptor_set = 1 if stage == 'vert' else 2
                binding = sampler_binding
                sampler_binding += 1
                samplers.append({'name': symbol, 'type': kind, 'set': descriptor_set, 'binding': binding})
                return f'layout(set={descriptor_set}, binding={binding}) ' + match.group()
            uniforms.append({'name': symbol, 'type': kind, 'precision': precision.strip()})
            return match.group()
        if (stage, io) in [('vert', 'out'), ('frag', 'in')]:
            location = varyings[symbol]
        elif stage == 'vert':
            location = attributes[symbol]
        else:
            location = output_location
            output_location += 1
        interfaces.append({'name': symbol, 'type': kind, 'storage': storage, 'location': location})
        return f'layout(location={location}) ' + match.group()

    derived = DECLARATION.sub(rewrite, source).replace('#version 300 es', '#version 310 es', 1)
    return derived, {'interfaces': interfaces, 'samplers': samplers, 'declared_uniforms': uniforms,
                     'uniform_block': {'name': 'I2AVertexUniforms' if stage == 'vert' else 'I2AFragmentUniforms', 'set': 0, 'binding': 0 if stage == 'vert' else 1}}


def native_vertex_flips_y(entry):
    """Follow the AIR position output, rather than applying a GLES-wide rule.

    The original xlat AIR already negates GLES Y for most passes. Scanout and a few
    sprites omit that transform. Vulkan's positive viewport needs the opposite
    sign of the actual Metal output in both cases.
    """
    path = Path(entry['llvm_ir'])
    raw = path.read_bytes()
    text = raw.decode('utf8').replace('\r\n', '\n')
    positions = [i for i, output in enumerate(entry['outputs']) if output['properties'].get('air.position')]
    if len(positions) != 1:
        raise ValueError('Ambiguous AIR position output')
    roots = re.findall(r'insertvalue .+, <4 x float> (%[\w.]+), ' + str(positions[0]) + r'$', text, re.M)
    if len(roots) != 1:
        raise ValueError('Unrecognized AIR position assignment')
    assignments = dict(re.findall(r'^\s*(%[\w.]+) = (.+)$', text, re.M))

    def follow(value, depth=0):
        if depth > 16:
            raise ValueError('AIR position chain too deep')
        expression = assignments[value]
        if expression.startswith('phi <4 x float> '):
            incoming = re.findall(r'\[ (%[\w.]+), %[\w.]+ \]', expression)
            signs = {follow(branch, depth + 1) for branch in incoming}
            if len(signs) != 1:
                raise ValueError('AIR branches disagree on clip Y')
            return signs.pop()
        insert = re.fullmatch(r'insertelement <4 x float> (%[\w.]+|poison|undef), float ([^,]+), i32 (\d+)', expression)
        if insert:
            previous, scalar, index = insert.groups()
            if index != '1':
                return follow(previous, depth + 1)
            scalar_expression = assignments.get(scalar, '')
            if re.fullmatch(r'fsub(?: fast)? float -0\.000000e\+00, %[\w.]+', scalar_expression):
                return True
            if scalar_expression == 'fmul float undef, -1.000000e+00':
                return True  # The original culled-vertex branch has undef position.
            raise ValueError('Unrecognized AIR clip Y write: ' + scalar_expression)
        if expression.startswith('shufflevector '):
            return False
        raise ValueError('Unrecognized AIR clip position: ' + expression)

    return follow(roots[0])



def uniform_expression(block, member, glsl_type):
    kind = member['type']
    match = re.fullmatch(r'(float|half)([234])?(?:x([234]))?', kind)
    if not match or member['extra'] != 0:
        raise ValueError(f'Unsupported native uniform: {member}')
    base, rows, columns = match.groups()
    rows, columns = int(rows or 1), int(columns or 1)
    expected = 'float' if rows == columns == 1 else f'vec{rows}' if columns == 1 else f'mat{columns}' if columns == rows else f'mat{columns}x{rows}'
    if glsl_type != expected:
        raise ValueError(f'Uniform type mismatch: GLSL {glsl_type}, AIR {kind}')
    scalar_size = 4 if base == 'float' else 2
    vector_stride = (4 if rows == 3 else rows) * scalar_size
    offset = member['offset']
    if offset < 0 or offset + member['size'] > block['size'] or (columns - 1) * vector_stride + rows * scalar_size > member['size']:
        raise ValueError(f'Uniform bounds mismatch: {member}')

    def scalar(position):
        if position % scalar_size:
            raise ValueError(f'Misaligned native scalar at {position}')
        word = f'_i2a_metal_b{block["index"]}._words[{position // 16}][{(position % 16) // 4}]'
        if base == 'half':
            return f'unpackHalf2x16({word})[{(position % 4) // 2}]'
        return f'uintBitsToFloat({word})'

    if columns == 1:
        values = [scalar(offset + index * scalar_size) for index in range(rows)]
        return values[0] if rows == 1 else f'{glsl_type}(' + ', '.join(values) + ')'
    vectors = [f'vec{rows}(' + ', '.join(scalar(offset + column * vector_stride + row * scalar_size) for row in range(rows)) + ')' for column in range(columns)]
    return f'{glsl_type}(' + ', '.join(vectors) + ')'


def derive_native(name, source, varyings, native):
    stage = 'vertex' if name.endswith('.vert') else 'fragment'
    entries = [entry for entry in native['entries'] if entry['stage'] == stage]
    if len(entries) != 1:
        raise ValueError(f'Ambiguous AIR entry for {name}')
    entry = entries[0]
    buffers = [arg for arg in entry['inputs'] if arg['kind'] == 'buffer']
    attributes = {arg['name']: arg for arg in entry['inputs'] if arg['kind'] == 'vertex_input'}
    textures = {arg['name']: arg for arg in entry['inputs'] if arg['kind'] == 'texture'}
    samplers = {arg['name']: arg for arg in entry['inputs'] if arg['kind'] == 'sampler'}
    targets = {arg['name']: arg for arg in entry['outputs'] if arg['kind'] == 'render_target'}
    members = {}
    for block in buffers:
        if not isinstance(block['index'], int) or not 0 <= block['index'] < 31:
            raise ValueError(f'Unsupported Metal buffer index: {block}')
        for member in block.get('members', []):
            if member['name'] in members:
                raise ValueError(f'Ambiguous Metal uniform: {member["name"]}')
            members[member['name']] = block, member
    descriptor_set = 0 if stage == 'vertex' else 1
    prologue = []
    used_blocks = {}
    uniform_map, sampler_map, interfaces = [], [], []

    def rewrite(match):
        storage, precision, kind, symbol = match.groups()
        io = storage.split()[-1]
        if storage == 'uniform':
            if kind.startswith('sampler'):
                texture = textures.get(symbol)
                matching_pairs = [pair for pair in entry['sampled_pairs'] if pair['texture'] == symbol]
                sampler_name = matching_pairs[0]['sampler'] if matching_pairs else '_mtlsmp_' + symbol
                if len({pair['sampler_index'] for pair in matching_pairs}) > 1:
                    raise ValueError(f'Multiple native samplers for {symbol}')
                sampler = samplers.get(sampler_name)
                if not texture or not sampler:
                    raise ValueError(f'No original texture/sampler metadata for {symbol}')
                ti, si = texture['index'], sampler['index']
                if not isinstance(ti, int) or not isinstance(si, int) or not 0 <= ti < 128 or not 0 <= si < 16:
                    raise ValueError(f'Unsupported native resource index for {symbol}')
                expected_texture = {'sampler2D': 'texture2d', 'sampler3D': 'texture3d', 'samplerCube': 'texturecube'}[kind]
                if not texture['type'].startswith(expected_texture + '<'):
                    raise ValueError(f'Texture dimension mismatch: {symbol}')
                prologue.extend([
                    f'layout(set={descriptor_set}, binding={32+ti}) uniform {precision}texture{kind[7:]} _i2a_tex_{symbol};',
                    f'layout(set={descriptor_set}, binding={160+si}) uniform sampler _i2a_smp_{symbol};',
                    f'#define {symbol} {kind}(_i2a_tex_{symbol}, _i2a_smp_{symbol})'
                ])
                sampler_map.append({'name': symbol, 'type': kind, 'set': descriptor_set, 'texture_index': ti, 'texture_binding': 32+ti,
                                    'sampler_index': si, 'sampler_binding': 160+si, 'pair_evidence': 'direct AIR sample operands' if matching_pairs else 'AIR argument names; no direct sample'})
                return ''
            if symbol not in members:
                if len(re.findall(r'\b' + re.escape(symbol) + r'\b', source)) == 1:
                    uniform_map.append({'name': symbol, 'unused': True})
                    return ''
                raise ValueError(f'No original constant member for {symbol}')
            block, member = members[symbol]
            expression = uniform_expression(block, member, kind)
            used_blocks[block['index']] = block
            prologue.append(f'#define {symbol} ({expression})')
            uniform_map.append({'name': symbol, 'glsl_type': kind, 'metal_type': member['type'], 'metal_buffer_index': block['index'],
                                'metal_offset': member['offset'], 'metal_size': member['size'], 'expression': expression})
            return ''
        if (stage, io) in [('vertex', 'out'), ('fragment', 'in')]:
            location = varyings[symbol]
        elif stage == 'vertex':
            if symbol not in attributes:
                raise ValueError(f'No original vertex attribute for {symbol}')
            location = attributes[symbol]['index']
        else:
            if symbol not in targets:
                raise ValueError(f'No original render target for {symbol}')
            location = targets[symbol]['properties']['air.render_target']
        interfaces.append({'name': symbol, 'type': kind, 'storage': storage, 'location': location})
        return f'layout(location={location}) ' + match.group()

    if not source.startswith('#version 300 es'):
        raise ValueError('Unrecognized source dialect')
    derived = DECLARATION.sub(rewrite, source).replace('#version 300 es', '#version 450', 1)
    derived = re.sub(r'\bgl_VertexID\b', 'gl_VertexIndex', derived)
    raw_blocks = []
    for index, block in sorted(used_blocks.items()):
        words = (block['size'] + 15) // 16
        raw_blocks.append(f'layout(std140, set={descriptor_set}, binding={index}) uniform I2AMetalBuffer{index} {{ uvec4 _words[{words}]; }} _i2a_metal_b{index};')
    prefix_end = max(match.end() for match in re.finditer(r'^#(?:version|extension)[^\n]*(?:\n|$)', derived, re.M))
    derived = derived[:prefix_end] + '\n' + '\n'.join(raw_blocks + prologue) + '\n' + derived[prefix_end:]
    if stage == 'vertex':
        derived, replaced = re.subn(r'\bvoid\s+main\s*\(\s*(?:void)?\s*\)', 'void i2aOriginalMain()', derived, count=1)
        if replaced != 1:
            raise ValueError('Cannot wrap vertex entry')
        air_flips_y = native_vertex_flips_y(entry)
        y_conversion = '' if air_flips_y else '    gl_Position.y = -gl_Position.y;\n'
        derived += '\nvoid main() {\n    i2aOriginalMain();\n' + y_conversion + '    gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;\n}\n'
    return derived, {'interfaces': interfaces, 'samplers': sampler_map, 'uniform_mapping': uniform_map,
                     'raw_uniform_blocks': [{'metal_index': index, 'set': descriptor_set, 'binding': index, 'native_size': block['size'], 'vulkan_size': ((block['size']+15)//16)*16} for index, block in sorted(used_blocks.items())],
                     'original_air': entry['llvm_ir'], 'original_entry': entry['entry'],
                     'clip_conversion': {'air_negates_gles_y': air_flips_y, 'vulkan_negates_gles_y': not air_flips_y,
                                         'depth': 'z=(z+w)*0.5'} if stage == 'vertex' else None}
