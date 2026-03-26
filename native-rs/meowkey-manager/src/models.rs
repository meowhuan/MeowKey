use serde::Serialize;
use serde_json::Value;

#[derive(Clone, Debug, Default)]
pub struct SessionSnapshot {
    pub backend_name: String,
    pub device_name: String,
    pub device_id: String,
    pub channel_id: String,
    pub capabilities: Vec<String>,
    pub state_label: String,
}

#[derive(Clone, Debug, Default, Serialize)]
pub struct InitDeviceVersion {
    pub major: u8,
    pub minor: u8,
    pub build: u8,
}

#[derive(Clone, Debug, Default, Serialize)]
pub struct InitSnapshot {
    pub nonce: String,
    pub channel_id: String,
    pub ctaphid_version: u8,
    pub device_version: InitDeviceVersion,
    pub capabilities: Vec<String>,
}

#[derive(Clone, Debug, Default)]
pub struct RegisterForm {
    pub rp_id: String,
    pub user_id: String,
    pub user_name: String,
    pub display_name: String,
    pub exclude_list: String,
}

#[derive(Clone, Debug, Default)]
pub struct AssertionForm {
    pub rp_id: String,
    pub credential_id: String,
}

#[derive(Clone, Debug, Default, Serialize)]
pub struct CredentialRecord {
    pub credential_id_hex: String,
    pub rp_id: String,
    pub user_id_hex: String,
    pub user_name: String,
    pub display_name: String,
    pub sign_count: u32,
    pub discoverable: bool,
}

#[derive(Clone, Debug, Default, Serialize)]
pub struct InfoSnapshot {
    pub status: u8,
    pub status_name: String,
    pub data: Value,
    pub versions: Vec<String>,
    pub capabilities: Vec<String>,
    pub options: Vec<String>,
}

#[derive(Clone, Debug, Default, Serialize)]
pub struct MakeCredentialRequestSnapshot {
    pub rp_id: String,
    pub user_id: String,
    pub user_name: String,
    pub display_name: String,
    pub exclude_list: Vec<String>,
}

#[derive(Clone, Debug, Default, Serialize)]
pub struct MakeCredentialResult {
    pub status: u8,
    pub status_name: String,
    pub client_data_hash: String,
    pub request: MakeCredentialRequestSnapshot,
    pub credential_id_hex: Option<String>,
    pub response: Value,
}

#[derive(Clone, Debug, Default, Serialize)]
pub struct AssertionRequestSnapshot {
    pub rp_id: String,
    pub credential_id: Option<String>,
}

#[derive(Clone, Debug, Default, Serialize)]
pub struct AssertionResult {
    pub status: u8,
    pub status_name: String,
    pub client_data_hash: String,
    pub request: AssertionRequestSnapshot,
    pub credential_id_hex: Option<String>,
    pub sign_count: u32,
    pub user_name: Option<String>,
    pub display_name: Option<String>,
    pub response: Value,
}

#[derive(Clone, Debug)]
pub struct LogEntry {
    pub timestamp: String,
    pub tag: String,
    pub body: String,
}
