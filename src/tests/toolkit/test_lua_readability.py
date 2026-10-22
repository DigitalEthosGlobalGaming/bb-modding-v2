"""Adversarial lexical binding tests; these do not establish gameplay equivalence."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
import lua_readability as r


class ReadabilityTests(unittest.TestCase):
    def rewrite(self, text):
        result = r.recover(text)
        self.assertEqual(result['status'], 'verified', result)
        rewritten = r.apply_edits(text, result['edits'])
        self.assertEqual(r.Parser(text).parse().fingerprint(), r.Parser(rewritten).parse().fingerprint())
        self.assertEqual(text.count('\n'), rewritten.count('\n'))
        # Reverse only the recorded substitutions; all other bytes must be intact.
        cursor, inverse = 0, []
        for edit in result['edits']:
            start = edit['start'] + cursor
            inverse.append(dict(start=start, end=start + len(edit['new']), old=edit['new'], new=edit['old']))
            cursor += len(edit['new']) - (edit['end'] - edit['start'])
        self.assertEqual(r.apply_edits(rewritten, inverse), text)
        return rewritten, result

    def test_api_results_and_parameter_roles(self):
        text = '''function Test(f1_arg0, f1_arg1)
local f1_local0 = f1_arg0:GetEventRequest()
local f1_local1 = f1_arg0:GetNumber(7)
local f1_local2 = f1_arg0:HasSpecialEffectId(TARGET_SELF, 5410)
f1_arg1:ClearSubGoal()
return f1_local0, f1_local1, f1_local2
end'''
        rewritten, result = self.rewrite(text)
        self.assertIn('function Test(ai, goal)', rewritten)
        self.assertIn('local event_request = ai:GetEventRequest()', rewritten)
        self.assertIn('local ai_number_7 = ai:GetNumber(7)', rewritten)
        self.assertIn('local has_special_effect_id_target_self_5410', rewritten)
        self.assertEqual(len(result['renames']), 5)

    def test_shadowing_closure_and_initializer_scope(self):
        text = '''function Test(f1_arg0)
local f1_local0 = f1_arg0:GetEventRequest()
do
 local f1_local0 = f1_arg0:GetNumber(f1_local0)
 local function nested() return f1_local0 end
end
return function() return f1_local0 end
end'''
        rewritten, _ = self.rewrite(text)
        self.assertIn('local ai_number = ai:GetNumber(event_request)', rewritten)
        self.assertIn('local function nested() return ai_number end', rewritten)
        self.assertIn('return function() return event_request end', rewritten)

    def test_collision_and_global_field_string_comment_preservation(self):
        text = '''function Test(f1_arg0)
print(ai, event_request, event_request_2)
-- f1_arg0:GetEventRequest() and f1_local0
local f1_local0 = f1_arg0:GetEventRequest()
local t = {f1_local0 = f1_local0, [f1_local0] = "f1_local0"}
t.f1_local0 = t:f1_local0(f1_local0)
return [=[f1_local0]=], f1_local0
end'''
        rewritten, _ = self.rewrite(text)
        self.assertIn('function Test(ai_2)', rewritten)
        self.assertIn('local event_request_3 = ai_2:GetEventRequest()', rewritten)
        self.assertIn('{f1_local0 = event_request_3, [event_request_3] = "f1_local0"}', rewritten)
        self.assertIn('t.f1_local0 = t:f1_local0(event_request_3)', rewritten)
        self.assertIn('-- f1_arg0:GetEventRequest() and f1_local0', rewritten)
        self.assertIn('[=[f1_local0]=]', rewritten)

    def test_repeat_for_and_branch_scopes(self):
        text = '''function Test(f1_arg0)
local f1_local0 = f1_arg0:GetNumber(7)
for f1_local0 = f1_local0, 10 do print(f1_local0) end
for f1_local0, f1_local1 in pairs({}) do print(f1_local0, f1_local1) end
repeat local f1_local0 = f1_arg0:GetTimer(2) until f1_local0 > 0
if true then local f1_local0 = f1_arg0:GetEventRequest() print(f1_local0)
else print(f1_local0) end
return f1_local0
end'''
        rewritten, _ = self.rewrite(text)
        self.assertIn('for loop_index = ai_number_7, 10 do print(loop_index)', rewritten)
        self.assertIn('for f1_local0, f1_local1 in pairs({})', rewritten)
        self.assertIn('until timer_2 > 0', rewritten)
        self.assertIn('else print(ai_number_7) end', rewritten)

    def test_reassignment_and_unknowns_stay_unchanged(self):
        text = '''function Test(f1_arg0)
local f1_local0 = f1_arg0:GetEventRequest()
local function later() f1_local0 = 1 end
local f1_local1, f1_local2 = f1_arg0:GetNumber(7)
local f1_local3 = 5410
return f1_local0, f1_local1, f1_local2, f1_local3
end'''
        rewritten, result = self.rewrite(text)
        self.assertEqual([v['original'] for v in result['renames']], ['f1_arg0'])
        self.assertIn('local f1_local3 = 5410', rewritten)

    def test_local_recursion_methods_and_parenthesized_calls(self):
        text = '''local function f1_local0(f1_arg0)
 f1_arg0:GetNumber(7)
 if false then return f1_local0(f1_arg0) end
end
local self = {}
function self:f1_arg0(f2_arg0)
 self:f1_local0()
 local f2_local0 = (f2_arg0:GetEventRequest())
 return f2_local0
end
f1_local0 {x=1}
f1_local0 "literal"
'''
        rewritten, _ = self.rewrite(text)
        self.assertIn('function self:f1_arg0(ai_2)', rewritten)
        self.assertIn('self:f1_local0()', rewritten)
        self.assertIn('local function f1_local0(ai)', rewritten)
        self.assertIn('local event_request = (ai_2:GetEventRequest())', rewritten)

    def test_unrecognized_syntax_fails_closed(self):
        for text in ('local f1_local0 = 1 & 2', 'goto label', 'function F(f1_arg0) f1_arg0:GetNumber(7)',
                     'local f1_local0 = "broken', 'local f1_local0 <const> = 1'):
            result = r.recover(text)
            self.assertEqual(result['status'], 'unsupported', text)
            self.assertEqual(result['edits'], [])
            self.assertEqual(r.apply_edits(text, result['edits']), text)

    def test_conflicting_receiver_evidence_and_descriptive_names_preserved(self):
        text = '''function Test(existing_ai)
local f1_local0 = existing_ai:GetNumber(7)
f1_local0:GetEventRequest()
f1_local0:ClearSubGoal()
local meaningful_name = existing_ai:GetEventRequest()
return meaningful_name
end'''
        rewritten, result = self.rewrite(text)
        self.assertEqual(result['renames'], [])
        self.assertEqual(rewritten, text)

    def test_operator_precedence_long_comments_bom_and_crlf(self):
        text = '\ufeff--[==[ fake local f1_local0 ]==]\r\nfunction F(f1_arg0)\r\nlocal f1_local0 = f1_arg0:GetNumber(7)\r\nreturn -f1_local0^2 .. "x" .. "y", .5 + 1e-2, 1..2\r\nend\r\n'
        rewritten, _ = self.rewrite(text)
        self.assertTrue(rewritten.startswith('\ufeff--[==['))
        self.assertEqual(rewritten.count('\r\n'), text.count('\r\n'))

    def test_battle_helper_argument_roles_and_registered_result(self):
        text = '''function Example(f2_arg0, f2_arg1)
local f2_local0, f2_local1, f2_local2 = {}, {}, {}
Common_Clear_Param(f2_local0, f2_local1, f2_local2)
f2_local0[3] = 25
f2_local1[3] = REGIST_FUNC(f2_arg0, f2_arg1, ExampleAction)
local f2_local6 = REGIST_FUNC(f2_arg0, f2_arg1, ExampleAfter)
Common_Battle_Activate(f2_arg0, f2_arg1, f2_local0, f2_local1, f2_local6, f2_local2)
end'''
        rewritten, result = self.rewrite(text)
        self.assertIn('function Example(ai, goal)', rewritten)
        self.assertIn('local action_weights, action_handlers, action_parameters = {}, {}, {}', rewritten)
        self.assertIn('action_handlers[3] = REGIST_FUNC(ai, goal, ExampleAction)', rewritten)
        self.assertIn('local after_action_callback = REGIST_FUNC(ai, goal, ExampleAfter)', rewritten)
        self.assertTrue(any('helper contract' in row['evidence'] for row in result['renames']))

    def test_shadowed_redefined_conflicting_and_wrong_arity_helpers(self):
        for prefix in ('local Common_Clear_Param = replacement\n',
                       'function Common_Clear_Param(a,b,c) return a end\n'):
            text = prefix + 'local f1_local0 = {}\nCommon_Clear_Param(f1_local0, {}, {})\n'
            rewritten, result = self.rewrite(text)
            self.assertEqual(result['renames'], [])
        for text in ('local f1_local0 = {}\nCommon_Clear_Param(f1_local0, f1_local0, {})',
                     'local f1_local0 = {}\nCommon_Clear_Param(f1_local0, {})'):
            _, result = self.rewrite(text)
            self.assertEqual(result['renames'], [])

    def test_registered_callback_without_specific_role_and_spelling(self):
        text = '''function Example(f1_arg0, f1_arg1)
local f1_local0 = REGIST_FUNC(f1_arg0, f1_arg1, Callback, nil)
local f1_local1 = f1_arg0:GetRandam_Int(2, 9)
return f1_local0, f1_local1
end'''
        rewritten, _ = self.rewrite(text)
        self.assertIn('local registered_callback = REGIST_FUNC(ai, goal, Callback, nil)', rewritten)
        self.assertIn('local random_int_2_9 = ai:GetRandam_Int(2, 9)', rewritten)


if __name__ == '__main__':unittest.main()
