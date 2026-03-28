using System.Net.Http;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;

namespace MeowKey.Manager.Services;

public enum UpdateState
{
    Unknown = 0,
    UpToDate,
    UpdateAvailable,
    NotConfigured,
    NoDevice
}

public sealed class UpdateTargetResult
{
    public UpdateState State { get; init; }
    public string Channel { get; init; } = "stable";
    public string CurrentVersion { get; init; } = string.Empty;
    public string LatestVersion { get; init; } = string.Empty;
    public string DownloadUrl { get; init; } = string.Empty;
    public string NotesUrl { get; init; } = string.Empty;
}

public sealed class UpdateCheckResult
{
    public string ManifestUrl { get; init; } = string.Empty;
    public string GeneratedAt { get; init; } = string.Empty;
    public UpdateTargetResult ManagerWinui { get; init; } = new();
    public UpdateTargetResult Firmware { get; init; } = new();
}

public sealed class UpdateCheckRequest
{
    public string ManifestUrl { get; init; } = string.Empty;
    public string AppTrack { get; init; } = "stable";
    public string FirmwareTrack { get; init; } = "stable";
    public string CurrentAppVersion { get; init; } = string.Empty;
    public string CurrentFirmwareVersion { get; init; } = string.Empty;
}

public sealed class UpdatePreferences
{
    public string ManifestUrl { get; set; } = UpdatePreferencesStore.DefaultManifestUrl;
    public string AppTrack { get; set; } = "stable";
    public string FirmwareTrack { get; set; } = "stable";
}

public sealed class UpdatePreferencesStore
{
    public const string DefaultManifestUrl = "https://github.com/meowhuan/MeowKey/releases";

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNameCaseInsensitive = true,
        WriteIndented = true
    };

    private static string PreferencesPath =>
        Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "MeowKey",
            "Manager",
            "update-preferences.json");

    public UpdatePreferences Load()
    {
        try
        {
            if (!File.Exists(PreferencesPath))
            {
                return new UpdatePreferences();
            }

            var raw = File.ReadAllText(PreferencesPath);
            var parsed = JsonSerializer.Deserialize<UpdatePreferences>(raw, JsonOptions);
            if (parsed is null)
            {
                return new UpdatePreferences();
            }

            parsed.ManifestUrl = string.IsNullOrWhiteSpace(parsed.ManifestUrl)
                ? DefaultManifestUrl
                : parsed.ManifestUrl.Trim();
            parsed.AppTrack = NormalizeTrack(parsed.AppTrack);
            parsed.FirmwareTrack = NormalizeTrack(parsed.FirmwareTrack);
            return parsed;
        }
        catch
        {
            return new UpdatePreferences();
        }
    }

    public void Save(UpdatePreferences preferences)
    {
        if (preferences is null)
        {
            return;
        }

        preferences.ManifestUrl = string.IsNullOrWhiteSpace(preferences.ManifestUrl)
            ? DefaultManifestUrl
            : preferences.ManifestUrl.Trim();
        preferences.AppTrack = NormalizeTrack(preferences.AppTrack);
        preferences.FirmwareTrack = NormalizeTrack(preferences.FirmwareTrack);

        var directory = Path.GetDirectoryName(PreferencesPath);
        if (!string.IsNullOrWhiteSpace(directory))
        {
            Directory.CreateDirectory(directory);
        }

        var raw = JsonSerializer.Serialize(preferences, JsonOptions);
        File.WriteAllText(PreferencesPath, raw);
    }

    public static string NormalizeTrack(string? track)
    {
        return string.Equals(track, "preview", StringComparison.OrdinalIgnoreCase)
            ? "preview"
            : "stable";
    }
}

public sealed class ReleaseUpdateService
{
    private const string GitHubApiBase = "https://api.github.com";
    private static readonly HttpClient HttpClient = CreateHttpClient();

    private static readonly Regex VersionRegex = new(
        @"^v?(?<major>\d+)\.(?<minor>\d+)\.(?<patch>\d+)(?:-(?<pre>[0-9A-Za-z.-]+))?$",
        RegexOptions.Compiled);

    public async Task<UpdateCheckResult> CheckAsync(UpdateCheckRequest request, CancellationToken cancellationToken = default)
    {
        if (request is null)
        {
            throw new ArgumentNullException(nameof(request));
        }

        var sourceUrl = string.IsNullOrWhiteSpace(request.ManifestUrl)
            ? UpdatePreferencesStore.DefaultManifestUrl
            : request.ManifestUrl.Trim();
        var repo = ParseRepository(sourceUrl);
        var releases = await FetchReleases(repo, cancellationToken);

        var appTrack = UpdatePreferencesStore.NormalizeTrack(request.AppTrack);
        var firmwareTrack = UpdatePreferencesStore.NormalizeTrack(request.FirmwareTrack);

        var managerRelease = FindReleaseWithAsset(
            releases,
            appTrack,
            assetName => assetName.Contains("-manager-winui.zip", StringComparison.OrdinalIgnoreCase));
        var firmwareRelease = FindFirmwareRelease(
            releases,
            firmwareTrack);

        return new UpdateCheckResult
        {
            ManifestUrl = $"https://github.com/{repo.Owner}/{repo.Repo}/releases",
            GeneratedAt = DateTimeOffset.UtcNow.ToString("O"),
            ManagerWinui = BuildTargetResult(
                appTrack,
                request.CurrentAppVersion,
                managerRelease,
                allowNoDevice: false),
            Firmware = BuildTargetResult(
                firmwareTrack,
                request.CurrentFirmwareVersion,
                firmwareRelease,
                allowNoDevice: true)
        };
    }

    private static HttpClient CreateHttpClient()
    {
        var client = new HttpClient
        {
            Timeout = TimeSpan.FromSeconds(20)
        };
        client.DefaultRequestHeaders.UserAgent.ParseAdd("MeowKey-Manager/1.0");
        client.DefaultRequestHeaders.Accept.ParseAdd("application/vnd.github+json");
        return client;
    }

    private static async Task<IReadOnlyList<GitHubReleaseDocument>> FetchReleases(
        GitHubRepository repo,
        CancellationToken cancellationToken)
    {
        var apiUrl = $"{GitHubApiBase}/repos/{repo.Owner}/{repo.Repo}/releases?per_page=100";
        using var httpRequest = new HttpRequestMessage(HttpMethod.Get, apiUrl);
        using var response = await HttpClient.SendAsync(httpRequest, cancellationToken);
        response.EnsureSuccessStatusCode();
        await using var stream = await response.Content.ReadAsStreamAsync(cancellationToken);
        var releases = await JsonSerializer.DeserializeAsync<List<GitHubReleaseDocument>>(stream, cancellationToken: cancellationToken);
        if (releases is null)
        {
            return Array.Empty<GitHubReleaseDocument>();
        }

        return releases;
    }

    private static GitHubReleaseAssetRef? FindFirmwareRelease(
        IReadOnlyList<GitHubReleaseDocument> releases,
        string track)
    {
        var preferred = FindReleaseWithAsset(
            releases,
            track,
            assetName => assetName.Contains("-generic-hardened.zip", StringComparison.OrdinalIgnoreCase));
        if (preferred is not null)
        {
            return preferred;
        }

        return FindReleaseWithAsset(
            releases,
            track,
            assetName =>
                assetName.StartsWith("meowkey-", StringComparison.OrdinalIgnoreCase) &&
                assetName.EndsWith(".zip", StringComparison.OrdinalIgnoreCase) &&
                assetName.Contains("-generic-", StringComparison.OrdinalIgnoreCase) &&
                !assetName.Contains("-manager-winui", StringComparison.OrdinalIgnoreCase) &&
                !assetName.Contains("-manager-rust-linux", StringComparison.OrdinalIgnoreCase));
    }

    private static GitHubReleaseAssetRef? FindReleaseWithAsset(
        IReadOnlyList<GitHubReleaseDocument> releases,
        string track,
        Func<string, bool> assetPredicate)
    {
        var requirePreview = string.Equals(track, "preview", StringComparison.OrdinalIgnoreCase);

        foreach (var release in releases)
        {
            if (release.Draft)
            {
                continue;
            }
            if (release.Prerelease != requirePreview)
            {
                continue;
            }

            var asset = release.Assets?.FirstOrDefault(item =>
                !string.IsNullOrWhiteSpace(item.Name) &&
                assetPredicate(item.Name!));
            if (asset is null)
            {
                continue;
            }

            return new GitHubReleaseAssetRef
            {
                TagName = release.TagName ?? string.Empty,
                HtmlUrl = release.HtmlUrl ?? string.Empty,
                AssetName = asset.Name ?? string.Empty,
                AssetDownloadUrl = asset.BrowserDownloadUrl ?? string.Empty
            };
        }

        return null;
    }

    private static UpdateTargetResult BuildTargetResult(
        string channel,
        string? currentVersion,
        GitHubReleaseAssetRef? releaseAsset,
        bool allowNoDevice)
    {
        var normalizedCurrent = (currentVersion ?? string.Empty).Trim();
        if (allowNoDevice && string.IsNullOrWhiteSpace(normalizedCurrent))
        {
            return new UpdateTargetResult
            {
                Channel = channel,
                State = UpdateState.NoDevice
            };
        }

        if (releaseAsset is null || string.IsNullOrWhiteSpace(releaseAsset.TagName))
        {
            return new UpdateTargetResult
            {
                Channel = channel,
                CurrentVersion = normalizedCurrent,
                State = UpdateState.NotConfigured
            };
        }

        var latest = NormalizeVersion(releaseAsset.TagName);
        var compare = CompareVersion(normalizedCurrent, latest);
        var state = compare < 0 ? UpdateState.UpdateAvailable : UpdateState.UpToDate;
        if (compare == int.MinValue)
        {
            state = string.Equals(NormalizeVersion(normalizedCurrent), latest, StringComparison.OrdinalIgnoreCase)
                ? UpdateState.UpToDate
                : UpdateState.Unknown;
        }

        return new UpdateTargetResult
        {
            Channel = channel,
            CurrentVersion = normalizedCurrent,
            LatestVersion = latest,
            DownloadUrl = (releaseAsset.AssetDownloadUrl ?? string.Empty).Trim(),
            NotesUrl = (releaseAsset.HtmlUrl ?? string.Empty).Trim(),
            State = state
        };
    }

    private static string NormalizeVersion(string value)
    {
        var normalized = (value ?? string.Empty).Trim();
        return normalized.StartsWith("v", StringComparison.OrdinalIgnoreCase)
            ? normalized[1..]
            : normalized;
    }

    private static int CompareVersion(string current, string latest)
    {
        if (!TryParseVersion(current, out var left) || !TryParseVersion(latest, out var right))
        {
            return int.MinValue;
        }

        var major = left.Major.CompareTo(right.Major);
        if (major != 0)
        {
            return major;
        }

        var minor = left.Minor.CompareTo(right.Minor);
        if (minor != 0)
        {
            return minor;
        }

        var patch = left.Patch.CompareTo(right.Patch);
        if (patch != 0)
        {
            return patch;
        }

        var leftRelease = string.IsNullOrWhiteSpace(left.PreRelease);
        var rightRelease = string.IsNullOrWhiteSpace(right.PreRelease);
        if (leftRelease && rightRelease)
        {
            return 0;
        }
        if (leftRelease)
        {
            return 1;
        }
        if (rightRelease)
        {
            return -1;
        }

        return ComparePreRelease(left.PreRelease ?? string.Empty, right.PreRelease ?? string.Empty);
    }

    private static int ComparePreRelease(string left, string right)
    {
        var leftParts = left.Split('.', StringSplitOptions.RemoveEmptyEntries);
        var rightParts = right.Split('.', StringSplitOptions.RemoveEmptyEntries);
        var count = Math.Max(leftParts.Length, rightParts.Length);
        for (var i = 0; i < count; i++)
        {
            var leftPart = i < leftParts.Length ? leftParts[i] : string.Empty;
            var rightPart = i < rightParts.Length ? rightParts[i] : string.Empty;
            if (leftPart == rightPart)
            {
                continue;
            }

            var leftNumeric = int.TryParse(leftPart, out var leftNumber);
            var rightNumeric = int.TryParse(rightPart, out var rightNumber);
            if (leftNumeric && rightNumeric)
            {
                return leftNumber.CompareTo(rightNumber);
            }
            if (leftNumeric)
            {
                return -1;
            }
            if (rightNumeric)
            {
                return 1;
            }

            return string.Compare(leftPart, rightPart, StringComparison.OrdinalIgnoreCase);
        }

        return 0;
    }

    private static bool TryParseVersion(string raw, out ParsedVersion parsed)
    {
        parsed = default;
        if (string.IsNullOrWhiteSpace(raw))
        {
            return false;
        }

        var match = VersionRegex.Match(NormalizeVersion(raw));
        if (!match.Success)
        {
            return false;
        }

        parsed = new ParsedVersion
        {
            Major = int.Parse(match.Groups["major"].Value),
            Minor = int.Parse(match.Groups["minor"].Value),
            Patch = int.Parse(match.Groups["patch"].Value),
            PreRelease = match.Groups["pre"].Value
        };
        return true;
    }

    private static GitHubRepository ParseRepository(string source)
    {
        var raw = (source ?? string.Empty).Trim();
        if (string.IsNullOrWhiteSpace(raw))
        {
            throw new InvalidOperationException("Release source URL is empty.");
        }

        if (TryParseOwnerRepo(raw, out var owner, out var repo))
        {
            return new GitHubRepository(owner, repo);
        }

        if (!Uri.TryCreate(raw, UriKind.Absolute, out var uri))
        {
            throw new InvalidOperationException("Release source URL is invalid.");
        }

        if (!string.Equals(uri.Host, "github.com", StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidOperationException("Only github.com release sources are supported.");
        }

        var segments = uri.AbsolutePath.Trim('/').Split('/', StringSplitOptions.RemoveEmptyEntries);
        if (segments.Length < 2)
        {
            throw new InvalidOperationException("Release source URL must include owner and repository.");
        }

        owner = segments[0];
        repo = segments[1];
        if (repo.EndsWith(".git", StringComparison.OrdinalIgnoreCase))
        {
            repo = repo[..^4];
        }

        return new GitHubRepository(owner, repo);
    }

    private static bool TryParseOwnerRepo(string raw, out string owner, out string repo)
    {
        owner = string.Empty;
        repo = string.Empty;
        var parts = raw.Split('/', StringSplitOptions.RemoveEmptyEntries);
        if (parts.Length != 2)
        {
            return false;
        }

        owner = parts[0].Trim();
        repo = parts[1].Trim();
        if (repo.EndsWith(".git", StringComparison.OrdinalIgnoreCase))
        {
            repo = repo[..^4];
        }

        return !string.IsNullOrWhiteSpace(owner) && !string.IsNullOrWhiteSpace(repo);
    }

    private sealed record GitHubRepository(string Owner, string Repo);

    private sealed class GitHubReleaseDocument
    {
        [JsonPropertyName("tag_name")]
        public string? TagName { get; set; }

        [JsonPropertyName("prerelease")]
        public bool Prerelease { get; set; }

        [JsonPropertyName("draft")]
        public bool Draft { get; set; }

        [JsonPropertyName("html_url")]
        public string? HtmlUrl { get; set; }

        [JsonPropertyName("assets")]
        public List<GitHubReleaseAssetDocument>? Assets { get; set; }
    }

    private sealed class GitHubReleaseAssetDocument
    {
        [JsonPropertyName("name")]
        public string? Name { get; set; }

        [JsonPropertyName("browser_download_url")]
        public string? BrowserDownloadUrl { get; set; }
    }

    private sealed class GitHubReleaseAssetRef
    {
        public string TagName { get; init; } = string.Empty;
        public string HtmlUrl { get; init; } = string.Empty;
        public string AssetName { get; init; } = string.Empty;
        public string AssetDownloadUrl { get; init; } = string.Empty;
    }

    private readonly struct ParsedVersion
    {
        public int Major { get; init; }
        public int Minor { get; init; }
        public int Patch { get; init; }
        public string? PreRelease { get; init; }
    }
}
