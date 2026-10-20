#!/usr/bin/env python3
"""Minimal GTK4/Adwaita front end for the private NRBridge preview.

It only selects and validates local inputs, then hands the actual session to
``nrbridge_entry.py --daily`` in a terminal.  It does not implement a second
lifecycle: the canonical daily runner still owns launch, close grace, save
continuation and retirement.
"""
from __future__ import annotations

import argparse
import contextlib
import io
import os
import subprocess
import sys
import threading
from pathlib import Path

import gi

gi.require_version("Gtk", "4.0")
gi.require_version("Adw", "1")
from gi.repository import Adw, GLib, Gtk  # noqa: E402

import nrbridge_entry as entry  # noqa: E402


def _button(icon: str, tooltip: str, callback) -> Gtk.Button:
    button = Gtk.Button(icon_name=icon, tooltip_text=tooltip, valign=Gtk.Align.CENTER)
    button.add_css_class("flat")
    button.connect("clicked", callback)
    return button


class Window(Adw.ApplicationWindow):
    def __init__(self, app: Adw.Application):
        super().__init__(application=app, title="bbport NRBridge Preview")
        self.set_default_size(760, 720)
        self.home = Path(os.environ.get("NRBRIDGE_HOME", entry.DEFAULT_HOME)).expanduser()
        self.info = entry.bundle_info()
        self.values = {
            "game_dir": entry.default_game_dir(),
            "ge_files": entry.default_ge(),
            "model": entry.default_model(),
            "ngx_dir": entry.default_ngx(),
            "system_ngx": entry.default_system_ngx(),
            "sysroot_lib": entry.default_sysroot(),
            "online_account": str(entry.launcher_online_settings()),
        }
        self.rows: dict[str, Adw.ActionRow] = {}
        self.working = False
        self.children: dict[int, str] = {}
        self.connect("close-request", self.on_close_request)
        self.build_ui()
        self.refresh_online()
        self.refresh_identity()
        self.refresh_state()

    # --- UI -----------------------------------------------------------------

    def build_ui(self) -> None:
        toolbar = Adw.ToolbarView()
        header = Adw.HeaderBar()
        self.check_button = Gtk.Button(label="Check")
        self.check_button.connect("clicked", lambda _b: self.run_check())
        self.launch_button = Gtk.Button(label="Launch daily")
        self.launch_button.add_css_class("suggested-action")
        self.launch_button.connect("clicked", lambda _b: self.run_launch())
        header.pack_end(self.launch_button)
        header.pack_end(self.check_button)
        toolbar.add_top_bar(header)

        page = Adw.PreferencesPage()
        identity = Adw.PreferencesGroup(title="Packaged identity")
        self.identity_rows = {}
        for key, label in (
            ("native", "Native"),
            ("helper", "Helper"),
            ("protocol", "Protocol"),
            ("forwarder", "Forwarder"),
        ):
            row = Adw.ActionRow(title=label)
            identity.add(row)
            self.identity_rows[key] = row
        page.add(identity)

        inputs = Adw.PreferencesGroup(
            title="Local inputs (not packaged)",
            description="The game, Wine/GE and the NR model stay on this machine. "
            "The model is verified by SHA and PE header, never copied.",
        )
        for key, label, kind in (
            ("game_dir", "Game folder (CUSA03173)", "folder"),
            ("ge_files", "Wine/GE files directory", "folder"),
            ("model", "NR model (nvngx_dlssnr.dll)", "file"),
            ("ngx_dir", "NGX Wine DLL directory", "folder"),
            ("system_ngx", "System libnvidia-ngx.so", "file"),
        ):
            row = Adw.ActionRow(title=label)
            row.add_suffix(_button("document-open-symbolic", "Choose", 
                                   lambda _b, k=key, t=label, d=kind: self.choose(k, t, d)))
            inputs.add(row)
            self.rows[key] = row
        page.add(inputs)

        online = Adw.PreferencesGroup(
            title="Online (daily only)",
            description="Offline by default. Sign in with Discord in the existing bbport "
            "launcher first; this preview only passes the settings path to the daily runner.",
        )
        self.online_pool_row = Adw.ComboRow(title="Pool")
        self.online_pool_row.set_model(Gtk.StringList.new(["offline", "shadnet", "thd"]))
        self.online_pool_row.set_selected(0)
        self.online_pool_row.connect("notify::selected", lambda *_: self.refresh_online())
        online.add(self.online_pool_row)
        self.bbsc_row = Adw.SwitchRow(title="BBSC hooks (shadnet only)", active=False)
        online.add(self.bbsc_row)
        login_row = Adw.ActionRow(
            title="Discord login / Online settings",
            subtitle="Opens the existing bbport Online page; this preview never logs in or prints a token",
        )
        login_row.add_suffix(_button("system-users-symbolic", "Open Online page",
                                     lambda _b: self.open_online_login()))
        online.add(login_row)
        account_row = Adw.ActionRow(title="Account settings (canonical JSON)")
        account_row.add_suffix(_button("document-open-symbolic", "Choose",
                                       lambda _b: self.choose("online_account", "Launcher settings", "file")))
        online.add(account_row)
        self.rows["online_account"] = account_row
        page.add(online)

        session = Adw.PreferencesGroup(title="Daily session")
        self.state_row = Adw.ActionRow(title="Last session")
        self.state_row.add_suffix(_button("folder-open-symbolic", "Open data folder",
                                          lambda _b: self.open_folder(self.home)))
        session.add(self.state_row)
        self.warning_row = Adw.ActionRow(title="Portability")
        session.add(self.warning_row)
        self.joystick_row = Adw.SwitchRow(
            title="Joystick mouse fallback",
            subtitle="Compatibility path; default is the validated native mouse",
            active=False,
        )
        session.add(self.joystick_row)
        page.add(session)

        log_group = Adw.PreferencesGroup(title="Log")
        self.log_view = Gtk.TextView(editable=False, monospace=True, wrap_mode=Gtk.WrapMode.WORD_CHAR)
        scroll = Gtk.ScrolledWindow(hexpand=True, vexpand=True)
        scroll.set_min_content_height(180)
        scroll.set_child(self.log_view)
        log_group.add(scroll)
        page.add(log_group)

        toolbar.set_content(page)
        self.set_content(toolbar)
        self.update_rows()

    def refresh_identity(self) -> None:
        info = self.info
        self.identity_rows["native"].set_subtitle(info["native"]["source"])
        self.identity_rows["helper"].set_subtitle(
            f"{info['helper']['build_id']}  {info['helper']['artifact_sha256'][:16]}…"
        )
        self.identity_rows["protocol"].set_subtitle(
            f"wire {info['helper']['protocol'].get('wire_version')} / "
            f"revision {info['helper']['protocol'].get('interface_revision')}"
        )
        self.identity_rows["forwarder"].set_subtitle(
            f"{info['forwarder']['build_id']}  {info['forwarder']['artifact_sha256'][:16]}…"
        )
        ready = bool(info.get("daily_ready", False))
        if ready:
            text = "daily ring64 v2 ready (Native marker + Helper revision 4)"
        elif not info["native"].get("daily_portable", False):
            text = "Native has no NRBRIDGE_NATIVE_DAILY_RING64_V2 marker; --check works, daily is disabled"
        else:
            text = ("Helper interface revision "
                    f"{info['helper']['protocol'].get('interface_revision')}; daily requires 4")
        self.warning_row.set_subtitle(text)
        self.launch_button.set_sensitive(ready and not self.working)

    def refresh_state(self) -> None:
        state = entry.read_state(self.home)
        last = state.get("last_session")
        status = state.get("last_status")
        if last:
            self.state_row.set_subtitle(f"{last}: {status}")
        else:
            self.state_row.set_subtitle("no previous session (first run uses an empty seed)")
        config = self.home / "localinputs/runtime.json"
        if config.is_file():
            self.check_button.set_label("Check")
        else:
            self.check_button.set_label("Save && Check")

    def update_rows(self) -> None:
        for key, row in self.rows.items():
            value = self.values.get(key) or "not selected"
            row.set_subtitle(value)

    def refresh_online(self) -> None:
        index = self.online_pool_row.get_selected()
        self.online_pool = ("offline", "shadnet", "thd")[index] if index < 3 else "offline"
        self.bbsc_row.set_sensitive(self.online_pool == "shadnet")
        if self.online_pool != "shadnet":
            self.bbsc_row.set_active(False)

    def append(self, text: str) -> None:
        buffer = self.log_view.get_buffer()
        buffer.insert(buffer.get_end_iter(), text)
        self.log_view.scroll_to_iter(buffer.get_end_iter(), 0.0, False, 0.0, 0.0)

    def _spawn(self, argv: list[str], env: dict[str, str], label: str) -> bool:
        try:
            process = subprocess.Popen(argv, env=env, cwd=str(entry.APP))
        except OSError as error:
            self.append(f"cannot start {label}: {error}\n")
            return False
        self.children[process.pid] = label
        GLib.child_watch_add(GLib.PRIORITY_DEFAULT, process.pid, self._child_done)
        return True

    def _child_done(self, pid: int, status: int) -> bool:
        label = self.children.pop(pid, "child")
        self.append(f"{label} ended (status {status})\n")
        return False

    def on_close_request(self, _window) -> bool:
        # The AppImage mount (and therefore the bundled Python/scripts) lasts
        # only while this process lives.  Closing the window while a terminal
        # daily session or the upstream Online page still runs would pull the
        # mount out from under it.
        if not self.children:
            return False
        labels = ", ".join(sorted(set(self.children.values())))
        self.append(f"refusing to close while running: {labels}\n")
        dialog = Gtk.AlertDialog(
            message="A child session is still running",
            detail="Closing this window would unmount the AppImage and break "
                   f"{labels}. Close the terminal/Online window first.",
        )
        dialog.set_buttons(["OK"])
        dialog.show(self)
        return True

    def open_online_login(self) -> None:
        launcher = Path(entry.APP) / "launcher/bbport_launcher.py"
        if not launcher.is_file():
            self.append(f"upstream launcher not found: {launcher}\n")
            return
        env = dict(os.environ)
        env["BB_LAUNCHER_PAGE"] = "online"
        if self._spawn([sys.executable, str(launcher)], env, "upstream Online page"):
            self.append("opened the existing bbport Online page\n")

    def open_folder(self, path: Path) -> None:
        try:
            subprocess.Popen(["xdg-open", str(path)])
        except OSError as error:
            self.append(f"cannot open {path}: {error}\n")

    # --- selection ----------------------------------------------------------

    def choose(self, key: str, title: str, kind: str) -> None:
        dialog = Gtk.FileDialog(title=title)
        if kind == "folder":
            dialog.select_folder(self, None, lambda d, r, k=key, t=kind: self._chosen(d, r, k, t))
        else:
            dialog.open(self, None, lambda d, r, k=key, t=kind: self._chosen(d, r, k, t))

    def _chosen(self, dialog, result, key: str, kind: str) -> None:
        try:
            file = (dialog.select_folder_finish(result) if kind == "folder"
                    else dialog.open_finish(result))
        except GLib.Error as error:
            self.append(f"selection cancelled: {error.message}\n")
            return
        if file is None:
            return
        path = Path(file.get_path())
        self.values[key] = str(path)
        self.update_rows()

    # --- actions ------------------------------------------------------------

    def namespace(self) -> argparse.Namespace:
        return argparse.Namespace(
            home=self.home,
            force=False,
            game_dir=self.values["game_dir"],
            ge_files=self.values["ge_files"],
            model=self.values["model"],
            ngx_dir=self.values["ngx_dir"],
            system_ngx=self.values["system_ngx"],
            sysroot_lib=self.values["sysroot_lib"],
        )

    def run_check(self) -> None:
        if self.working:
            return
        self.working = True
        self.check_button.set_sensitive(False)
        self.launch_button.set_sensitive(False)
        self.append("\n— checking local inputs —\n")
        threading.Thread(target=self._check_worker, daemon=True).start()

    def _check_worker(self) -> None:
        output = io.StringIO()
        code = 0
        try:
            with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
                args = self.namespace()
                config = self.home / "localinputs/runtime.json"
                if not config.is_file():
                    entry.setup(args)
                entry.check(args)
        except SystemExit as error:
            code = int(error.code or 0)
        except Exception as error:  # keep the window usable and show the reason
            code = 1
            output.write(f"{type(error).__name__}: {error}\n")
        GLib.idle_add(self.finish, output.getvalue(), code)

    def finish(self, text: str, code: int) -> bool:
        self.append(text)
        if code:
            self.append(f"failed (exit {code})\n")
        self.working = False
        self.check_button.set_sensitive(True)
        self.refresh_identity()
        self.refresh_state()
        self.update_rows()
        return False

    def run_launch(self) -> None:
        if self.working:
            return
        if not (self.home / "localinputs/runtime.json").is_file():
            self.append("save the local inputs first (Check)\n")
            return
        command = [
            sys.executable,
            str(Path(entry.__file__).resolve()),
            "--daily",
            "--home",
            str(self.home),
            "--online-pool",
            self.online_pool,
        ]
        if self.joystick_row.get_active():
            command += ["--joystick-mouse"]
        if self.online_pool != "offline":
            command += ["--online-account-config", self.values["online_account"]]
            if self.bbsc_row.get_active():
                command += ["--bbsc"]
        argv = entry.terminal_command(command)
        if argv is None:
            self.append("no terminal emulator found; run the AppImage with --daily in a terminal\n")
            return
        env = dict(os.environ)
        env["NRBRIDGE_HOME"] = str(self.home)
        if self._spawn(argv, env, "daily session"):
            self.append("\n— daily session started in a terminal —\n")


class App(Adw.Application):
    def __init__(self):
        super().__init__(application_id="io.github.bbport.NRBridgePreview",
                         flags=0)

    def do_activate(self):
        window = self.get_active_window() or Window(self)
        window.present()


def main(argv=None) -> int:
    return App().run([sys.argv[0]] if argv is None else argv)


if __name__ == "__main__":
    raise SystemExit(main())
