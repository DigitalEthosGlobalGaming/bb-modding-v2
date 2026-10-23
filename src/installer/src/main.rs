//! `bb-modding-installer.exe`: pick the game and bbport folders, review, install.
//!
//! No arguments opens the wizard. For scripted use:
//!   bb-modding-installer --game <dir> --port <dir> [--runtime <id>] [--quiet]   install without a window
//!   bb-modding-installer --detect --port <dir>                                    say which bbport version is installed
//!   bb-modding-installer --uninstall --port <dir>                                 undo the last install
//! `--runtime` forces one of the supported runtimes when detection cannot decide; use with care.

use bbinstall::auto;
use bbinstall::install::{self, Action, PlanItem, Receipt};
use bbinstall::payload::Payload;
use bbinstall::validate::{self, GameInfo, PortInfo};
use eframe::egui;
use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};
use std::sync::mpsc::{channel, Receiver};

#[derive(Default, Serialize, Deserialize)]
struct Remembered { game: String, port: String }

fn config_path() -> Option<PathBuf> {
    std::env::var_os("APPDATA").map(|a| PathBuf::from(a).join("bb-modding-installer").join("folders.json"))
}
fn load_remembered() -> Remembered {
    config_path().and_then(|p| std::fs::read(p).ok()).and_then(|b| serde_json::from_slice(&b).ok()).unwrap_or_default()
}
fn save_remembered(r: &Remembered) {
    if let Some(p) = config_path() {
        if let Some(d) = p.parent() { let _ = std::fs::create_dir_all(d); }
        if let Ok(j) = serde_json::to_vec(r) { let _ = std::fs::write(p, j); }
    }
}

/// A port next to the installer, if the installer was dropped into one.
fn port_beside_exe() -> Option<String> {
    let dir = std::env::current_exe().ok()?.parent()?.to_path_buf();
    dir.join("Bloodborne.exe").is_file().then(|| dir.display().to_string())
}

#[cfg(windows)]
extern "system" { fn FreeConsole() -> i32; }

fn main() -> eframe::Result<()> {
    let args: Vec<String> = std::env::args().skip(1).collect();
    // A console-subsystem exe, so scripted use prints output and returns an exit code. The wizard
    // detaches from (and so closes) the console a double-click created.
    if !args.is_empty() { std::process::exit(cli(&args)); }
    #[cfg(windows)]
    unsafe { FreeConsole(); }
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default().with_inner_size([680.0, 540.0]).with_min_inner_size([560.0, 460.0]),
        ..Default::default()
    };
    eframe::run_native("Bloodborne PC Mod Loader Setup", options, Box::new(|_| Ok(Box::new(App::new()))))
}

fn cli(args: &[String]) -> i32 {
    let value = |flag: &str| args.iter().position(|a| a == flag).and_then(|i| args.get(i + 1)).cloned();
    let quiet = args.iter().any(|a| a == "--quiet");
    let say = |s: &str| if !quiet { println!("{s}") };
    let Some(port) = value("--port").map(PathBuf::from) else { eprintln!("Missing --port <bbport folder>. See the README for usage."); return 2 };
    let info = match validate::validate_port(&port) { Ok(i) => i, Err(e) => { eprintln!("{e}"); return 1 } };
    if args.iter().any(|a| a == "--uninstall") {
        return match install::uninstall(&port, true) {
            Ok(n) => { say(&format!("Restored {n} files.")); 0 }
            Err(e) => { eprintln!("{e}"); 1 }
        };
    }
    let payload = match Payload::open(install::embedded_payload()) { Ok(p) => p, Err(e) => { eprintln!("{e}"); return 1 } };
    let force = value("--runtime");
    if args.iter().any(|a| a == "--detect") {
        return match auto::choose(&payload, &port, info.probe_dir, force.as_deref()) {
            Ok(rt) => { println!("{} ({})", rt.label, rt.id); 0 }
            Err(e) => { eprintln!("{e}"); 1 }
        };
    }
    let Some(game) = value("--game").map(PathBuf::from) else { eprintln!("Missing --game <game folder>."); return 2 };
    let game = match validate::validate_game(&game) { Ok(g) => g, Err(e) => { eprintln!("{e}"); return 1 } };
    match auto::install_auto(&payload, &port, info.probe_dir, &game.root.display().to_string(), force.as_deref(), true) {
        Ok(o) => {
            say(&format!("Installed {} for {} ({} files). Backup: {}", if o.restored_previous { "again" } else { "the mod loader" }, o.runtime, o.receipt.files.len(), o.receipt.backup));
            0
        }
        Err(e) => { eprintln!("{e}"); 1 }
    }
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Step { Welcome, Folders, Review, Working, Done }

struct Checked<T> { text: String, result: Result<T, String> }

struct App {
    step: Step,
    game: String,
    port: String,
    game_check: Option<Checked<GameInfo>>,
    port_check: Option<Checked<PortInfo>>,
    /// Which bbport version the port has and whether this installer supports it.
    detection: Option<Result<String, String>>,
    payload_error: Option<String>,
    payload_empty: bool,
    plan: Vec<PlanItem>,
    plan_label: String,
    existing: Option<Receipt>,
    rx: Option<Receiver<Result<String, String>>>,
    outcome: Option<Result<String, String>>,
}

impl App {
    fn new() -> App {
        let saved = load_remembered();
        let (payload_error, payload_empty) = match Payload::open(install::embedded_payload()) { Ok(p) => (None, p.is_empty()), Err(e) => (Some(e), true) };
        let mut app = App {
            step: Step::Welcome, game: saved.game, port: port_beside_exe().unwrap_or(saved.port),
            game_check: None, port_check: None, detection: None, payload_error, payload_empty,
            plan: vec![], plan_label: String::new(), existing: None, rx: None, outcome: None,
        };
        app.recheck();
        app
    }

    fn recheck(&mut self) {
        if self.game_check.as_ref().map(|c| &c.text) != Some(&self.game) {
            self.game_check = (!self.game.trim().is_empty()).then(|| Checked { text: self.game.clone(), result: validate::validate_game(Path::new(self.game.trim())) });
        }
        if self.port_check.as_ref().map(|c| &c.text) != Some(&self.port) {
            self.refresh_port();
        }
    }

    fn refresh_port(&mut self) {
        let dir = self.port.trim().to_owned();
        self.port_check = (!dir.is_empty()).then(|| Checked { text: self.port.clone(), result: validate::validate_port(Path::new(&dir)) });
        self.existing = install::read_receipt(Path::new(&dir));
        self.detection = match (&self.port_check, Payload::open(install::embedded_payload())) {
            (Some(Checked { result: Ok(info), .. }), Ok(p)) if !p.is_empty() => Some(auto::choose(&p, Path::new(&dir), info.probe_dir, None).map(|rt| {
                if self.existing.is_some() { format!("{} found. The mod loader is already installed and will be reinstalled cleanly.", rt.label) } else { format!("{} found.", rt.label) }
            })),
            _ => None,
        };
    }

    fn folders_ok(&self) -> bool {
        matches!(&self.game_check, Some(Checked { result: Ok(_), .. })) && matches!(&self.port_check, Some(Checked { result: Ok(_), .. }))
            && matches!(&self.detection, Some(Ok(_))) && !self.payload_empty
    }

    fn start(&mut self, uninstall: bool) {
        let port = PathBuf::from(self.port.trim());
        let probe_dir = match &self.port_check { Some(Checked { result: Ok(i), .. }) => i.probe_dir, _ => "bin" };
        let game = match &self.game_check { Some(Checked { result: Ok(g), .. }) => g.root.display().to_string(), _ => String::new() };
        let (tx, rx) = channel();
        self.rx = Some(rx);
        self.step = Step::Working;
        save_remembered(&Remembered { game: self.game.clone(), port: self.port.clone() });
        std::thread::spawn(move || {
            let result = if uninstall {
                install::uninstall(&port, true).map(|n| format!("Restored {n} files from the backup. Your saves and settings were not touched."))
            } else {
                Payload::open(install::embedded_payload()).and_then(|p| auto::install_auto(&p, &port, probe_dir, &game, None, true)).map(|o| {
                    format!("Installed the mod loader for {} ({} files).\nA backup of replaced files is in {}\\{}.", o.runtime, o.receipt.files.len(), port.display(), o.receipt.backup.replace('/', "\\"))
                })
            };
            let _ = tx.send(result);
        });
    }

    fn make_plan(&mut self) -> Result<(), String> {
        let probe_dir = match &self.port_check { Some(Checked { result: Ok(i), .. }) => i.probe_dir, _ => "bin" };
        let p = Payload::open(install::embedded_payload())?;
        let (label, _restoring, items) = auto::plan(&p, Path::new(self.port.trim()), probe_dir, None)?;
        self.plan = items;
        self.plan_label = label;
        Ok(())
    }
}

fn folder_row(ui: &mut egui::Ui, label: &str, text: &mut String, status: Option<&Result<String, String>>) {
    ui.label(egui::RichText::new(label).strong());
    ui.horizontal(|ui| {
        ui.add(egui::TextEdit::singleline(text).desired_width(ui.available_width() - 90.0));
        if ui.button("Browse...").clicked() {
            let mut dialog = rfd::FileDialog::new().set_title(label);
            if Path::new(text.as_str()).is_dir() { dialog = dialog.set_directory(text.as_str()); }
            if let Some(p) = dialog.pick_folder() { *text = p.display().to_string(); }
        }
    });
    match status {
        Some(Ok(msg)) => { ui.colored_label(egui::Color32::from_rgb(90, 180, 110), format!("OK: {msg}")); }
        Some(Err(e)) => { ui.colored_label(egui::Color32::from_rgb(220, 90, 80), e); }
        None => { ui.weak("Not chosen yet."); }
    }
    ui.add_space(10.0);
}

impl eframe::App for App {
    fn update(&mut self, ctx: &egui::Context, _: &mut eframe::Frame) {
        if let Some(rx) = &self.rx {
            if let Ok(r) = rx.try_recv() { self.outcome = Some(r); self.rx = None; self.step = Step::Done; self.refresh_port(); }
            ctx.request_repaint_after(std::time::Duration::from_millis(100));
        }
        self.recheck();
        egui::CentralPanel::default().show(ctx, |ui| {
            ui.add_space(6.0);
            ui.heading("Bloodborne PC Mod Loader Setup");
            ui.separator();
            ui.add_space(8.0);
            match self.step {
                Step::Welcome => self.welcome(ui),
                Step::Folders => self.folders(ui),
                Step::Review => self.review(ui),
                Step::Working => { ui.label("Installing... please keep this window open."); ui.add(egui::ProgressBar::new(0.0).animate(true)); }
                Step::Done => self.done(ui),
            }
        });
    }
}

impl App {
    fn welcome(&mut self, ui: &mut egui::Ui) {
        ui.label("This sets up mods for Bloodborne on PC (bbport).");
        ui.add_space(6.0);
        ui.label("It installs:");
        ui.label("  - the mod loader that lets the game run mods, matched to your bbport version");
        ui.label("  - the Mod Manager, where you browse, install and update mods inside the game");
        ui.add_space(6.0);
        ui.label("Your game files, saves and settings are not changed. Files the installer replaces are backed up so you can undo the install.");
        if let Some(e) = &self.payload_error { ui.colored_label(egui::Color32::from_rgb(220, 90, 80), e); }
        else if self.payload_empty {
            ui.colored_label(egui::Color32::from_rgb(220, 160, 60), "This is a developer build without an installer payload, so it cannot install anything.");
        }
        if let Some(r) = &self.existing { ui.add_space(6.0); ui.weak(format!("Already installed (installer {}).", r.installer_version)); }
        ui.add_space(14.0);
        if ui.button("Next").clicked() { self.step = Step::Folders; }
    }

    fn folders(&mut self, ui: &mut egui::Ui) {
        let game_status = self.game_check.as_ref().map(|c| c.result.as_ref().map(|g| g.root.display().to_string()).map_err(String::clone));
        folder_row(ui, "Game folder (contains CUSA03173)", &mut self.game, game_status.as_ref());
        let port_status = match (&self.port_check, &self.detection) {
            (None, _) => None,
            (Some(Checked { result: Err(e), .. }), _) => Some(Err(e.clone())),
            (Some(_), Some(Ok(msg))) => Some(Ok(msg.clone())),
            (Some(_), Some(Err(e))) => Some(Err(e.clone())),
            (Some(_), None) => Some(Ok("bbport found".to_string())),
        };
        folder_row(ui, "bbport folder (contains Bloodborne.exe)", &mut self.port, port_status.as_ref());
        ui.horizontal(|ui| {
            if ui.button("Back").clicked() { self.step = Step::Welcome; }
            if ui.add_enabled(self.folders_ok(), egui::Button::new("Next")).clicked() {
                match self.make_plan() {
                    Ok(()) => self.step = Step::Review,
                    Err(e) => { self.outcome = Some(Err(e)); self.step = Step::Done; }
                }
            }
        });
    }

    fn review(&mut self, ui: &mut egui::Ui) {
        let count = |a: Action| self.plan.iter().filter(|i| i.action == a).count();
        ui.label(format!("{}: {} files to add, {} to replace (backed up first), {} already up to date.", self.plan_label, count(Action::Add), count(Action::Replace), count(Action::Unchanged)));
        ui.add_space(6.0);
        egui::ScrollArea::vertical().max_height(ui.available_height() - 60.0).show(ui, |ui| {
            for i in &self.plan {
                let tag = match i.action { Action::Add => "add", Action::Replace => "replace", Action::Unchanged => "same" };
                ui.monospace(format!("{tag:<8}{}", i.path));
            }
        });
        ui.add_space(8.0);
        ui.horizontal(|ui| {
            if ui.button("Back").clicked() { self.step = Step::Folders; }
            if ui.button("Install").clicked() { self.start(false); }
            if self.existing.is_some() && ui.button("Uninstall instead").clicked() { self.start(true); }
        });
    }

    fn done(&mut self, ui: &mut egui::Ui) {
        match &self.outcome {
            Some(Ok(msg)) => {
                ui.colored_label(egui::Color32::from_rgb(90, 180, 110), "Done.");
                ui.label(msg);
                ui.add_space(6.0);
                ui.label("Start the game as usual. Open the Mod Manager with F2, or from the pause menu's Mods entry.");
            }
            Some(Err(e)) => { ui.colored_label(egui::Color32::from_rgb(220, 90, 80), "The install did not complete."); ui.label(e); }
            None => {}
        }
        ui.add_space(12.0);
        ui.horizontal(|ui| {
            if ui.button("Back to start").clicked() { self.step = Step::Welcome; self.refresh_port(); }
            if ui.button("Close").clicked() { ui.ctx().send_viewport_cmd(egui::ViewportCommand::Close); }
        });
    }
}
