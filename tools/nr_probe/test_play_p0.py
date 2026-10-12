#!/usr/bin/env python3
"""Offline entry checks only: no Root runner, profile creation or GPU launch."""
import contextlib
import importlib.util
import io
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("entry", Path(__file__).with_name("play_p0.py"))
entry = importlib.util.module_from_spec(spec); spec.loader.exec_module(entry)

class EntryTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.state = self.root / "integration/playtest"; self.state.mkdir(parents=True)
        self.approval = self.state / "approved.json"
        self.value = {"status": "APPROVED_P0_CASE", "batch": {"path": "Root-only-frozen-batch"},
                      "case": {"case_id": "newcase", "mode": "resident", "frames": 120,
                               "execution_mode": "hot", "initial_enable": False,
                               "outer_seconds": 600, "helper_start_allowance": 1}}
        self.approval.write_text(json.dumps(self.value))
        self.inv = self.root / "runs/new-invocation"; self.inv.mkdir(parents=True)
    def test_plan_no_budget_or_mode_override(self):
        plan = entry.launch_plan(self.root, "New-Case", self.approval)
        self.assertEqual(plan["launch_command"][1], str(self.root / "integration/run_playtest.py"))
        self.assertEqual(plan["check_command"][-1], "--preflight-only")
        self.assertEqual(plan["frames"], 120)
        self.assertEqual(plan["status"], "COMMAND_PREVIEW_NOT_PREFLIGHT_OR_RUNTIME_PASS")
    def test_no_longrun_or_budget_override(self):
        for key,value in [("frames",1800),("frames",True),("outer_seconds",900),("helper_start_allowance",3),("initial_enable",1)]:
            value_json=json.loads(json.dumps(self.value));value_json["case"][key]=value
            self.approval.write_text(json.dumps(value_json))
            with self.assertRaises(ValueError): entry.launch_plan(self.root,"new",self.approval)
    def user_approval(self, status="PASS_BOUNDED_EXPERIMENTAL_ENTRY_GATE"):
        value=json.loads(json.dumps(self.value));value["case"].update(
            case_id="p2-quality-user1800",frames=1800,outer_seconds=300,helper_start_allowance=2)
        gate=self.state/"experience-gate.json";gate.write_text(json.dumps({"status":status}))
        value["experience_entry_gate"]={"path":str(gate),"bytes":gate.stat().st_size,
            "sha256":hashlib.sha256(gate.read_bytes()).hexdigest()}
        return value
    def test_exact_user_case_gate_delegation(self):
        value=self.user_approval();self.approval.write_text(json.dumps(value))
        plan=entry.launch_plan(self.root,"new",self.approval)
        self.assertEqual(plan["frames"],1800);self.assertEqual(plan["effective_NR_interaction_window_max_seconds"],60)
        self.assertEqual(plan["launch_command"][1],str(self.root/"integration/run_playtest.py"))
    def test_user_gate_missing_or_failed(self):
        value=self.user_approval();value.pop("experience_entry_gate");self.approval.write_text(json.dumps(value))
        with self.assertRaises(ValueError):entry.launch_plan(self.root,"new",self.approval)
        value=self.user_approval("DRAFT");self.approval.write_text(json.dumps(value))
        with self.assertRaises(ValueError):entry.launch_plan(self.root,"new",self.approval)
    def test_user_gate_identity_and_type_mismatch(self):
        for key,wrong in [("sha256","0"*64),("bytes",1),("bytes",True),("path","relative.json")]:
            value=self.user_approval();value["experience_entry_gate"][key]=wrong
            self.approval.write_text(json.dumps(value))
            with self.assertRaises(ValueError):entry.launch_plan(self.root,"new",self.approval)
    def test_user_case_no_mode_duration_or_general_longrun(self):
        for key,wrong in [("case_id","other-user1800"),("mode","srOnly"),("execution_mode","diagnostic"),("outer_seconds",301),("helper_start_allowance",0),("frames",120)]:
            value=self.user_approval();value["case"][key]=wrong;self.approval.write_text(json.dumps(value))
            with self.assertRaises(ValueError):entry.launch_plan(self.root,"new",self.approval)
    def test_user_gate_external_symlink_or_changed_payload(self):
        value=self.user_approval();gate=Path(value["experience_entry_gate"]["path"])
        gate.write_text('{"status":"DRAFT"}');self.approval.write_text(json.dumps(value))
        with self.assertRaises(ValueError):entry.launch_plan(self.root,"new",self.approval)
        value=self.user_approval();link=self.state/"gate-link.json";link.symlink_to(gate)
        value["experience_entry_gate"]["path"]=str(link);self.approval.write_text(json.dumps(value))
        with self.assertRaises(ValueError):entry.launch_plan(self.root,"new",self.approval)
        value=self.user_approval();outside=self.root/"outside-gate.json";outside.write_bytes(gate.read_bytes())
        value["experience_entry_gate"]["path"]=str(outside);self.approval.write_text(json.dumps(value))
        with self.assertRaises(ValueError):entry.launch_plan(self.root,"new",self.approval)
    def test_input_hold_fail_closed(self):
        for hold in (float("nan"),float("inf"),-1,31):
            with self.assertRaises(ValueError): entry.input_command(self.root,self.inv,"on",hold=hold)
    def test_reject_unsafe_session(self):
        for s in ("../old", "bad_token", "x"*49, ""):
            with self.assertRaises(ValueError): entry.launch_plan(self.root, s, self.approval)
    def test_no_old_batch_reset(self):
        self.value.pop("batch"); self.approval.write_text(json.dumps(self.value))
        with self.assertRaises(ValueError): entry.launch_plan(self.root, "new", self.approval)
    def test_unapproved_rejected(self):
        self.value["status"] = "DRAFT"; self.approval.write_text(json.dumps(self.value))
        with self.assertRaises(ValueError): entry.launch_plan(self.root, "new", self.approval)
    def test_external_and_symlink_rejected(self):
        external = self.root / "outside.json"; external.write_text("{}")
        with self.assertRaises(ValueError): entry.launch_plan(self.root, "new", external)
        linked = self.state / "link.json"; linked.symlink_to(self.approval)
        with self.assertRaises(ValueError): entry.launch_plan(self.root, "new", linked)
    def test_duplicate_json_rejected(self):
        self.approval.write_text('{"status":1,"status":2}')
        with self.assertRaises(ValueError): entry.launch_plan(self.root, "new", self.approval)
    def test_input_only_original_live_identity_tool(self):
        cmd = entry.input_command(self.root, self.inv, "off", True, ["cross"], .15)
        self.assertEqual(cmd[1], str(self.root / "integration/input_playtest.py"))
        self.assertIn("--enable", cmd); self.assertNotIn("stop", cmd)
    def test_launch_default_cannot_execute(self):
        with patch.object(entry, "ROOT", self.root), patch.object(entry.subprocess, "run") as run, contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(entry.main(["launch", "new", "--approval", str(self.approval)]), 0)
            run.assert_not_called()
    def test_explicit_delegation_mock_not_GPU(self):
        with patch.object(entry, "ROOT", self.root), patch.object(entry.subprocess, "run") as run, contextlib.redirect_stdout(io.StringIO()):
            run.return_value.returncode = 3
            self.assertEqual(entry.main(["check", "new", "--approval", str(self.approval), "--execute-approved"]), 3)
            self.assertEqual(run.call_args.args[0][-1], "--preflight-only")
    def test_status_scope_and_types(self):
        case = self.root / "bbport/runs/p0-game-new"; (case / "profile").mkdir(parents=True)
        (case / "profile/.p0-profile").write_text("new")
        (self.inv / "manifest.json").write_text(json.dumps({"session":"new","protocol_version":6,"run":str(case)}))
        self.assertEqual(entry.read_status(self.root, self.inv)["state"], "NOT_OBSERVED")
        status = {"schema":"p0-frontend-status-v1","session":"new","state":"Starting",
                  "requested_enable":True,"applied_enable":False,"external_revision":0,"helper_starts":1,
                  "finalized_count":0,"accepted_for_merge_count":0,"fallback_count":0}
        (case / "frontend-status.json").write_text(json.dumps(status))
        self.assertEqual(entry.read_status(self.root, self.inv)["status"]["state"], "Starting")
        status["helper_starts"] = True; (case / "frontend-status.json").write_text(json.dumps(status))
        with self.assertRaises(ValueError): entry.read_status(self.root, self.inv)

if __name__ == "__main__":
    unittest.main()
