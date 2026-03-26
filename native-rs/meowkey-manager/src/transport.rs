use std::{
    ffi::CString,
    time::{Duration, SystemTime, UNIX_EPOCH},
};

use anyhow::{Context, Result, anyhow, bail};
use getrandom::getrandom;
use hidapi::{HidApi, HidDevice};
use serde_json::{Value, json};

use crate::{
    cbor::{CborValue, decode as decode_cbor, encode as encode_cbor},
    models::{
        AssertionForm, AssertionRequestSnapshot, AssertionResult, CredentialRecord, InfoSnapshot,
        InitDeviceVersion, InitSnapshot, MakeCredentialRequestSnapshot, MakeCredentialResult,
        RegisterForm, SessionSnapshot,
    },
};

const DEBUG_USAGE_PAGE: u16 = 0xff00;
const DEBUG_USAGE: u16 = 0x01;
const FIDO_USAGE_PAGE: u16 = 0xf1d0;
const FIDO_USAGE: u16 = 0x01;
const DEFAULT_VENDOR_ID: u16 = 0xcafe;
const DEFAULT_PRODUCT_ID: u16 = 0x4004;
const PACKET_SIZE: usize = 64;
const INIT_PAYLOAD_SIZE: usize = 57;
const CONT_PAYLOAD_SIZE: usize = 59;
const BROADCAST_CID: u32 = 0xffff_ffff;
const RESPONSE_TIMEOUT: Duration = Duration::from_secs(2);

const CTAPHID_PING: u8 = 0x01;
const CTAPHID_INIT: u8 = 0x06;
const CTAPHID_CBOR: u8 = 0x10;
const CTAPHID_DIAG: u8 = 0x40;
const CTAPHID_ERROR: u8 = 0x3f;

const CTAP2_MAKE_CREDENTIAL: u8 = 0x01;
const CTAP2_GET_ASSERTION: u8 = 0x02;
const CTAP2_GET_INFO: u8 = 0x04;

const CTAP2_STATUS_OK: u8 = 0x00;
const CTAP2_ERR_INVALID_CBOR: u8 = 0x12;
const CTAP2_ERR_MISSING_PARAMETER: u8 = 0x14;
const CTAP2_ERR_CREDENTIAL_EXCLUDED: u8 = 0x19;
const CTAP2_ERR_INVALID_CREDENTIAL: u8 = 0x22;
const CTAP2_ERR_UNSUPPORTED_ALGORITHM: u8 = 0x26;
const CTAP2_ERR_KEY_STORE_FULL: u8 = 0x28;
const CTAP2_ERR_UNSUPPORTED_OPTION: u8 = 0x2b;
const CTAP2_ERR_NO_CREDENTIALS: u8 = 0x2e;

type Logger<'a> = &'a mut dyn FnMut(&str, &str);

pub trait ManagerBackend {
    fn backend_name(&self) -> &'static str;

    fn connect(&mut self, log: Logger<'_>) -> Result<SessionSnapshot>;

    fn disconnect(&mut self) -> Result<()> {
        Ok(())
    }

    fn fetch_diagnostics(&mut self, _log: Logger<'_>) -> Result<String> {
        Ok("当前后端不支持固件诊断日志。".to_string())
    }

    fn clear_diagnostics(&mut self, _log: Logger<'_>) -> Result<String> {
        Ok("当前后端不支持固件诊断日志。".to_string())
    }

    fn list_credentials(&mut self, _log: Logger<'_>) -> Result<String> {
        Ok("当前后端不支持固件凭据列表。".to_string())
    }

    fn clear_credentials(&mut self, _log: Logger<'_>) -> Result<String> {
        Ok("当前后端不支持固件凭据擦除。".to_string())
    }

    fn init_channel(
        &mut self,
        session: &mut SessionSnapshot,
        log: Logger<'_>,
    ) -> Result<InitSnapshot>;

    fn get_info(&mut self, session: &SessionSnapshot, log: Logger<'_>) -> Result<InfoSnapshot>;

    fn make_credential(
        &mut self,
        session: &SessionSnapshot,
        form: &RegisterForm,
        existing: &[CredentialRecord],
        log: Logger<'_>,
    ) -> Result<MakeCredentialResult>;

    fn get_assertion(
        &mut self,
        session: &SessionSnapshot,
        form: &AssertionForm,
        credentials: &mut [CredentialRecord],
        log: Logger<'_>,
    ) -> Result<AssertionResult>;
}

pub struct PreviewBackend {
    next_channel_id: u32,
    next_credential_seed: u128,
}

impl Default for PreviewBackend {
    fn default() -> Self {
        Self {
            next_channel_id: 1,
            next_credential_seed: 0x9923_3e84_a981_d740_a416_a14a_764a_db68,
        }
    }
}

impl PreviewBackend {
    fn new_credential_id(&mut self) -> String {
        let hex = format!(
            "{:032x}{:032x}",
            self.next_credential_seed,
            self.next_credential_seed ^ 0x4b5c_a424_8d28_979b_6074_9905_a170_8b76
        );
        self.next_credential_seed = self.next_credential_seed.wrapping_add(0x10001);
        hex
    }

    fn now_label() -> String {
        let seconds = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map(|duration| duration.as_secs())
            .unwrap_or(0);
        format!("preview-{seconds}")
    }
}

impl ManagerBackend for PreviewBackend {
    fn backend_name(&self) -> &'static str {
        "Rust 预览后端"
    }

    fn connect(&mut self, log: Logger<'_>) -> Result<SessionSnapshot> {
        log("会话", "已连接到预览后端设备模型。");
        Ok(SessionSnapshot {
            backend_name: self.backend_name().to_string(),
            device_name: "MeowKey RP2350".to_string(),
            device_id: format_device_id(DEFAULT_VENDOR_ID, DEFAULT_PRODUCT_ID),
            channel_id: "未初始化".to_string(),
            capabilities: vec!["支持 CBOR".to_string(), "无旧版 MSG".to_string()],
            state_label: "已连接".to_string(),
        })
    }

    fn init_channel(
        &mut self,
        session: &mut SessionSnapshot,
        log: Logger<'_>,
    ) -> Result<InitSnapshot> {
        let mut nonce = vec![0u8; 8];
        getrandom(&mut nonce).map_err(|error| anyhow!("生成随机 nonce 失败: {error}"))?;

        session.channel_id = format!("0x{:08x}", self.next_channel_id);
        self.next_channel_id = self.next_channel_id.wrapping_add(1);
        session.state_label = "通道已建立".to_string();

        let snapshot = InitSnapshot {
            nonce: bytes_to_hex_compact(&nonce),
            channel_id: session.channel_id.clone(),
            ctaphid_version: 2,
            device_version: InitDeviceVersion {
                major: 1,
                minor: 0,
                build: 1,
            },
            capabilities: session.capabilities.clone(),
        };
        log(
            "初始化",
            &format!("预览后端已分配通道 {}", snapshot.channel_id),
        );
        Ok(snapshot)
    }

    fn get_info(&mut self, _session: &SessionSnapshot, log: Logger<'_>) -> Result<InfoSnapshot> {
        let data = json!({
            "1": ["FIDO_2_1", "FIDO_2_0"],
            "2": [],
            "3": "hex:4d656f774b6579005250323335300001",
            "4": {
                "rk": true,
                "up": true,
                "uv": false,
                "plat": false,
                "clientPin": false
            },
            "5": 1024,
            "9": ["usb"],
            "10": [
                {
                    "type": "public-key",
                    "alg": -7
                }
            ]
        });

        let snapshot = InfoSnapshot {
            status: CTAP2_STATUS_OK,
            status_name: ctap_status_name(CTAP2_STATUS_OK),
            versions: vec!["FIDO_2_1".to_string(), "FIDO_2_0".to_string()],
            capabilities: vec!["usb".to_string()],
            options: vec![
                "rk=true".to_string(),
                "up=true".to_string(),
                "uv=false".to_string(),
                "plat=false".to_string(),
                "clientPin=false".to_string(),
            ],
            data,
        };
        log("信息", "预览后端返回固定的 authenticatorGetInfo 响应。");
        Ok(snapshot)
    }

    fn make_credential(
        &mut self,
        _session: &SessionSnapshot,
        form: &RegisterForm,
        existing: &[CredentialRecord],
        log: Logger<'_>,
    ) -> Result<MakeCredentialResult> {
        if form.rp_id.trim().is_empty() {
            bail!("RP ID 不能为空");
        }
        if form.user_id.trim().is_empty() {
            bail!("用户 ID 不能为空");
        }

        let client_data_hash = random_bytes(32)?;
        let exclude_list = parse_hex_lines(&form.exclude_list)?;
        let exclude_list_hex = exclude_list
            .iter()
            .map(|entry| bytes_to_hex_compact(entry))
            .collect::<Vec<_>>();

        if exclude_list_hex.iter().any(|needle| {
            existing
                .iter()
                .any(|credential| credential.credential_id_hex == *needle)
        }) {
            log(
                "CTAP",
                "预览后端命中 excludeList，返回 CTAP2_ERR_CREDENTIAL_EXCLUDED。",
            );
            return Ok(MakeCredentialResult {
                status: CTAP2_ERR_CREDENTIAL_EXCLUDED,
                status_name: ctap_status_name(CTAP2_ERR_CREDENTIAL_EXCLUDED),
                client_data_hash: bytes_to_hex_compact(&client_data_hash),
                request: MakeCredentialRequestSnapshot {
                    rp_id: form.rp_id.trim().to_string(),
                    user_id: bytes_to_hex_compact(form.user_id.trim().as_bytes()),
                    user_name: form.user_name.trim().to_string(),
                    display_name: form.display_name.trim().to_string(),
                    exclude_list: exclude_list_hex,
                },
                credential_id_hex: None,
                response: Value::Null,
            });
        }

        let credential_id_hex = self.new_credential_id();
        let result = MakeCredentialResult {
            status: CTAP2_STATUS_OK,
            status_name: ctap_status_name(CTAP2_STATUS_OK),
            client_data_hash: bytes_to_hex_compact(&client_data_hash),
            request: MakeCredentialRequestSnapshot {
                rp_id: form.rp_id.trim().to_string(),
                user_id: bytes_to_hex_compact(form.user_id.trim().as_bytes()),
                user_name: form.user_name.trim().to_string(),
                display_name: form.display_name.trim().to_string(),
                exclude_list: exclude_list_hex,
            },
            credential_id_hex: Some(credential_id_hex.clone()),
            response: json!({
                "fmt": "none",
                "credentialId": credential_id_hex,
                "backend": "preview"
            }),
        };

        log(
            "注册",
            &format!(
                "预览后端已生成凭据 {}\nRP: {}\n用户: {}",
                result.credential_id_hex.as_deref().unwrap_or("-"),
                result.request.rp_id,
                result.request.user_name
            ),
        );
        Ok(result)
    }

    fn get_assertion(
        &mut self,
        _session: &SessionSnapshot,
        form: &AssertionForm,
        credentials: &mut [CredentialRecord],
        log: Logger<'_>,
    ) -> Result<AssertionResult> {
        if form.rp_id.trim().is_empty() {
            bail!("RP ID 不能为空");
        }

        let client_data_hash = random_bytes(32)?;
        let selected = if form.credential_id.trim().is_empty() {
            credentials
                .iter_mut()
                .find(|credential| credential.discoverable && credential.rp_id == form.rp_id.trim())
        } else {
            let needle = normalize_hex(&form.credential_id)?;
            credentials.iter_mut().find(|credential| {
                credential.credential_id_hex == needle && credential.rp_id == form.rp_id.trim()
            })
        };

        let Some(credential) = selected else {
            let status = if form.credential_id.trim().is_empty() {
                CTAP2_ERR_NO_CREDENTIALS
            } else {
                CTAP2_ERR_INVALID_CREDENTIAL
            };
            log(
                "CTAP",
                &format!("预览后端返回 {}", ctap_status_name(status)),
            );
            return Ok(AssertionResult {
                status,
                status_name: ctap_status_name(status),
                client_data_hash: bytes_to_hex_compact(&client_data_hash),
                request: AssertionRequestSnapshot {
                    rp_id: form.rp_id.trim().to_string(),
                    credential_id: (!form.credential_id.trim().is_empty())
                        .then(|| normalize_hex(&form.credential_id))
                        .transpose()?,
                },
                credential_id_hex: None,
                sign_count: 0,
                user_name: None,
                display_name: None,
                response: Value::Null,
            });
        };

        credential.sign_count = credential.sign_count.saturating_add(1);
        let result = AssertionResult {
            status: CTAP2_STATUS_OK,
            status_name: ctap_status_name(CTAP2_STATUS_OK),
            client_data_hash: bytes_to_hex_compact(&client_data_hash),
            request: AssertionRequestSnapshot {
                rp_id: form.rp_id.trim().to_string(),
                credential_id: (!form.credential_id.trim().is_empty())
                    .then(|| normalize_hex(&form.credential_id))
                    .transpose()?,
            },
            credential_id_hex: Some(credential.credential_id_hex.clone()),
            sign_count: credential.sign_count,
            user_name: Some(credential.user_name.clone()),
            display_name: Some(credential.display_name.clone()),
            response: json!({
                "credentialId": credential.credential_id_hex,
                "signCount": credential.sign_count,
                "user": {
                    "name": credential.user_name,
                    "displayName": credential.display_name
                },
                "backend": "preview"
            }),
        };

        log(
            "断言",
            &format!(
                "预览后端已获取断言\ncredentialId: {}\nsignCount: {}",
                result.credential_id_hex.as_deref().unwrap_or("-"),
                result.sign_count
            ),
        );
        Ok(result)
    }
}

#[derive(Default)]
pub struct DebugHidBackend {
    api: Option<HidApi>,
    device: Option<HidDevice>,
    channel_id: Option<u32>,
    capabilities: Vec<String>,
    device_name: String,
    vendor_id: u16,
    product_id: u16,
}

pub fn default_register_form() -> RegisterForm {
    RegisterForm {
        rp_id: "meowkey.local".to_string(),
        user_id: "meowhuan".to_string(),
        user_name: "Meowhuan".to_string(),
        display_name: "Meowhuan Debug".to_string(),
        exclude_list: String::new(),
    }
}

pub fn default_assertion_form() -> AssertionForm {
    AssertionForm {
        rp_id: "meowkey.local".to_string(),
        credential_id: String::new(),
    }
}

pub fn default_log_body() -> String {
    format!(
        "Rust 原生管理器已切到双后端模式。\n可连真实调试 HID，也可切回预览后端。\n当前时间标签：{}",
        PreviewBackend::now_label()
    )
}

struct ResponseMessage {
    command: u8,
    payload: Vec<u8>,
}

struct ParsedPacket {
    raw: [u8; PACKET_SIZE],
    is_init: bool,
    cid: u32,
    command: Option<u8>,
    sequence: Option<u8>,
    payload_length: Option<usize>,
    payload: Vec<u8>,
}

struct DeviceCandidate {
    path: CString,
    product_name: String,
    vendor_id: u16,
    product_id: u16,
    usage_page: u16,
    usage: u16,
    interface_number: i32,
    score: u8,
}

impl ManagerBackend for DebugHidBackend {
    fn backend_name(&self) -> &'static str {
        "Rust Debug HID 后端"
    }

    fn connect(&mut self, log: Logger<'_>) -> Result<SessionSnapshot> {
        let api = HidApi::new().context("初始化 HID 子系统失败")?;
        let candidate = pick_debug_candidate(&api)?;
        let device = api
            .open_path(&candidate.path)
            .with_context(|| format!("打开调试 HID 设备失败: {}", candidate.product_name))?;

        self.api = Some(api);
        self.device = Some(device);
        self.channel_id = None;
        self.capabilities.clear();
        self.device_name = candidate.product_name.clone();
        self.vendor_id = candidate.vendor_id;
        self.product_id = candidate.product_id;

        log(
            "会话",
            &format!(
                "已连接到调试 HID：{}\nVID:PID={}\nusagePage=0x{:04x}\nusage=0x{:04x}\ninterface={}",
                candidate.product_name,
                format_device_id(candidate.vendor_id, candidate.product_id),
                candidate.usage_page,
                candidate.usage,
                candidate.interface_number
            ),
        );

        Ok(SessionSnapshot {
            backend_name: self.backend_name().to_string(),
            device_name: candidate.product_name,
            device_id: format_device_id(candidate.vendor_id, candidate.product_id),
            channel_id: "未初始化".to_string(),
            capabilities: Vec::new(),
            state_label: "已连接".to_string(),
        })
    }

    fn disconnect(&mut self) -> Result<()> {
        self.device = None;
        self.api = None;
        self.channel_id = None;
        self.capabilities.clear();
        Ok(())
    }

    fn fetch_diagnostics(&mut self, log: Logger<'_>) -> Result<String> {
        let packet = self.send_command(
            self.channel_id
                .ok_or_else(|| anyhow!("请先执行 CTAPHID 初始化。"))?,
            CTAPHID_DIAG,
            &[1u8],
            log,
        )?;
        Ok(String::from_utf8_lossy(&packet.payload).to_string())
    }

    fn clear_diagnostics(&mut self, log: Logger<'_>) -> Result<String> {
        let packet = self.send_command(
            self.channel_id
                .ok_or_else(|| anyhow!("请先执行 CTAPHID 初始化。"))?,
            CTAPHID_DIAG,
            &[2u8],
            log,
        )?;
        Ok(String::from_utf8_lossy(&packet.payload).to_string())
    }

    fn list_credentials(&mut self, log: Logger<'_>) -> Result<String> {
        let packet = self.send_command(
            self.channel_id
                .ok_or_else(|| anyhow!("请先执行 CTAPHID 初始化。"))?,
            CTAPHID_DIAG,
            &[4u8],
            log,
        )?;
        Ok(String::from_utf8_lossy(&packet.payload).to_string())
    }

    fn clear_credentials(&mut self, log: Logger<'_>) -> Result<String> {
        let packet = self.send_command(
            self.channel_id
                .ok_or_else(|| anyhow!("请先执行 CTAPHID 初始化。"))?,
            CTAPHID_DIAG,
            &[3u8],
            log,
        )?;
        Ok(String::from_utf8_lossy(&packet.payload).to_string())
    }

    fn init_channel(
        &mut self,
        session: &mut SessionSnapshot,
        log: Logger<'_>,
    ) -> Result<InitSnapshot> {
        let nonce = random_bytes(8)?;
        let response = self.send_command(BROADCAST_CID, CTAPHID_INIT, &nonce, log)?;
        if response.command != CTAPHID_INIT {
            bail!("初始化响应命令不正确: {}", command_name(response.command));
        }
        if response.payload.len() < 17 {
            bail!("初始化响应长度不足: {}", response.payload.len());
        }

        let channel_id = u32::from_be_bytes(response.payload[8..12].try_into()?);
        let capabilities = decode_capabilities(response.payload[16]);

        self.channel_id = Some(channel_id);
        self.capabilities = capabilities.clone();
        session.channel_id = format!("0x{channel_id:08x}");
        session.capabilities = capabilities.clone();
        session.state_label = "通道已建立".to_string();

        Ok(InitSnapshot {
            nonce: bytes_to_hex_compact(&nonce),
            channel_id: session.channel_id.clone(),
            ctaphid_version: response.payload[12],
            device_version: InitDeviceVersion {
                major: response.payload[13],
                minor: response.payload[14],
                build: response.payload[15],
            },
            capabilities,
        })
    }

    fn get_info(&mut self, _session: &SessionSnapshot, log: Logger<'_>) -> Result<InfoSnapshot> {
        let packet = self.send_cbor(&[CTAP2_GET_INFO], log)?;
        let status = read_status_byte(&packet.payload, "authenticatorGetInfo")?;
        if status != CTAP2_STATUS_OK {
            return Ok(InfoSnapshot {
                status,
                status_name: ctap_status_name(status),
                data: json!({
                    "error": format!("CTAP 返回了非零状态码 {}", ctap_status_name(status))
                }),
                versions: Vec::new(),
                capabilities: Vec::new(),
                options: Vec::new(),
            });
        }

        let (decoded, _) = decode_cbor(&packet.payload[1..])?;
        let data = decoded.to_json();
        let versions = extract_text_array(decoded.get_int_key(1));
        let capabilities = extract_text_array(decoded.get_int_key(9));
        let options = extract_options(decoded.get_int_key(4));

        Ok(InfoSnapshot {
            status,
            status_name: ctap_status_name(status),
            data,
            versions,
            capabilities,
            options,
        })
    }

    fn make_credential(
        &mut self,
        _session: &SessionSnapshot,
        form: &RegisterForm,
        _existing: &[CredentialRecord],
        log: Logger<'_>,
    ) -> Result<MakeCredentialResult> {
        if form.rp_id.trim().is_empty() {
            bail!("RP ID 不能为空");
        }
        if form.user_id.trim().is_empty() {
            bail!("用户 ID 不能为空");
        }

        let client_data_hash = random_bytes(32)?;
        let exclude_list = parse_hex_lines(&form.exclude_list)?;
        let request = MakeCredentialRequestSnapshot {
            rp_id: form.rp_id.trim().to_string(),
            user_id: bytes_to_hex_compact(form.user_id.trim().as_bytes()),
            user_name: form.user_name.trim().to_string(),
            display_name: form.display_name.trim().to_string(),
            exclude_list: exclude_list
                .iter()
                .map(|entry| bytes_to_hex_compact(entry))
                .collect(),
        };

        let mut entries = vec![
            (
                CborValue::Integer(1),
                CborValue::Bytes(client_data_hash.clone()),
            ),
            (
                CborValue::Integer(2),
                CborValue::Map(vec![
                    (
                        CborValue::Text("id".to_string()),
                        CborValue::Text(request.rp_id.clone()),
                    ),
                    (
                        CborValue::Text("name".to_string()),
                        CborValue::Text(request.rp_id.clone()),
                    ),
                ]),
            ),
            (
                CborValue::Integer(3),
                CborValue::Map(vec![
                    (
                        CborValue::Text("id".to_string()),
                        CborValue::Bytes(form.user_id.trim().as_bytes().to_vec()),
                    ),
                    (
                        CborValue::Text("name".to_string()),
                        CborValue::Text(request.user_name.clone()),
                    ),
                    (
                        CborValue::Text("displayName".to_string()),
                        CborValue::Text(request.display_name.clone()),
                    ),
                ]),
            ),
            (
                CborValue::Integer(4),
                CborValue::Array(vec![CborValue::Map(vec![
                    (
                        CborValue::Text("type".to_string()),
                        CborValue::Text("public-key".to_string()),
                    ),
                    (CborValue::Text("alg".to_string()), CborValue::Integer(-7)),
                ])]),
            ),
            (
                CborValue::Integer(7),
                CborValue::Map(vec![
                    (CborValue::Text("rk".to_string()), CborValue::Bool(true)),
                    (CborValue::Text("uv".to_string()), CborValue::Bool(false)),
                ]),
            ),
        ];
        if !exclude_list.is_empty() {
            let value = exclude_list
                .iter()
                .map(|entry| {
                    CborValue::Map(vec![
                        (
                            CborValue::Text("type".to_string()),
                            CborValue::Text("public-key".to_string()),
                        ),
                        (
                            CborValue::Text("id".to_string()),
                            CborValue::Bytes(entry.clone()),
                        ),
                    ])
                })
                .collect();
            entries.push((CborValue::Integer(5), CborValue::Array(value)));
        }

        let mut payload = vec![CTAP2_MAKE_CREDENTIAL];
        payload.extend_from_slice(&encode_cbor(&CborValue::Map(entries))?);

        let packet = self.send_cbor(&payload, log)?;
        let status = read_status_byte(&packet.payload, "authenticatorMakeCredential")?;

        let (credential_id_hex, response) = if status == CTAP2_STATUS_OK {
            let (decoded, _) = decode_cbor(&packet.payload[1..])?;
            (
                extract_credential_id_from_make_credential(&decoded).map(bytes_to_hex_compact),
                decoded.to_json(),
            )
        } else {
            (None, Value::Null)
        };

        Ok(MakeCredentialResult {
            status,
            status_name: ctap_status_name(status),
            client_data_hash: bytes_to_hex_compact(&client_data_hash),
            request,
            credential_id_hex,
            response,
        })
    }

    fn get_assertion(
        &mut self,
        _session: &SessionSnapshot,
        form: &AssertionForm,
        _credentials: &mut [CredentialRecord],
        log: Logger<'_>,
    ) -> Result<AssertionResult> {
        if form.rp_id.trim().is_empty() {
            bail!("RP ID 不能为空");
        }

        let client_data_hash = random_bytes(32)?;
        let credential_id = normalize_hex(&form.credential_id)?;
        let credential_bytes = if credential_id.is_empty() {
            None
        } else {
            Some(hex_to_bytes(&credential_id)?)
        };

        let mut entries = vec![
            (
                CborValue::Integer(1),
                CborValue::Text(form.rp_id.trim().to_string()),
            ),
            (
                CborValue::Integer(2),
                CborValue::Bytes(client_data_hash.clone()),
            ),
            (
                CborValue::Integer(5),
                CborValue::Map(vec![(
                    CborValue::Text("uv".to_string()),
                    CborValue::Bool(false),
                )]),
            ),
        ];
        if let Some(credential) = &credential_bytes {
            entries.push((
                CborValue::Integer(3),
                CborValue::Array(vec![CborValue::Map(vec![
                    (
                        CborValue::Text("type".to_string()),
                        CborValue::Text("public-key".to_string()),
                    ),
                    (
                        CborValue::Text("id".to_string()),
                        CborValue::Bytes(credential.clone()),
                    ),
                ])]),
            ));
        }

        let mut payload = vec![CTAP2_GET_ASSERTION];
        payload.extend_from_slice(&encode_cbor(&CborValue::Map(entries))?);

        let packet = self.send_cbor(&payload, log)?;
        let status = read_status_byte(&packet.payload, "authenticatorGetAssertion")?;
        if status != CTAP2_STATUS_OK {
            return Ok(AssertionResult {
                status,
                status_name: ctap_status_name(status),
                client_data_hash: bytes_to_hex_compact(&client_data_hash),
                request: AssertionRequestSnapshot {
                    rp_id: form.rp_id.trim().to_string(),
                    credential_id: credential_bytes
                        .as_ref()
                        .map(|bytes| bytes_to_hex_compact(bytes)),
                },
                credential_id_hex: None,
                sign_count: 0,
                user_name: None,
                display_name: None,
                response: Value::Null,
            });
        }

        let (decoded, _) = decode_cbor(&packet.payload[1..])?;
        let response = decoded.to_json();
        let credential_id_hex =
            extract_credential_id_from_assertion(&decoded).map(bytes_to_hex_compact);
        let sign_count = extract_sign_count_from_assertion(&decoded).unwrap_or(0);
        let user_name = decoded
            .get_int_key(4)
            .and_then(|value| value.get_text_key("name"))
            .and_then(CborValue::as_text)
            .map(str::to_string);
        let display_name = decoded
            .get_int_key(4)
            .and_then(|value| value.get_text_key("displayName"))
            .and_then(CborValue::as_text)
            .map(str::to_string);

        Ok(AssertionResult {
            status,
            status_name: ctap_status_name(status),
            client_data_hash: bytes_to_hex_compact(&client_data_hash),
            request: AssertionRequestSnapshot {
                rp_id: form.rp_id.trim().to_string(),
                credential_id: credential_bytes
                    .as_ref()
                    .map(|bytes| bytes_to_hex_compact(bytes)),
            },
            credential_id_hex,
            sign_count,
            user_name,
            display_name,
            response,
        })
    }
}

impl DebugHidBackend {
    fn send_cbor(&self, payload: &[u8], log: Logger<'_>) -> Result<ResponseMessage> {
        let cid = self
            .channel_id
            .ok_or_else(|| anyhow!("请先执行 CTAPHID 初始化。"))?;
        self.send_command(cid, CTAPHID_CBOR, payload, log)
    }

    fn send_command(
        &self,
        cid: u32,
        command: u8,
        payload: &[u8],
        log: Logger<'_>,
    ) -> Result<ResponseMessage> {
        let device = self
            .device
            .as_ref()
            .ok_or_else(|| anyhow!("当前没有已连接的 HID 设备。"))?;

        let first_chunk_len = payload.len().min(INIT_PAYLOAD_SIZE);
        let init_packet =
            make_init_packet(cid, command, payload.len(), &payload[..first_chunk_len]);
        log(
            &format!("发送 {}", command_name(command)),
            &format!(
                "cid=0x{cid:08x}\nlen={}\n{}",
                payload.len(),
                format_hex(&init_packet)
            ),
        );
        write_packet(device, &init_packet)?;

        let mut offset = INIT_PAYLOAD_SIZE;
        let mut sequence = 0u8;
        while offset < payload.len() {
            let end = (offset + CONT_PAYLOAD_SIZE).min(payload.len());
            let packet = make_continuation_packet(cid, sequence, &payload[offset..end]);
            log(
                &format!("发送续包 {}", command_name(command)),
                &format!("cid=0x{cid:08x}\nseq={sequence}\n{}", format_hex(&packet)),
            );
            write_packet(device, &packet)?;
            offset = end;
            sequence = sequence.wrapping_add(1);
        }

        let start = std::time::Instant::now();
        let mut expected_cid = None;
        let mut expected_command = None;
        let mut total_length = None;
        let mut received_length = 0usize;
        let mut next_seq = 0u8;
        let mut chunks: Vec<Vec<u8>> = Vec::new();

        loop {
            let elapsed = start.elapsed();
            if elapsed >= RESPONSE_TIMEOUT {
                bail!("等待 CTAPHID 响应超时。");
            }
            let remaining = (RESPONSE_TIMEOUT - elapsed).as_millis() as i32;
            let raw = read_packet(device, remaining.max(1))?;
            let packet = parse_packet(raw)?;
            if packet.is_init {
                log(
                    &format!("接收 {}", command_name(packet.command.unwrap_or_default())),
                    &format!(
                        "cid=0x{:08x}\nlen={}\n{}",
                        packet.cid,
                        packet.payload_length.unwrap_or(0),
                        format_hex(&packet.raw)
                    ),
                );
                expected_cid = Some(packet.cid);
                expected_command = packet.command;
                total_length = packet.payload_length;
                let body_len = packet.payload_length.unwrap_or(0);
                let first_len = packet.payload.len().min(body_len);
                received_length = first_len;
                next_seq = 0;
                chunks = vec![packet.payload[..first_len].to_vec()];

                if body_len == 0 || received_length >= body_len {
                    break;
                }
            } else {
                log(
                    "接收续包",
                    &format!(
                        "cid=0x{:08x}\nseq={}\n{}",
                        packet.cid,
                        packet.sequence.unwrap_or_default(),
                        format_hex(&packet.raw)
                    ),
                );

                let Some(pending_cid) = expected_cid else {
                    continue;
                };
                let Some(total) = total_length else {
                    continue;
                };

                if packet.cid != pending_cid || packet.sequence != Some(next_seq) {
                    bail!("收到的续包顺序不正确。");
                }

                let chunk_len = (total - received_length).min(packet.payload.len());
                chunks.push(packet.payload[..chunk_len].to_vec());
                received_length += chunk_len;
                next_seq = next_seq.wrapping_add(1);

                if received_length >= total {
                    break;
                }
            }
        }

        let total = total_length.unwrap_or(0);
        let mut payload_buffer = Vec::with_capacity(total);
        for chunk in chunks {
            payload_buffer.extend_from_slice(&chunk);
        }
        payload_buffer.truncate(total);

        let command =
            expected_command.ok_or_else(|| anyhow!("没有收到有效的 CTAPHID 初始化响应。"))?;
        if command == CTAPHID_ERROR {
            let error_code = payload_buffer.first().copied().unwrap_or(0xff);
            bail!(
                "CTAPHID 错误 {} (0x{error_code:02x})",
                ctaphid_error_name(error_code)
            );
        }

        Ok(ResponseMessage {
            command,
            payload: payload_buffer,
        })
    }
}

fn pick_debug_candidate(api: &HidApi) -> Result<DeviceCandidate> {
    let mut candidates = Vec::new();

    for info in api.device_list() {
        if info.vendor_id() != DEFAULT_VENDOR_ID || info.product_id() != DEFAULT_PRODUCT_ID {
            continue;
        }

        let usage_page = info.usage_page();
        let usage = info.usage();
        let interface_number = info.interface_number();
        let score = if usage_page == DEBUG_USAGE_PAGE && usage == DEBUG_USAGE {
            0
        } else if usage_page == FIDO_USAGE_PAGE && usage == FIDO_USAGE {
            100
        } else if interface_number == 1 {
            10
        } else {
            50
        };

        candidates.push(DeviceCandidate {
            path: info.path().to_owned(),
            product_name: info
                .product_string()
                .unwrap_or("MeowKey Debug HID")
                .to_string(),
            vendor_id: info.vendor_id(),
            product_id: info.product_id(),
            usage_page,
            usage,
            interface_number,
            score,
        });
    }

    candidates.sort_by_key(|candidate| candidate.score);
    candidates
        .into_iter()
        .find(|candidate| candidate.score < 100)
        .ok_or_else(|| anyhow!("没有找到已连接的 MeowKey 调试 HID 接口。"))
}

fn parse_packet(raw: [u8; PACKET_SIZE]) -> Result<ParsedPacket> {
    let is_init = (raw[4] & 0x80) != 0;
    Ok(ParsedPacket {
        cid: u32::from_be_bytes(raw[0..4].try_into()?),
        command: is_init.then_some(raw[4] & 0x7f),
        sequence: (!is_init).then_some(raw[4] & 0x7f),
        payload_length: is_init.then_some(u16::from_be_bytes(raw[5..7].try_into()?) as usize),
        payload: if is_init {
            raw[7..].to_vec()
        } else {
            raw[5..].to_vec()
        },
        raw,
        is_init,
    })
}

fn write_packet(device: &HidDevice, packet: &[u8; PACKET_SIZE]) -> Result<()> {
    let mut report = [0u8; PACKET_SIZE + 1];
    report[1..].copy_from_slice(packet);
    let written = device.write(&report)?;
    if written == 0 {
        bail!("HID 写入失败，设备没有接受任何字节。");
    }
    Ok(())
}

fn read_packet(device: &HidDevice, timeout_ms: i32) -> Result<[u8; PACKET_SIZE]> {
    let mut buffer = [0u8; PACKET_SIZE + 1];
    let length = device.read_timeout(&mut buffer, timeout_ms)?;

    if length == 0 {
        bail!("等待 CTAPHID 响应超时。");
    }
    if length == PACKET_SIZE {
        return Ok(buffer[..PACKET_SIZE].try_into().expect("exact packet size"));
    }
    if length == (PACKET_SIZE + 1) && buffer[0] == 0 {
        return Ok(buffer[1..].try_into().expect("report ID stripped"));
    }

    bail!("收到意外的 HID 报文长度: {length}");
}

fn make_init_packet(
    cid: u32,
    command: u8,
    total_length: usize,
    payload: &[u8],
) -> [u8; PACKET_SIZE] {
    let mut packet = [0u8; PACKET_SIZE];
    packet[..4].copy_from_slice(&cid.to_be_bytes());
    packet[4] = 0x80 | command;
    packet[5..7].copy_from_slice(&(total_length as u16).to_be_bytes());
    packet[7..(7 + payload.len())].copy_from_slice(payload);
    packet
}

fn make_continuation_packet(cid: u32, sequence: u8, payload: &[u8]) -> [u8; PACKET_SIZE] {
    let mut packet = [0u8; PACKET_SIZE];
    packet[..4].copy_from_slice(&cid.to_be_bytes());
    packet[4] = sequence & 0x7f;
    packet[5..(5 + payload.len())].copy_from_slice(payload);
    packet
}

fn extract_credential_id_from_make_credential(decoded: &CborValue) -> Option<&[u8]> {
    let auth_data = decoded.get_int_key(2)?.as_bytes()?;
    if auth_data.len() < 55 {
        return None;
    }
    let credential_len = u16::from_be_bytes(auth_data[53..55].try_into().ok()?) as usize;
    let end = 55 + credential_len;
    auth_data.get(55..end)
}

fn extract_credential_id_from_assertion(decoded: &CborValue) -> Option<&[u8]> {
    decoded.get_int_key(1)?.get_text_key("id")?.as_bytes()
}

fn extract_sign_count_from_assertion(decoded: &CborValue) -> Option<u32> {
    let auth_data = decoded.get_int_key(2)?.as_bytes()?;
    if auth_data.len() < 37 {
        return None;
    }
    Some(u32::from_be_bytes(auth_data[33..37].try_into().ok()?))
}

fn extract_text_array(value: Option<&CborValue>) -> Vec<String> {
    value
        .and_then(CborValue::as_array)
        .map(|entries| {
            entries
                .iter()
                .filter_map(CborValue::as_text)
                .map(str::to_string)
                .collect()
        })
        .unwrap_or_default()
}

fn extract_options(value: Option<&CborValue>) -> Vec<String> {
    let Some(CborValue::Map(entries)) = value else {
        return Vec::new();
    };

    entries
        .iter()
        .filter_map(|(key, value)| Some((key.as_text()?, value.as_bool()?)))
        .map(|(key, value)| format!("{key}={value}"))
        .collect()
}

fn decode_capabilities(mask: u8) -> Vec<String> {
    let mut values = Vec::new();
    if (mask & 0x04) != 0 {
        values.push("支持 CBOR".to_string());
    }
    if (mask & 0x08) != 0 {
        values.push("无旧版 MSG".to_string());
    }
    values
}

fn read_status_byte(payload: &[u8], command_name: &str) -> Result<u8> {
    payload
        .first()
        .copied()
        .ok_or_else(|| anyhow!("{command_name} 返回了空的 CTAP 响应负载。"))
}

fn random_bytes(length: usize) -> Result<Vec<u8>> {
    let mut output = vec![0u8; length];
    getrandom(&mut output).map_err(|error| anyhow!("生成随机字节失败: {error}"))?;
    Ok(output)
}

fn normalize_hex(input: &str) -> Result<String> {
    let value = input
        .chars()
        .filter(|character| !character.is_whitespace())
        .collect::<String>()
        .to_lowercase();
    if value.is_empty() {
        return Ok(String::new());
    }
    if (value.len() % 2) != 0 || !value.chars().all(|character| character.is_ascii_hexdigit()) {
        bail!("十六进制字符串格式不正确");
    }
    Ok(value)
}

fn hex_to_bytes(text: &str) -> Result<Vec<u8>> {
    let normalized = normalize_hex(text)?;
    if normalized.is_empty() {
        return Ok(Vec::new());
    }

    let mut bytes = Vec::with_capacity(normalized.len() / 2);
    for chunk in normalized.as_bytes().chunks(2) {
        let byte = u8::from_str_radix(std::str::from_utf8(chunk)?, 16)?;
        bytes.push(byte);
    }
    Ok(bytes)
}

fn parse_hex_lines(text: &str) -> Result<Vec<Vec<u8>>> {
    text.lines()
        .map(str::trim)
        .filter(|line| !line.is_empty())
        .map(hex_to_bytes)
        .collect()
}

fn bytes_to_hex_compact(bytes: &[u8]) -> String {
    bytes.iter().map(|value| format!("{value:02x}")).collect()
}

fn format_hex(bytes: &[u8]) -> String {
    bytes
        .iter()
        .map(|value| format!("{value:02x}"))
        .collect::<Vec<_>>()
        .join(" ")
}

fn format_device_id(vendor_id: u16, product_id: u16) -> String {
    format!("0x{vendor_id:04x}:0x{product_id:04x}")
}

fn command_name(command: u8) -> &'static str {
    match command {
        CTAPHID_PING => "PING",
        CTAPHID_INIT => "INIT",
        CTAPHID_CBOR => "CBOR",
        CTAPHID_DIAG => "DIAG",
        CTAPHID_ERROR => "ERROR",
        _ => "UNKNOWN",
    }
}

fn ctaphid_error_name(error_code: u8) -> &'static str {
    match error_code {
        0x01 => "INVALID_CMD",
        0x03 => "INVALID_LEN",
        0x04 => "INVALID_SEQ",
        0x0b => "INVALID_CID",
        _ => "UNKNOWN_ERROR",
    }
}

fn ctap_status_name(status: u8) -> String {
    match status {
        CTAP2_STATUS_OK => "OK".to_string(),
        CTAP2_ERR_INVALID_CBOR => "INVALID_CBOR".to_string(),
        CTAP2_ERR_MISSING_PARAMETER => "MISSING_PARAMETER".to_string(),
        CTAP2_ERR_CREDENTIAL_EXCLUDED => "CREDENTIAL_EXCLUDED".to_string(),
        CTAP2_ERR_INVALID_CREDENTIAL => "INVALID_CREDENTIAL".to_string(),
        CTAP2_ERR_UNSUPPORTED_ALGORITHM => "UNSUPPORTED_ALGORITHM".to_string(),
        CTAP2_ERR_KEY_STORE_FULL => "KEY_STORE_FULL".to_string(),
        CTAP2_ERR_UNSUPPORTED_OPTION => "UNSUPPORTED_OPTION".to_string(),
        CTAP2_ERR_NO_CREDENTIALS => "NO_CREDENTIALS".to_string(),
        _ => format!("0x{status:02x}"),
    }
}
