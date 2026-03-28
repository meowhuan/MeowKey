use std::{
    env, fs,
    path::PathBuf,
    time::{SystemTime, UNIX_EPOCH},
};

use anyhow::{Context, Result, anyhow, bail};
use semver::Version;
use serde::{Deserialize, Serialize};

pub const DEFAULT_RELEASES_URL: &str = "https://github.com/meowhuan/MeowKey/releases";

#[derive(Clone, Copy, Debug, Serialize)]
pub enum UpdateState {
    Unknown,
    UpToDate,
    UpdateAvailable,
    NotConfigured,
    NoDevice,
}

#[derive(Clone, Debug, Serialize)]
pub struct UpdateTargetResult {
    pub state: UpdateState,
    pub channel: String,
    pub current_version: String,
    pub latest_version: String,
    pub download_url: String,
    pub notes_url: String,
}

#[derive(Clone, Debug, Serialize)]
pub struct UpdateCheckResult {
    pub releases_url: String,
    pub checked_at_unix_ms: u128,
    pub manager_winui: UpdateTargetResult,
    pub firmware: UpdateTargetResult,
}

#[derive(Clone, Debug)]
pub struct UpdateCheckRequest {
    pub releases_url: String,
    pub app_track: String,
    pub firmware_track: String,
    pub current_app_version: String,
    pub current_firmware_version: Option<String>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct UpdatePreferences {
    pub releases_url: String,
    pub app_track: String,
    pub firmware_track: String,
}

impl Default for UpdatePreferences {
    fn default() -> Self {
        Self {
            releases_url: DEFAULT_RELEASES_URL.to_string(),
            app_track: "stable".to_string(),
            firmware_track: "stable".to_string(),
        }
    }
}

pub fn normalize_track(track: &str) -> String {
    if track.eq_ignore_ascii_case("preview") {
        "preview".to_string()
    } else {
        "stable".to_string()
    }
}

pub fn load_update_preferences() -> UpdatePreferences {
    let path = preferences_path();
    if !path.exists() {
        return UpdatePreferences::default();
    }

    let raw = match fs::read_to_string(&path) {
        Ok(raw) => raw,
        Err(_) => return UpdatePreferences::default(),
    };
    let mut parsed = match serde_json::from_str::<UpdatePreferences>(&raw) {
        Ok(parsed) => parsed,
        Err(_) => return UpdatePreferences::default(),
    };

    if parsed.releases_url.trim().is_empty() {
        parsed.releases_url = DEFAULT_RELEASES_URL.to_string();
    } else {
        parsed.releases_url = parsed.releases_url.trim().to_string();
    }
    parsed.app_track = normalize_track(&parsed.app_track);
    parsed.firmware_track = normalize_track(&parsed.firmware_track);
    parsed
}

pub fn save_update_preferences(preferences: &UpdatePreferences) -> Result<()> {
    let mut normalized = preferences.clone();
    if normalized.releases_url.trim().is_empty() {
        normalized.releases_url = DEFAULT_RELEASES_URL.to_string();
    } else {
        normalized.releases_url = normalized.releases_url.trim().to_string();
    }
    normalized.app_track = normalize_track(&normalized.app_track);
    normalized.firmware_track = normalize_track(&normalized.firmware_track);

    let path = preferences_path();
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent).context("创建更新偏好目录失败")?;
    }

    let raw = serde_json::to_string_pretty(&normalized).context("序列化更新偏好失败")?;
    fs::write(&path, raw).with_context(|| format!("写入更新偏好失败: {}", path.display()))?;
    Ok(())
}

pub fn check_updates(request: &UpdateCheckRequest) -> Result<UpdateCheckResult> {
    let releases_url = if request.releases_url.trim().is_empty() {
        DEFAULT_RELEASES_URL.to_string()
    } else {
        request.releases_url.trim().to_string()
    };

    let (owner, repo) = parse_repository(&releases_url)?;
    let releases = fetch_releases(&owner, &repo)?;
    let app_track = normalize_track(&request.app_track);
    let firmware_track = normalize_track(&request.firmware_track);

    let manager_release = find_release_with_asset(&releases, &app_track, |name| {
        name.contains("-manager-winui.zip")
    });
    let firmware_release = find_firmware_release(&releases, &firmware_track);

    Ok(UpdateCheckResult {
        releases_url: format!("https://github.com/{owner}/{repo}/releases"),
        checked_at_unix_ms: unix_time_ms(),
        manager_winui: build_target_result(
            &app_track,
            request.current_app_version.trim(),
            manager_release,
            false,
        ),
        firmware: build_target_result(
            &firmware_track,
            request
                .current_firmware_version
                .as_deref()
                .unwrap_or("")
                .trim(),
            firmware_release,
            true,
        ),
    })
}

#[derive(Clone)]
struct ReleaseAssetRef {
    tag_name: String,
    html_url: String,
    browser_download_url: String,
}

fn find_firmware_release(releases: &[GitHubRelease], track: &str) -> Option<ReleaseAssetRef> {
    let preferred = find_release_with_asset(releases, track, |name| {
        name.contains("-generic-hardened.zip")
    });
    if preferred.is_some() {
        return preferred;
    }

    find_release_with_asset(releases, track, |name| {
        name.starts_with("meowkey-")
            && name.ends_with(".zip")
            && name.contains("-generic-")
            && !name.contains("-manager-winui")
            && !name.contains("-manager-rust-linux")
    })
}

fn find_release_with_asset(
    releases: &[GitHubRelease],
    track: &str,
    predicate: impl Fn(&str) -> bool,
) -> Option<ReleaseAssetRef> {
    let preview = track.eq_ignore_ascii_case("preview");

    for release in releases {
        if release.draft || release.prerelease != preview {
            continue;
        }
        let asset = release
            .assets
            .iter()
            .find(|asset| predicate(asset.name.as_str()));
        let Some(asset) = asset else {
            continue;
        };
        return Some(ReleaseAssetRef {
            tag_name: release.tag_name.clone(),
            html_url: release.html_url.clone(),
            browser_download_url: asset.browser_download_url.clone(),
        });
    }

    None
}

fn build_target_result(
    channel: &str,
    current_version: &str,
    release: Option<ReleaseAssetRef>,
    allow_no_device: bool,
) -> UpdateTargetResult {
    if allow_no_device && current_version.is_empty() {
        return UpdateTargetResult {
            state: UpdateState::NoDevice,
            channel: channel.to_string(),
            current_version: String::new(),
            latest_version: String::new(),
            download_url: String::new(),
            notes_url: String::new(),
        };
    }

    let Some(release) = release else {
        return UpdateTargetResult {
            state: UpdateState::NotConfigured,
            channel: channel.to_string(),
            current_version: current_version.to_string(),
            latest_version: String::new(),
            download_url: String::new(),
            notes_url: String::new(),
        };
    };

    let latest = normalize_version(&release.tag_name);
    let compare = compare_versions(current_version, latest.as_str());
    let state = if compare == i32::MIN {
        if normalize_version(current_version).eq_ignore_ascii_case(&latest) {
            UpdateState::UpToDate
        } else {
            UpdateState::Unknown
        }
    } else if compare < 0 {
        UpdateState::UpdateAvailable
    } else {
        UpdateState::UpToDate
    };

    UpdateTargetResult {
        state,
        channel: channel.to_string(),
        current_version: current_version.to_string(),
        latest_version: latest,
        download_url: release.browser_download_url,
        notes_url: release.html_url,
    }
}

fn compare_versions(current: &str, latest: &str) -> i32 {
    let current = normalize_version(current);
    let latest = normalize_version(latest);
    let current = match Version::parse(&current) {
        Ok(value) => value,
        Err(_) => return i32::MIN,
    };
    let latest = match Version::parse(&latest) {
        Ok(value) => value,
        Err(_) => return i32::MIN,
    };

    if current < latest {
        -1
    } else if current > latest {
        1
    } else {
        0
    }
}

fn normalize_version(value: &str) -> String {
    let raw = value.trim();
    if raw.starts_with('v') || raw.starts_with('V') {
        raw[1..].to_string()
    } else {
        raw.to_string()
    }
}

fn fetch_releases(owner: &str, repo: &str) -> Result<Vec<GitHubRelease>> {
    let api_url = format!("https://api.github.com/repos/{owner}/{repo}/releases?per_page=100");
    let response = ureq::get(&api_url)
        .set("User-Agent", "MeowKey-Manager/1.0")
        .set("Accept", "application/vnd.github+json")
        .call()
        .map_err(|error| anyhow!("请求 GitHub Releases 失败: {error}"))?;
    let reader = response.into_reader();
    serde_json::from_reader(reader).context("解析 GitHub Releases 响应失败")
}

fn parse_repository(source: &str) -> Result<(String, String)> {
    let source = source.trim();
    if source.is_empty() {
        bail!("发行版来源地址为空。");
    }

    if let Some((owner, repo)) = parse_owner_repo_literal(source) {
        return Ok((owner, repo));
    }

    let url = url::Url::parse(source).context("发行版来源 URL 格式不正确")?;
    if !url
        .host_str()
        .unwrap_or_default()
        .eq_ignore_ascii_case("github.com")
    {
        bail!("当前仅支持 github.com 发行版地址。");
    }

    let parts = url
        .path_segments()
        .map(|segments| segments.collect::<Vec<_>>())
        .unwrap_or_default();
    if parts.len() < 2 {
        bail!("发行版地址必须包含 owner/repo。");
    }

    let owner = parts[0].trim().to_string();
    let mut repo = parts[1].trim().to_string();
    if repo.ends_with(".git") {
        repo.truncate(repo.len().saturating_sub(4));
    }
    if owner.is_empty() || repo.is_empty() {
        bail!("发行版地址中的 owner/repo 无效。");
    }
    Ok((owner, repo))
}

fn parse_owner_repo_literal(raw: &str) -> Option<(String, String)> {
    let mut parts = raw.split('/').filter(|part| !part.trim().is_empty());
    let owner = parts.next()?.trim().to_string();
    let mut repo = parts.next()?.trim().to_string();
    if parts.next().is_some() {
        return None;
    }
    if repo.ends_with(".git") {
        repo.truncate(repo.len().saturating_sub(4));
    }
    if owner.is_empty() || repo.is_empty() {
        return None;
    }
    Some((owner, repo))
}

fn unix_time_ms() -> u128 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|duration| duration.as_millis())
        .unwrap_or(0)
}

fn preferences_path() -> PathBuf {
    if let Ok(local_app_data) = env::var("LOCALAPPDATA") {
        return PathBuf::from(local_app_data)
            .join("MeowKey")
            .join("Manager")
            .join("update-preferences.json");
    }

    if let Ok(xdg_config_home) = env::var("XDG_CONFIG_HOME") {
        return PathBuf::from(xdg_config_home)
            .join("meowkey-manager")
            .join("update-preferences.json");
    }

    if let Ok(home) = env::var("HOME") {
        return PathBuf::from(home)
            .join(".config")
            .join("meowkey-manager")
            .join("update-preferences.json");
    }

    PathBuf::from(".meowkey-manager-update-preferences.json")
}

#[derive(Debug, Deserialize)]
struct GitHubRelease {
    #[serde(default)]
    tag_name: String,
    #[serde(default)]
    prerelease: bool,
    #[serde(default)]
    draft: bool,
    #[serde(default)]
    html_url: String,
    #[serde(default)]
    assets: Vec<GitHubAsset>,
}

#[derive(Debug, Deserialize)]
struct GitHubAsset {
    #[serde(default)]
    name: String,
    #[serde(default)]
    browser_download_url: String,
}
