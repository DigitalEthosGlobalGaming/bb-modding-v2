"""Conservative Lua local-name recovery for reference copies, using lexical bindings.

No execution, third-party packages or game-specific semantic guesses. Unsupported syntax
fails closed. Token fingerprints after reparsing establish lexical alpha-equivalence,
not runtime equivalence (debug/reflection can observe local variable names).
"""
from dataclasses import dataclass, field
import re

KEYWORDS = set('and break do else elseif end false for function if in local nil not or repeat return then true until while'.split())
GENERATED = re.compile(r'f\d+_(?:arg|local)\d+\Z')
IDENT = re.compile(r'[A-Za-z_][A-Za-z_0-9]*')
NUM = re.compile(r'(?:0[xX][0-9a-fA-F]+|(?:\d+\.(?!\.)\d*|\d+|\.\d+)(?:[eE][+-]?\d+)?)')
LONG = re.compile(r'\[(=*)\[')
AI_METHODS = {'GetEventRequest', 'GetNumber', 'SetNumber', 'HasSpecialEffectId',
              'GetDist', 'IsBattleState', 'IsCautionState', 'IsFindState', 'IsSearchTarget',
              'IsInterupt', 'GetNpcThinkParamID', 'GetHpRate', 'AddTopGoal', 'GetRandam_Int',
              'GetRandam_Float', 'SetTimer', 'IsFinishTimer', 'Replaning'}
GOAL_METHODS = {'AddSubGoal', 'ClearSubGoal', 'GetSubGoalNum', 'GetLife', 'SetLife'}
PRECEDENCE = {'or': 1, 'and': 2, '<': 3, '>': 3, '<=': 3, '>=': 3, '~=': 3, '==': 3,
              '..': 4, '+': 5, '-': 5, '*': 6, '/': 6, '%': 6, '^': 8}
# Reviewed argument roles in the 1.09 aicommon/common_battle_func helper definitions.
# These are named-helper contracts, not universal conclusions about arbitrary tables.
HELPER_ROLES = {
    'Common_Clear_Param': ('action_weights', 'action_handlers', 'action_parameters'),
    'Common_Battle_Activate': ('ai', 'goal', 'action_weights', 'action_handlers',
                               'after_action_callback', 'action_parameters'),
    'REGIST_FUNC': ('ai', 'goal', 'action_callback', 'callback_argument'),
}


@dataclass
class Token:
    value: str
    kind: str
    start: int
    end: int
    line: int


def lex(text):
    ts, i, line = [], 0, 1
    while i < len(text):
        start, at_line = i, line
        kind = None
        if text[i].isspace() or text[i] == '\ufeff':
            i += 1
        elif text.startswith('--', i):
            m = LONG.match(text, i + 2)
            if m:
                close = ']' + m[1] + ']'
                end = text.find(close, m.end())
                if end < 0:raise ValueError(f'Unterminated comment at line {line}')
                i = end + len(close)
            else:
                end = text.find('\n', i)
                i = len(text) if end < 0 else end
        elif text[i] in ('"', "'"):
            quote = text[i]
            i += 1
            while i < len(text) and text[i] != quote:
                if text[i] in '\r\n':raise ValueError(f'Unescaped string newline at line {line}')
                if text[i] == '\\':
                    i += 1
                    if i < len(text) and text[i:i + 2] == '\r\n':i += 1
                i += 1
            if i >= len(text):raise ValueError(f'Unterminated string at line {line}')
            i += 1
            kind = 'string'
        elif (m := LONG.match(text, i)):
            close = ']' + m[1] + ']'
            end = text.find(close, m.end())
            if end < 0:raise ValueError(f'Unterminated long string at line {line}')
            i = end + len(close)
            kind = 'string'
        elif (m := IDENT.match(text, i)):
            i = m.end()
            kind = 'keyword' if m[0] in KEYWORDS else 'name'
        elif (m := NUM.match(text, i)):
            i = m.end()
            kind = 'number'
        else:
            op = next((v for v in ('...', '..', '==', '~=', '<=', '>=') if text.startswith(v, i)), None)
            i += len(op) if op else 1
            kind = 'symbol'
        if kind:ts.append(Token(text[start:i], kind, start, i, at_line))
        line += text[start:i].count('\n')
    ts.append(Token('<eof>', 'eof', len(text), len(text), line))
    return ts


@dataclass
class Binding:
    name: str
    kind: str
    declaration: int
    refs: list = field(default_factory=list)
    writes: int = 0
    initial: object = None
    methods: set = field(default_factory=set)
    roles: dict = field(default_factory=dict)


class Parser:
    def __init__(self, text):
        self.ts = lex(text)
        self.i = 0
        self.scopes = [{}]
        self.bindings = []
        self.resolution = {}
        self.calls = []
        self.global_writes = set()

    def value(self):return self.ts[self.i].value

    def take(self, expected=None):
        if expected is not None and self.value() != expected:
            raise ValueError(f'Expected {expected!r}, got {self.value()!r} at line {self.ts[self.i].line}')
        at = self.i
        self.i += 1
        return at

    def accept(self, value):
        if self.value() == value:
            self.take()
            return True
        return False

    def name(self):
        if self.ts[self.i].kind != 'name':
            raise ValueError(f'Expected identifier at line {self.ts[self.i].line}')
        return self.take()

    def declare(self, token, kind):
        binding = len(self.bindings)
        self.bindings.append(Binding(self.ts[token].value, kind, token))
        self.scopes[-1][self.ts[token].value] = binding
        self.resolution[token] = binding
        return binding

    def reference(self, token):
        for scope in reversed(self.scopes):
            if self.ts[token].value in scope:
                binding = scope[self.ts[token].value]
                self.resolution[token] = binding
                if binding is not None:self.bindings[binding].refs.append(token)
                return
        self.resolution[token] = None

    def parse(self):
        self.block({'<eof>'})
        self.take('<eof>')
        self.helper_hints()
        return self

    def helper_hints(self):
        for call in self.calls:
            callee, args = call[1], call[2]
            if callee[0] != 'name' or self.resolution.get(callee[1]) is not None:continue
            name = self.ts[callee[1]].value
            roles = HELPER_ROLES.get(name)
            if roles is None or name in self.global_writes:continue
            if len(args) != len(roles) and not (name == 'REGIST_FUNC' and len(args) == 3):continue
            for position, (arg, role) in enumerate(zip(args, roles), 1):
                while arg[0] == 'paren':arg = arg[1]
                if arg[0] != 'name':continue
                binding = self.resolution.get(arg[1])
                if binding is not None:
                    evidence = f'reviewed helper contract: {name} argument {position} (source line {self.ts[callee[1]].line})'
                    self.bindings[binding].roles.setdefault(role, []).append(evidence)

    def block(self, stops, scoped=False):
        if scoped:self.scopes.append({})
        while self.value() not in stops:
            self.statement()
            self.accept(';')
        if scoped:self.scopes.pop()

    def statement(self):
        if self.accept(';'):return
        if self.accept('local'):
            if self.accept('function'):
                binding = self.declare(self.name(), 'local_function')
                self.function_body()
                return
            names = [self.name()]
            while self.accept(','):names.append(self.name())
            # Lua locals become visible AFTER the initializer list.
            values = self.expr_list() if self.accept('=') else []
            for pos, token in enumerate(names):
                binding = self.declare(token, 'local')
                # Last expression may return several values; do not name those results.
                if len(values) == len(names):self.bindings[binding].initial = values[pos]
            return
        if self.accept('function'):
            token = self.name()
            self.reference(token)
            target = ('name', token)
            while self.accept('.'):
                target = ('field', target, self.ts[self.name()].value)
            method = self.accept(':')
            if method:self.name()
            self.mark_write(target)
            self.function_body(method)
            return
        if self.accept('do'):
            self.block({'end'}, True);self.take('end');return
        if self.accept('while'):
            self.expr();self.take('do');self.block({'end'}, True);self.take('end');return
        if self.accept('repeat'):
            self.scopes.append({})
            self.block({'until'});self.take('until');self.expr()
            self.scopes.pop();return
        if self.accept('if'):
            self.expr();self.take('then');self.block({'elseif', 'else', 'end'}, True)
            while self.accept('elseif'):
                self.expr();self.take('then');self.block({'elseif', 'else', 'end'}, True)
            if self.accept('else'):self.block({'end'}, True)
            self.take('end');return
        if self.accept('for'):
            names = [self.name()]
            if self.accept('='):
                self.expr();self.take(',');self.expr()
                if self.accept(','):self.expr()
                kind = 'numeric_for'
            else:
                while self.accept(','):names.append(self.name())
                self.take('in');self.expr_list();kind = 'generic_for'
            self.take('do');self.scopes.append({})
            for token in names:self.declare(token, kind)
            self.block({'end'});self.take('end');self.scopes.pop();return
        if self.accept('return'):
            if self.value() not in ('end', 'else', 'elseif', 'until', ';', '<eof>'):self.expr_list()
            return
        if self.accept('break'):return
        first = self.prefix()
        if self.value() in (',', '='):
            targets = [first]
            while self.accept(','):targets.append(self.prefix())
            self.take('=');self.expr_list()
            for target in targets:self.mark_write(target)
        elif first[0] != 'call':
            raise ValueError(f'Expected assignment or call at line {self.ts[self.i].line}')

    def mark_write(self, expr):
        if expr[0] not in ('name', 'field', 'index'):raise ValueError('Invalid assignment target')
        if expr[0] == 'name':
            binding = self.resolution.get(expr[1])
            if binding is not None:self.bindings[binding].writes += 1
            else:self.global_writes.add(self.ts[expr[1]].value)

    def function_body(self, method=False):
        self.take('(')
        self.scopes.append({})
        # Implicit method self shadows any enclosing self but has no source declaration.
        if method:self.scopes[-1]['self'] = None
        if self.value() != ')':
            if not self.accept('...'):
                self.declare(self.name(), 'parameter')
                while self.accept(','):
                    if self.accept('...'):break
                    self.declare(self.name(), 'parameter')
        self.take(')');self.block({'end'});self.take('end');self.scopes.pop()

    def expr_list(self):
        values = [self.expr()]
        while self.accept(','):values.append(self.expr())
        return values

    def expr(self, minimum=1):
        if self.value() in ('not', '-', '#'):
            op = self.ts[self.take()].value
            left = ('unary', op, self.expr(7))
        elif self.accept('function'):
            self.function_body();left = ('function',)
        elif self.value() in ('nil', 'true', 'false', '...') or self.ts[self.i].kind in ('number', 'string'):
            left = ('literal', self.take())
        elif self.value() == '{':
            left = self.table()
        else:
            left = self.prefix()
        while PRECEDENCE.get(self.value(), 0) >= minimum:
            op = self.ts[self.take()].value
            right = self.expr(PRECEDENCE[op] if op in ('^', '..') else PRECEDENCE[op] + 1)
            left = ('binary', op, left, right)
        return left

    def table(self):
        self.take('{')
        while self.value() != '}':
            if self.accept('['):
                self.expr();self.take(']');self.take('=');self.expr()
            elif self.ts[self.i].kind == 'name' and self.ts[self.i + 1].value == '=':
                self.name();self.take('=');self.expr()  # record key, not a variable
            else:self.expr()
            if not self.accept(',') and not self.accept(';'):break
        self.take('}')
        return ('table',)

    def prefix(self):
        if self.accept('('):
            result = ('paren', self.expr());self.take(')')
        else:
            token = self.name();self.reference(token);result = ('name', token)
        while True:
            if self.accept('.'):
                result = ('field', result, self.ts[self.name()].value)
            elif self.accept('['):
                index = self.expr();self.take(']');result = ('index', result, index)
            elif self.accept(':'):
                method = self.ts[self.name()].value
                receiver = result
                args = self.call_args()
                result = ('call', ('method', receiver, method), args)
                self.calls.append(result)
                if receiver[0] == 'name':
                    binding = self.resolution.get(receiver[1])
                    if binding is not None:self.bindings[binding].methods.add(method)
            elif self.value() in ('(', '{') or self.ts[self.i].kind == 'string':
                result = ('call', result, self.call_args())
                self.calls.append(result)
            else:return result

    def call_args(self):
        if self.accept('('):
            args = self.expr_list() if self.value() != ')' else []
            self.take(')');return args
        if self.value() == '{':return [self.table()]
        if self.ts[self.i].kind == 'string':return [('literal', self.take())]
        raise ValueError('Expected call arguments')

    def fingerprint(self):
        return [(t.kind, ('binding', self.resolution[i]) if self.resolution.get(i) is not None else t.value)
                for i, t in enumerate(self.ts)]


def snake(name):
    name = re.sub(r'([A-Z]+)([A-Z][a-z])', r'\1_\2', name)
    return re.sub(r'([a-z0-9])([A-Z])', r'\1_\2', name).lower().strip('_')


def suggestion(binding, parser):
    if binding.writes:return None
    ai = sorted(binding.methods & AI_METHODS)
    goal = sorted(binding.methods & GOAL_METHODS)
    roles = set(binding.roles)
    if ai:roles.add('ai')
    if goal:roles.add('goal')
    if len(roles) > 1:return None
    if binding.roles:
        role = next(iter(roles))
        return role, '; '.join(sorted(set(binding.roles[role])))
    if ai and not goal:return 'ai', 'receiver of AI API methods: ' + ', '.join(ai)
    if goal and not ai:return 'goal', 'receiver of goal API methods: ' + ', '.join(goal)
    if binding.kind == 'numeric_for':return 'loop_index', 'numeric for-loop control variable'
    expr = binding.initial
    while expr and expr[0] == 'paren':expr = expr[1]
    if expr and expr[0] == 'call' and expr[1][0] == 'name':
        callee = expr[1][1]
        if (parser.ts[callee].value == 'REGIST_FUNC' and parser.resolution.get(callee) is None
                and 'REGIST_FUNC' not in parser.global_writes and len(expr[2]) in (3, 4)):
            return 'registered_callback', 'closure result of reviewed REGIST_FUNC helper contract'
    if not expr or expr[0] != 'call' or expr[1][0] != 'method':return None
    method, args = expr[1][2], expr[2]
    if not re.fullmatch(r'(?:Get|Is|Has)[A-Z][A-Za-z_0-9]*', method):return None
    base = snake(method[3:] if method.startswith('Get') else method)
    if method == 'GetNumber':base = 'ai_number'
    if method == 'GetRandam_Int':base = 'random_int'
    if method == 'GetRandam_Float':base = 'random_float'
    suffixes = []
    for arg in args:
        if arg[0] == 'literal' and parser.ts[arg[1]].kind == 'number':
            suffixes.append(re.sub(r'[^a-z0-9]+', '_', parser.ts[arg[1]].value.lower()))
        elif arg[0] == 'name' and parser.resolution.get(arg[1]) is None:
            name = parser.ts[arg[1]].value
            if re.fullmatch(r'[A-Z][A-Z0-9_]*', name):suffixes.append(name.lower())
    result = '_'.join([base] + suffixes)
    if len(result) > 80:result = base
    return result, f'result of {method}; literal/constant arguments retained in name'


def recover(text):
    """Return reversible edits only after reparsing confirms binding/token equivalence."""
    try:
        parser = Parser(text).parse()
        occupied = {t.value for t in parser.ts if t.kind == 'name'} | KEYWORDS
        mappings, edits = [], []
        for ident, binding in enumerate(parser.bindings):
            if not GENERATED.fullmatch(binding.name):continue
            proposed = suggestion(binding, parser)
            if proposed is None:continue
            base, evidence = proposed
            name, suffix = base, 2
            while name in occupied:
                name = f'{base}_{suffix}';suffix += 1
            occupied.add(name)
            indices = [binding.declaration] + binding.refs
            # Names are unique across the file, including globals/fields, to avoid capture.
            binding_edits = [{'start': parser.ts[i].start, 'end': parser.ts[i].end,
                              'old': binding.name, 'new': name, 'line': parser.ts[i].line}
                             for i in sorted(set(indices))]
            mappings.append({'binding': ident, 'original': binding.name, 'readable': name,
                             'kind': binding.kind, 'declaration_line': parser.ts[binding.declaration].line,
                             'evidence': evidence, 'occurrences': len(binding_edits)})
            edits.extend(binding_edits)
        edits.sort(key=lambda e: e['start'])
        rewritten = apply_edits(text, edits)
        checked = Parser(rewritten).parse()
        if parser.fingerprint() != checked.fingerprint():
            raise ValueError('Binding/token fingerprint changed')
        return {'status': 'verified', 'bindings': len(parser.bindings), 'renames': mappings, 'edits': edits,
                'validation': 'Reparsed lexical binding/token equivalence; no execution or gameplay validation.'}
    except (ValueError, RecursionError) as error:
        return {'status': 'unsupported', 'reason': str(error), 'renames': [], 'edits': []}


def apply_edits(text, edits):
    pieces, cursor = [], 0
    for edit in edits:
        if edit['start'] < cursor or text[edit['start']:edit['end']] != edit['old']:
            raise ValueError('Invalid or stale readability edit')
        pieces.extend((text[cursor:edit['start']], edit['new']))
        cursor = edit['end']
    pieces.append(text[cursor:])
    return ''.join(pieces)
