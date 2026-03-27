use std::{
    fs,
    time::{SystemTime, UNIX_EPOCH},
};

use eframe::egui::{self, Color32, RichText, ScrollArea, TextEdit};
use serde::Serialize;

use crate::models::{
    AssertionResult, CredentialRecord, InfoSnapshot, InitSnapshot, LogEntry, MakeCredentialResult,
    RegisterForm, SessionSnapshot,
};
use crate::transport::{
    DebugHidBackend, ManagerBackend, ManagerChannelBackend, PreviewBackend, default_assertion_form, default_log_body,
    default_register_form,
};

const FONT_CANDIDATES: &[&str] = &[
    r"C:\Windows\Fonts\msyh.ttc",
    r"C:\Windows\Fonts\msyhbd.ttc",
    r"C:\Windows\Fonts\simhei.ttf",
    r"C:\Windows\Fonts\simsun.ttc",
];

pub struct MeowKeyManagerApp {
    backend: Box<dyn ManagerBackend>,
    session: Option<SessionSnapshot>,
    register_form: RegisterForm,
    assertion_form: crate::models::AssertionForm,
    init_snapshot: Option<InitSnapshot>,
    info_snapshot: Option<InfoSnapshot>,
    last_make_credential: Option<MakeCredentialResult>,
    last_assertion: Option<AssertionResult>,
    diagnostic_output: String,
    manager_catalog_output: String,
    manager_security_output: String,
    init_output: String,
    info_output: String,
    make_credential_output: String,
    get_assertion_output: String,
    credentials: Vec<CredentialRecord>,
    selected_credential: Option<usize>,
    logs: Vec<LogEntry>,
}

impl MeowKeyManagerApp {
    pub fn new(cc: &eframe::CreationContext<'_>) -> Self {
        install_cjk_fonts(&cc.egui_ctx);

        let mut app = Self {
            backend: Box::<PreviewBackend>::default(),
            session: None,
            register_form: default_register_form(),
            assertion_form: default_assertion_form(),
            init_snapshot: None,
            info_snapshot: None,
            last_make_credential: None,
            last_assertion: None,
            diagnostic_output: "还没有固件诊断日志。".to_string(),
            manager_catalog_output: "还没有正式凭据目录。".to_string(),
            manager_security_output: "还没有正式安全状态。".to_string(),
            init_output: "还没有初始化响应。".to_string(),
            info_output: "还没有认证器信息。".to_string(),
            make_credential_output: "还没有注册结果。".to_string(),
            get_assertion_output: "还没有断言结果。".to_string(),
            credentials: Vec::new(),
            selected_credential: None,
            logs: Vec::new(),
        };
        app.push_log("就绪", &default_log_body());
        app
    }

    fn push_log(&mut self, tag: &str, body: &str) {
        let seconds = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map(|duration| duration.as_secs())
            .unwrap_or(0);
        self.logs.insert(
            0,
            LogEntry {
                timestamp: format!("{seconds}"),
                tag: tag.to_string(),
                body: body.to_string(),
            },
        );
    }

    fn collect_backend_logs<T>(
        &mut self,
        operation: impl FnOnce(&mut dyn ManagerBackend, &mut dyn FnMut(&str, &str)) -> anyhow::Result<T>,
    ) -> anyhow::Result<T> {
        let mut emitted = Vec::<(String, String)>::new();
        let result = {
            let backend = self.backend.as_mut();
            let mut logger =
                |tag: &str, body: &str| emitted.push((tag.to_string(), body.to_string()));
            operation(backend, &mut logger)
        };
        for (tag, body) in emitted {
            self.push_log(&tag, &body);
        }
        result
    }

    fn reset_runtime_state(&mut self) {
        self.session = None;
        self.init_snapshot = None;
        self.info_snapshot = None;
        self.last_make_credential = None;
        self.last_assertion = None;
        self.diagnostic_output = "还没有固件诊断日志。".to_string();
        self.manager_catalog_output = "还没有正式凭据目录。".to_string();
        self.manager_security_output = "还没有正式安全状态。".to_string();
        self.init_output = "还没有初始化响应。".to_string();
        self.info_output = "还没有认证器信息。".to_string();
        self.make_credential_output = "还没有注册结果。".to_string();
        self.get_assertion_output = "还没有断言结果。".to_string();
        self.credentials.clear();
        self.selected_credential = None;
    }

    fn switch_backend(&mut self, backend: Box<dyn ManagerBackend>) {
        if let Err(error) = self.backend.disconnect() {
            self.push_log("错误", &format!("切换后端前断开旧连接失败: {error}"));
        }
        self.backend = backend;
        self.reset_runtime_state();
        self.push_log("后端", &format!("已切换到 {}", self.backend.backend_name()));
    }

    fn connect_preview(&mut self) {
        self.switch_backend(Box::<PreviewBackend>::default());
        match self.collect_backend_logs(|backend, log| backend.connect(log)) {
            Ok(session) => self.session = Some(session),
            Err(error) => self.push_log("错误", &error.to_string()),
        }
    }

    fn connect_debug_hid(&mut self) {
        self.switch_backend(Box::<DebugHidBackend>::default());
        match self.collect_backend_logs(|backend, log| backend.connect(log)) {
            Ok(session) => self.session = Some(session),
            Err(error) => self.push_log("错误", &error.to_string()),
        }
    }

    fn connect_manager_channel(&mut self) {
        self.switch_backend(Box::<ManagerChannelBackend>::default());
        match self.collect_backend_logs(|backend, log| backend.connect(log)) {
            Ok(session) => self.session = Some(session),
            Err(error) => self.push_log("错误", &error.to_string()),
        }
    }

    fn disconnect(&mut self) {
        if let Err(error) = self.collect_backend_logs(|backend, _log| backend.disconnect()) {
            self.push_log("错误", &error.to_string());
        }
        self.reset_runtime_state();
        self.push_log("会话", "当前连接已断开。");
    }

    fn init_channel(&mut self) {
        let Some(mut session) = self.session.clone() else {
            self.push_log("错误", "请先连接真实设备或预览后端。");
            return;
        };

        match self.collect_backend_logs(|backend, log| backend.init_channel(&mut session, log)) {
            Ok(snapshot) => {
                self.init_output = to_pretty_json(&snapshot);
                self.init_snapshot = Some(snapshot);
                self.session = Some(session);
            }
            Err(error) => self.push_log("错误", &error.to_string()),
        }
    }

    fn refresh_info(&mut self) {
        let Some(session) = self.session.clone() else {
            self.push_log("错误", "请先建立设备会话。");
            return;
        };

        match self.collect_backend_logs(|backend, log| backend.get_info(&session, log)) {
            Ok(snapshot) => {
                self.info_output = to_pretty_json(&snapshot);
                self.info_snapshot = Some(snapshot);
            }
            Err(error) => self.push_log("错误", &error.to_string()),
        }
    }

    fn register_credential(&mut self) {
        let Some(session) = self.session.clone() else {
            self.push_log("错误", "请先建立设备会话。");
            return;
        };
        let form = self.register_form.clone();
        let existing = self.credentials.clone();

        match self.collect_backend_logs(|backend, log| {
            backend.make_credential(&session, &form, &existing, log)
        }) {
            Ok(result) => {
                self.make_credential_output = to_pretty_json(&result);
                self.last_make_credential = Some(result.clone());

                if result.status == 0 {
                    if let Some(credential_id_hex) = result.credential_id_hex.clone() {
                        self.assertion_form.credential_id = credential_id_hex.clone();
                        self.register_form.exclude_list = credential_id_hex.clone();
                        self.credentials.insert(
                            0,
                            CredentialRecord {
                                credential_id_hex,
                                rp_id: result.request.rp_id.clone(),
                                user_id_hex: result.request.user_id.clone(),
                                user_name: result.request.user_name.clone(),
                                display_name: result.request.display_name.clone(),
                                sign_count: 0,
                                discoverable: true,
                            },
                        );
                        self.selected_credential = Some(0);
                    }
                    self.push_log("CTAP", "authenticatorMakeCredential 成功。");
                } else {
                    self.push_log(
                        "CTAP",
                        &format!("authenticatorMakeCredential => {}", result.status_name),
                    );
                }
            }
            Err(error) => self.push_log("错误", &error.to_string()),
        }
    }

    fn get_assertion(&mut self) {
        let Some(session) = self.session.clone() else {
            self.push_log("错误", "请先建立设备会话。");
            return;
        };
        let form = self.assertion_form.clone();
        let mut credentials = self.credentials.clone();

        match self.collect_backend_logs(|backend, log| {
            backend.get_assertion(&session, &form, &mut credentials, log)
        }) {
            Ok(result) => {
                self.get_assertion_output = to_pretty_json(&result);
                self.last_assertion = Some(result.clone());

                if result.status == 0 {
                    if let Some(credential_id_hex) = &result.credential_id_hex {
                        if let Some(index) = self.credentials.iter().position(|credential| {
                            credential.credential_id_hex == *credential_id_hex
                        }) {
                            self.credentials[index].sign_count = result.sign_count;
                            if let Some(user_name) = &result.user_name {
                                self.credentials[index].user_name = user_name.clone();
                            }
                            if let Some(display_name) = &result.display_name {
                                self.credentials[index].display_name = display_name.clone();
                            }
                            self.selected_credential = Some(index);
                        }
                    }
                    self.push_log("CTAP", "authenticatorGetAssertion 成功。");
                } else {
                    self.push_log(
                        "CTAP",
                        &format!("authenticatorGetAssertion => {}", result.status_name),
                    );
                }
            }
            Err(error) => self.push_log("错误", &error.to_string()),
        }
    }

    fn remove_selected_credential(&mut self) {
        if let Some(index) = self.selected_credential {
            if index < self.credentials.len() {
                let removed = self.credentials.remove(index);
                self.selected_credential = None;
                self.push_log(
                    "凭据",
                    &format!("已从本地会话缓存移除 {}", removed.credential_id_hex),
                );
            }
        }
    }

    fn fill_selected_credential_into_exclude_list(&mut self) {
        if let Some(index) = self.selected_credential {
            if let Some(credential) = self.credentials.get(index) {
                self.register_form.exclude_list = credential.credential_id_hex.clone();
            }
        }
    }

    fn fetch_diagnostics(&mut self) {
        match self.collect_backend_logs(|backend, log| backend.fetch_diagnostics(log)) {
            Ok(output) => {
                self.diagnostic_output = output.clone();
                self.push_log("诊断", "已拉取固件 FIDO 诊断日志。");
            }
            Err(error) => self.push_log("错误", &error.to_string()),
        }
    }

    fn clear_diagnostics(&mut self) {
        match self.collect_backend_logs(|backend, log| backend.clear_diagnostics(log)) {
            Ok(output) => {
                self.diagnostic_output = output;
                self.push_log("诊断", "已清空固件诊断日志。");
            }
            Err(error) => self.push_log("错误", &error.to_string()),
        }
    }

    fn list_credentials(&mut self) {
        match self.collect_backend_logs(|backend, log| backend.list_credentials(log)) {
            Ok(output) => {
                self.diagnostic_output = output;
                self.push_log("凭据", "已读取固件凭据列表。");
            }
            Err(error) => self.push_log("错误", &error.to_string()),
        }
    }

    fn clear_credentials(&mut self) {
        match self.collect_backend_logs(|backend, log| backend.clear_credentials(log)) {
            Ok(output) => {
                self.credentials.clear();
                self.selected_credential = None;
                self.push_log("凭据", "已清空固件中的凭据存储。");
                self.diagnostic_output = output;
            }
            Err(error) => self.push_log("错误", &error.to_string()),
        }
    }

    fn refresh_formal_credential_catalog(&mut self) {
        match self.collect_backend_logs(|backend, log| backend.get_formal_credential_catalog(log)) {
            Ok(snapshot) => {
                self.manager_catalog_output = to_pretty_json(&snapshot);
                self.push_log("管理", "已读取正式管理凭据目录。");
            }
            Err(error) => self.push_log("错误", &error.to_string()),
        }
    }

    fn refresh_formal_security_state(&mut self) {
        match self.collect_backend_logs(|backend, log| backend.get_formal_security_state(log)) {
            Ok(snapshot) => {
                self.manager_security_output = to_pretty_json(&snapshot);
                self.push_log("管理", "已读取正式管理安全状态。");
            }
            Err(error) => self.push_log("错误", &error.to_string()),
        }
    }

    fn render_title_bar(&self, ui: &mut egui::Ui) {
        ui.horizontal(|ui| {
            ui.add_space(8.0);
            ui.vertical(|ui| {
                ui.label(RichText::new("MeowKey Manager").size(20.0).strong());
                ui.label(
                    RichText::new("正式管理通道 / 调试 HID 工作台")
                        .size(12.0)
                        .color(muted_text()),
                );
            });
            ui.add_space(12.0);
            ui.separator();
            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                status_chip(
                    ui,
                    self.session
                        .as_ref()
                        .map(|session| session.state_label.as_str())
                        .unwrap_or("空闲"),
                    chip_blue(),
                );
                status_chip(ui, self.backend.backend_name(), chip_neutral());
                status_chip(ui, env!("CARGO_PKG_VERSION"), chip_neutral());
            });
        });
    }

    fn render_sidebar(&mut self, ui: &mut egui::Ui) {
        ScrollArea::vertical()
            .id_salt("sidebar-scroll")
            .show(ui, |ui| {
                card(ui, |ui| {
                    card_header(
                        ui,
                        "连接会话",
                        "压缩左栏宽度，并保留真实调试 HID / 预览后端切换。",
                    );
                    ui.horizontal_wrapped(|ui| {
                        action_button(ui, "连接管理通道")
                            .clicked()
                            .then(|| self.connect_manager_channel());
                        action_button(ui, "连接调试 HID")
                            .clicked()
                            .then(|| self.connect_debug_hid());
                        secondary_button(ui, "预览后端")
                            .clicked()
                            .then(|| self.connect_preview());
                    });
                    ui.add_space(8.0);
                    ui.horizontal_wrapped(|ui| {
                        secondary_button(ui, "断开")
                            .clicked()
                            .then(|| self.disconnect());
                        secondary_button(ui, "初始化")
                            .clicked()
                            .then(|| self.init_channel());
                        secondary_button(ui, "读取信息")
                            .clicked()
                            .then(|| self.refresh_info());
                    });
                    ui.add_space(8.0);
                    ui.horizontal_wrapped(|ui| {
                        secondary_button(ui, "拉取诊断")
                            .clicked()
                            .then(|| self.fetch_diagnostics());
                        secondary_button(ui, "正式目录")
                            .clicked()
                            .then(|| self.refresh_formal_credential_catalog());
                        secondary_button(ui, "安全状态")
                            .clicked()
                            .then(|| self.refresh_formal_security_state());
                        secondary_button(ui, "列出凭据")
                            .clicked()
                            .then(|| self.list_credentials());
                        secondary_button(ui, "清空诊断")
                            .clicked()
                            .then(|| self.clear_diagnostics());
                        secondary_button(ui, "清空凭据")
                            .clicked()
                            .then(|| self.clear_credentials());
                    });
                });

                ui.add_space(10.0);

                card(ui, |ui| {
                    card_header(ui, "会话状态", "真实设备状态和当前通道。");
                    let session = self.session.clone().unwrap_or_default();
                    let capability_text = if session.capabilities.is_empty() {
                        "-".to_string()
                    } else {
                        session.capabilities.join(", ")
                    };
                    fact_row(ui, "后端", &session.backend_name);
                    fact_row(ui, "设备", &session.device_name);
                    fact_row(ui, "设备 ID", &session.device_id);
                    fact_row(ui, "通道", &session.channel_id);
                    fact_row(ui, "状态", &session.state_label);
                    fact_row(ui, "能力", &capability_text);
                });

                ui.add_space(10.0);

                card(ui, |ui| {
                    card_header(
                        ui,
                        "注册测试",
                        "最小 makeCredential 请求；支持 excludeList。",
                    );
                    form_label(ui, "RP ID");
                    ui.add(input_line(&mut self.register_form.rp_id));
                    form_label(ui, "用户 ID");
                    ui.add(input_line(&mut self.register_form.user_id));
                    form_label(ui, "用户名");
                    ui.add(input_line(&mut self.register_form.user_name));
                    form_label(ui, "显示名");
                    ui.add(input_line(&mut self.register_form.display_name));
                    form_label(ui, "excludeList");
                    ui.add(
                        TextEdit::multiline(&mut self.register_form.exclude_list)
                            .desired_rows(4)
                            .hint_text("每行一个 credentialId"),
                    );
                    ui.add_space(6.0);
                    action_button(ui, "注册凭据")
                        .clicked()
                        .then(|| self.register_credential());
                });

                ui.add_space(10.0);

                card(ui, |ui| {
                    card_header(ui, "断言测试", "可指定 credentialId，留空走 discoverable。");
                    form_label(ui, "RP ID");
                    ui.add(input_line(&mut self.assertion_form.rp_id));
                    form_label(ui, "Credential ID");
                    ui.add(
                        TextEdit::singleline(&mut self.assertion_form.credential_id)
                            .hint_text("可选：十六进制 credentialId"),
                    );
                    ui.add_space(6.0);
                    action_button(ui, "获取断言")
                        .clicked()
                        .then(|| self.get_assertion());
                });
            });
    }

    fn render_main(&mut self, ui: &mut egui::Ui, ctx: &egui::Context) {
        ScrollArea::vertical().id_salt("main-scroll").show(ui, |ui| {
            card(ui, |ui| {
                card_header(ui, "当前摘要", "把 Android-Cam-Bridge 顶部摘要卡的节奏搬到这里。");
                ui.columns(4, |columns| {
                    summary_card(&mut columns[0], "后端", self.backend.backend_name());
                    summary_card(
                        &mut columns[1],
                        "设备",
                        self.session
                            .as_ref()
                            .map(|session| session.device_name.as_str())
                            .unwrap_or("未连接"),
                    );
                    summary_card(
                        &mut columns[2],
                        "通道",
                        self.session
                            .as_ref()
                            .map(|session| session.channel_id.as_str())
                            .unwrap_or("未分配"),
                    );
                    summary_card(
                        &mut columns[3],
                        "状态",
                        self.session
                            .as_ref()
                            .map(|session| session.state_label.as_str())
                            .unwrap_or("空闲"),
                    );
                });
            });

            ui.add_space(12.0);

            ui.columns(2, |columns| {
                card(&mut columns[0], |ui| {
                    card_header(ui, "正式凭据目录", "管理通道 0x03 的分页聚合结果。");
                    copy_toolbar(ui, ctx, "正式凭据目录", &self.manager_catalog_output);
                    output_panel(ui, "manager-catalog-output", &self.manager_catalog_output, 220.0);
                });
                card(&mut columns[1], |ui| {
                    card_header(ui, "正式安全状态", "管理通道 0x04 返回的结构化安全视图。");
                    copy_toolbar(ui, ctx, "正式安全状态", &self.manager_security_output);
                    output_panel(ui, "manager-security-output", &self.manager_security_output, 220.0);
                });
            });

            ui.add_space(12.0);

            ui.columns(2, |columns| {
                card(&mut columns[0], |ui| {
                    card_header(ui, "初始化响应", "CTAPHID_INIT 的结构化结果。");
                    copy_toolbar(ui, ctx, "初始化响应", &self.init_output);
                    output_panel(ui, "init-output", &self.init_output, 150.0);
                });
                card(&mut columns[1], |ui| {
                    card_header(ui, "认证器信息", "authenticatorGetInfo 返回体。");
                    copy_toolbar(ui, ctx, "认证器信息", &self.info_output);
                    output_panel(ui, "info-output", &self.info_output, 170.0);
                });
            });

            ui.add_space(12.0);

            ui.columns(2, |columns| {
                card(&mut columns[0], |ui| {
                    card_header(ui, "注册结果", "makeCredential 的状态码、请求体和响应体。");
                    copy_toolbar(ui, ctx, "注册结果", &self.make_credential_output);
                    output_panel(ui, "register-output", &self.make_credential_output, 200.0);
                });
                card(&mut columns[1], |ui| {
                    card_header(ui, "断言结果", "getAssertion 的状态码、请求体和响应体。");
                    copy_toolbar(ui, ctx, "断言结果", &self.get_assertion_output);
                    output_panel(ui, "assertion-output", &self.get_assertion_output, 200.0);
                });
            });

            ui.add_space(12.0);

            ui.columns(2, |columns| {
                card(&mut columns[0], |ui| {
                    card_header(ui, "固件诊断", "拉取 FIDO 接口最近一次收到的真实系统请求轨迹。");
                    copy_toolbar(ui, ctx, "固件诊断", &self.diagnostic_output);
                    output_panel(ui, "diag-output", &self.diagnostic_output, 180.0);
                });

                card(&mut columns[1], |ui| {
                    card_header(ui, "凭据缓存", "当前会话看到的 credential 列表。");
                    ScrollArea::vertical()
                        .id_salt("credential-cache-scroll")
                        .max_height(240.0)
                        .show(ui, |ui| {
                            for (index, credential) in self.credentials.iter().enumerate() {
                                let selected = self.selected_credential == Some(index);
                                let title = format!("{}  {}", credential.rp_id, credential.user_name);
                                if ui.selectable_label(selected, title).clicked() {
                                    self.selected_credential = Some(index);
                                    self.assertion_form.credential_id = credential.credential_id_hex.clone();
                                }
                                if selected {
                                    accent_card(ui, |ui| {
                                        ui.monospace(format!(
                                            "credentialId: {}\nuserId: {}\ndisplayName: {}\nsignCount: {}\ndiscoverable: {}",
                                            credential.credential_id_hex,
                                            credential.user_id_hex,
                                            credential.display_name,
                                            credential.sign_count,
                                            credential.discoverable
                                        ));
                                    });
                                }
                                ui.add_space(6.0);
                            }
                        });
                    ui.add_space(8.0);
                    ui.horizontal_wrapped(|ui| {
                        secondary_button(ui, "移除选中凭据")
                            .clicked()
                            .then(|| self.remove_selected_credential());
                        secondary_button(ui, "填入 excludeList")
                            .clicked()
                            .then(|| self.fill_selected_credential_into_exclude_list());
                    });
                });
            });

            ui.add_space(12.0);

            card(ui, |ui| {
                card_header(ui, "原始日志", "按时间倒序保留 HID / CTAP 原始会话轨迹。");
                copy_toolbar(ui, ctx, "原始日志", &flatten_logs(&self.logs));
                ScrollArea::vertical()
                    .id_salt("raw-log-scroll")
                    .max_height(260.0)
                    .show(ui, |ui| {
                        for entry in &self.logs {
                            accent_card(ui, |ui| {
                                ui.horizontal_wrapped(|ui| {
                                    ui.label(
                                        RichText::new(&entry.timestamp)
                                            .monospace()
                                            .size(11.0)
                                            .color(muted_text()),
                                    );
                                    status_chip(ui, &entry.tag, chip_neutral());
                                });
                                ui.add_space(4.0);
                                ui.label(&entry.body);
                            });
                            ui.add_space(6.0);
                        }
                    });
            });
        });
    }
}

impl eframe::App for MeowKeyManagerApp {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        let mut visuals = egui::Visuals::light();
        visuals.panel_fill = Color32::from_rgb(244, 247, 251);
        visuals.window_fill = Color32::from_rgb(244, 247, 251);
        visuals.extreme_bg_color = Color32::from_rgb(247, 248, 250);
        visuals.widgets.noninteractive.bg_fill =
            Color32::from_rgba_unmultiplied(255, 255, 255, 225);
        visuals.widgets.inactive.bg_fill = Color32::from_rgba_unmultiplied(255, 255, 255, 240);
        visuals.widgets.hovered.bg_fill = Color32::from_rgb(236, 243, 252);
        visuals.widgets.active.bg_fill = Color32::from_rgb(228, 239, 251);
        visuals.selection.bg_fill = Color32::from_rgb(217, 233, 252);
        ctx.set_visuals(visuals);

        egui::TopBottomPanel::top("titlebar")
            .exact_height(58.0)
            .show(ctx, |ui| self.render_title_bar(ui));

        egui::SidePanel::left("sidebar")
            .default_width(292.0)
            .resizable(true)
            .show(ctx, |ui| self.render_sidebar(ui));

        egui::CentralPanel::default().show(ctx, |ui| self.render_main(ui, ctx));
    }
}

fn install_cjk_fonts(ctx: &egui::Context) {
    let mut fonts = egui::FontDefinitions::default();

    for path in FONT_CANDIDATES {
        if let Ok(bytes) = fs::read(path) {
            fonts.font_data.insert(
                "meowkey-cjk".to_string(),
                egui::FontData::from_owned(bytes).into(),
            );
            if let Some(family) = fonts.families.get_mut(&egui::FontFamily::Proportional) {
                family.insert(0, "meowkey-cjk".to_string());
            }
            if let Some(family) = fonts.families.get_mut(&egui::FontFamily::Monospace) {
                family.push("meowkey-cjk".to_string());
            }
            ctx.set_fonts(fonts);
            return;
        }
    }
}

fn card(ui: &mut egui::Ui, add_contents: impl FnOnce(&mut egui::Ui)) {
    egui::Frame {
        inner_margin: egui::Margin::same(18),
        corner_radius: egui::CornerRadius::same(22),
        fill: Color32::from_rgba_unmultiplied(252, 252, 253, 228),
        stroke: egui::Stroke::new(1.0, Color32::from_rgba_unmultiplied(0, 0, 0, 20)),
        ..Default::default()
    }
    .show(ui, add_contents);
}

fn accent_card(ui: &mut egui::Ui, add_contents: impl FnOnce(&mut egui::Ui)) {
    egui::Frame {
        inner_margin: egui::Margin::same(12),
        corner_radius: egui::CornerRadius::same(16),
        fill: Color32::from_rgba_unmultiplied(255, 255, 255, 244),
        stroke: egui::Stroke::new(1.0, Color32::from_rgba_unmultiplied(0, 0, 0, 16)),
        ..Default::default()
    }
    .show(ui, add_contents);
}

fn card_header(ui: &mut egui::Ui, title: &str, subtitle: &str) {
    ui.label(RichText::new(title).size(18.0).strong());
    ui.label(RichText::new(subtitle).size(12.0).color(muted_text()));
    ui.add_space(10.0);
}

fn summary_card(ui: &mut egui::Ui, label: &str, value: &str) {
    accent_card(ui, |ui| {
        ui.label(RichText::new(label).size(12.0).color(muted_text()));
        ui.add_space(2.0);
        ui.label(RichText::new(value).size(15.0).strong());
    });
}

fn fact_row(ui: &mut egui::Ui, label: &str, value: &str) {
    ui.horizontal_wrapped(|ui| {
        ui.set_min_width(ui.available_width());
        ui.label(RichText::new(label).size(12.0).color(muted_text()));
        ui.add_space(8.0);
        ui.label(RichText::new(value).size(13.0).strong());
    });
    ui.add_space(4.0);
}

fn form_label(ui: &mut egui::Ui, label: &str) {
    ui.label(RichText::new(label).size(12.0).color(muted_text()));
    ui.add_space(2.0);
}

fn input_line(value: &mut String) -> TextEdit<'_> {
    TextEdit::singleline(value)
        .desired_width(f32::INFINITY)
        .hint_text("")
}

fn output_panel(ui: &mut egui::Ui, id: &str, text: &str, height: f32) {
    accent_card(ui, |ui| {
        ui.set_min_height(height);
        let mut content = text.to_string();
        ScrollArea::vertical().id_salt(id).show(ui, |ui| {
            ui.add(
                TextEdit::multiline(&mut content)
                    .desired_width(f32::INFINITY)
                    .font(egui::TextStyle::Monospace)
                    .interactive(false)
                    .frame(false),
            );
        });
    });
}

fn copy_toolbar(ui: &mut egui::Ui, ctx: &egui::Context, label: &str, text: &str) {
    ui.horizontal(|ui| {
        if secondary_button(ui, "复制").clicked() {
            ctx.copy_text(text.to_string());
        }
        ui.label(
            RichText::new(format!("复制 {label}"))
                .size(11.0)
                .color(muted_text()),
        );
    });
    ui.add_space(6.0);
}

fn status_chip(ui: &mut egui::Ui, text: &str, fill: Color32) {
    egui::Frame {
        inner_margin: egui::Margin::symmetric(10, 6),
        corner_radius: egui::CornerRadius::same(11),
        fill,
        stroke: egui::Stroke::new(1.0, Color32::from_rgba_unmultiplied(0, 0, 0, 18)),
        ..Default::default()
    }
    .show(ui, |ui| {
        ui.label(RichText::new(text).size(11.5).strong());
    });
}

fn action_button(ui: &mut egui::Ui, label: &str) -> egui::Response {
    ui.add_sized(
        [106.0, 32.0],
        egui::Button::new(RichText::new(label).strong()).fill(Color32::from_rgb(231, 241, 252)),
    )
}

fn secondary_button(ui: &mut egui::Ui, label: &str) -> egui::Response {
    ui.add_sized([96.0, 32.0], egui::Button::new(label))
}

fn muted_text() -> Color32 {
    Color32::from_rgb(116, 123, 135)
}

fn chip_neutral() -> Color32 {
    Color32::from_rgba_unmultiplied(255, 255, 255, 240)
}

fn chip_blue() -> Color32 {
    Color32::from_rgba_unmultiplied(0, 103, 192, 26)
}

fn flatten_logs(entries: &[LogEntry]) -> String {
    entries
        .iter()
        .map(|entry| format!("[{}] {}\n{}", entry.timestamp, entry.tag, entry.body))
        .collect::<Vec<_>>()
        .join("\n\n")
}

fn to_pretty_json<T: Serialize>(value: &T) -> String {
    serde_json::to_string_pretty(value).unwrap_or_else(|error| format!("序列化失败: {error}"))
}
