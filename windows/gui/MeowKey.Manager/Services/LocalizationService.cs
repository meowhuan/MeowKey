using System.Globalization;

namespace MeowKey.Manager.Services;

public sealed class LocalizationService
{
    private static readonly Lazy<LocalizationService> CurrentInstance = new(() => new LocalizationService());

    private readonly IReadOnlyDictionary<string, string> _strings;

    public static LocalizationService Current => CurrentInstance.Value;

    public string LanguageTag { get; }

    public bool IsChinese => string.Equals(LanguageTag, "zh-CN", StringComparison.OrdinalIgnoreCase);

    private LocalizationService()
    {
        var cultureName = CultureInfo.CurrentUICulture.Name;
        if (cultureName.StartsWith("zh", StringComparison.OrdinalIgnoreCase))
        {
            LanguageTag = "zh-CN";
            _strings = ZhCn;
            return;
        }

        if (cultureName.StartsWith("en", StringComparison.OrdinalIgnoreCase))
        {
            LanguageTag = "en-US";
            _strings = EnUs;
            return;
        }

        LanguageTag = "zh-CN";
        _strings = ZhCn;
    }

    public string this[string key] => Get(key);

    public string Get(string key)
    {
        if (_strings.TryGetValue(key, out var localized))
        {
            return localized;
        }

        if (ZhCn.TryGetValue(key, out var fallback))
        {
            return fallback;
        }

        return key;
    }

    private static readonly IReadOnlyDictionary<string, string> ZhCn = new Dictionary<string, string>
    {
        ["App.WindowTitle"] = "MeowKey 管理器",
        ["App.ProductName"] = "MeowKey 管理器",
        ["App.WindowSubtitle"] = "正式管理器壳层",
        ["App.ChannelLabel"] = "管理器优先",
        ["App.WindowsLabel"] = "Windows",
        ["App.LinuxLabel"] = "Linux",
        ["App.WindowsSurface"] = "WinUI 3",
        ["App.LinuxSurface"] = "Rust + egui/eframe",

        ["Nav.Overview"] = "概览",
        ["Nav.Devices"] = "设备",
        ["Nav.Credentials"] = "凭据",
        ["Nav.Security"] = "安全",
        ["Nav.Maintenance"] = "维护",
        ["Nav.Activity"] = "活动",

        ["Section.Overview.Title"] = "概览",
        ["Section.Overview.Subtitle"] = "按 Android-Cam-Bridge 的节奏重组后的正式管理器桌面壳层。",
        ["Section.Devices.Title"] = "设备",
        ["Section.Devices.Subtitle"] = "围绕设备盘点、底板角色和发布姿态组织，而不是继续堆调试面板。",
        ["Section.Credentials.Title"] = "凭据",
        ["Section.Credentials.Subtitle"] = "明确展示真正的管理能力缺口，而不是把未实现的能力包装成已完成。",
        ["Section.Security.Title"] = "安全",
        ["Section.Security.Subtitle"] = "把当前硬化边界、PIN 范围和 user presence 现实约束讲清楚。",
        ["Section.Maintenance.Title"] = "维护",
        ["Section.Maintenance.Subtitle"] = "构建、probe、发布和恢复仍然重要，但它们现在归到维护区而不是霸占整个 UI。",
        ["Section.Activity.Title"] = "活动",
        ["Section.Activity.Subtitle"] = "记录管理器壳层的决策、清理动作和后续工作项。",

        ["Summary.PrimaryDevice.Label"] = "主设备",
        ["Summary.PrimaryDevice.Value"] = "MeowKey Hardened RP2350",
        ["Summary.PrimaryDevice.Detail"] = "以发布基线设备为中心，而不是把 bring-up 控制台当产品。",
        ["Summary.ReleaseMode.Label"] = "桌面定位",
        ["Summary.ReleaseMode.Value"] = "管理器优先",
        ["Summary.ReleaseMode.Detail"] = "术语和分区围绕管理动作组织，不再围绕原始协议测试。",
        ["Summary.CredentialFlow.Label"] = "凭据流程",
        ["Summary.CredentialFlow.Value"] = "权限 API 待补齐",
        ["Summary.CredentialFlow.Detail"] = "单条删除和结构化目录仍然需要正式管理通道。",
        ["Summary.LinuxUi.Label"] = "Linux UI",
        ["Summary.LinuxUi.Value"] = "Rust + egui",
        ["Summary.LinuxUi.Detail"] = "先复用现有跨平台后端，避免再引入第二套未成型桌面栈。",

        ["Dashboard.Readiness.Now"] = "当前",
        ["Dashboard.Readiness.Next"] = "下一步",
        ["Dashboard.Readiness.Item1.Title"] = "Windows 壳层已经切到 WinUI 3",
        ["Dashboard.Readiness.Item1.Detail"] = "导航、标题栏、摘要卡和页面分区现在都按正式管理器来组织。",
        ["Dashboard.Readiness.Item2.Title"] = "WPF 原型不再是产品路径",
        ["Dashboard.Readiness.Item2.Detail"] = "旧占位应用已经从桌面主路径中移除。",
        ["Dashboard.Readiness.Item3.Title"] = "凭据管理仍需要权限范围 API",
        ["Dashboard.Readiness.Item3.Detail"] = "固件还能通过 Debug HID 列摘要和清空全部槽位，但还没有面向管理器的单项操作通道。",
        ["Dashboard.Readiness.Item4.Title"] = "Linux 先保持一套务实 UI 栈",
        ["Dashboard.Readiness.Item4.Detail"] = "在管理 API 稳定前，egui/eframe 是 Linux 侧迁移成本最低的呈现方式。",

        ["Dashboard.Platform.Windows"] = "Windows",
        ["Dashboard.Platform.Windows.Toolkit"] = "WinUI 3",
        ["Dashboard.Platform.Windows.Detail"] = "在 Windows 上使用带自定义标题栏、侧边导航和管理分区的原生桌面壳层。",
        ["Dashboard.Platform.Linux"] = "Linux",
        ["Dashboard.Platform.Linux.Toolkit"] = "Rust + egui/eframe",
        ["Dashboard.Platform.Linux.Detail"] = "复用现有 Rust 后端，把 Linux 壳层做成管理器样式，而不是再引入第二套原生控件依赖。",
        ["Dashboard.Platform.Shared"] = "统一方向",
        ["Dashboard.Platform.Shared.Toolkit"] = "一致的信息架构",
        ["Dashboard.Platform.Shared.Detail"] = "概览、设备、凭据、安全、维护和活动在各平台上应保持同一套概念地图。",

        ["Devices.Entry.Primary.Name"] = "主认证器",
        ["Devices.Entry.Primary.Role"] = "终端用户管理目标",
        ["Devices.Entry.Primary.Transport"] = "USB FIDO HID",
        ["Devices.Entry.Primary.State"] = "推荐",
        ["Devices.Entry.Primary.Detail"] = "这是管理器应该优先优化的发布基线设备形态，默认假设没有 Debug HID。",
        ["Devices.Entry.Bringup.Name"] = "联调构建",
        ["Devices.Entry.Bringup.Role"] = "开发恢复路径",
        ["Devices.Entry.Bringup.Transport"] = "USB FIDO HID + Debug HID",
        ["Devices.Entry.Bringup.State"] = "受限",
        ["Devices.Entry.Bringup.Detail"] = "它应该保留给维护和诊断，不该继续占据产品 UI 的中心。",
        ["Devices.Entry.Probe.Name"] = "Probe 固件",
        ["Devices.Entry.Probe.Role"] = "板卡识别流程",
        ["Devices.Entry.Probe.Transport"] = "USB 串口",
        ["Devices.Entry.Probe.State"] = "按需",
        ["Devices.Entry.Probe.Detail"] = "只在底板未知或需要生成新 preset 草案时进入 probe 路径。",
        ["Devices.Item.FirmwareLabel"] = "固件",
        ["Devices.Item.BoardLabel"] = "底板",
        ["Devices.Item.TransportLabel"] = "传输",
        ["Devices.Policy1.Title"] = "默认发布基线",
        ["Devices.Policy1.Value"] = "硬化构建",
        ["Devices.Policy1.Detail"] = "管理器文案和动作都应默认假设普通用户场景下没有 Debug HID。",
        ["Devices.Policy2.Title"] = "板卡识别路径",
        ["Devices.Policy2.Value"] = "需要时走 probe",
        ["Devices.Policy2.Detail"] = "未知 RP2350 底板应该被路由到 probe 流程，而不是走零散的手动调试步骤。",
        ["Devices.Policy3.Title"] = "支持术语",
        ["Devices.Policy3.Value"] = "统一管理器语言",
        ["Devices.Policy3.Detail"] = "界面应使用设备、凭据、安全和维护术语，而不是继续沿用 CTAP 测试话术。",

        ["Credentials.Capability1.Name"] = "凭据摘要列表",
        ["Credentials.Capability1.Status"] = "可通过 Debug HID 获取",
        ["Credentials.Capability1.Detail"] = "它对 bring-up 有用，但不能作为最终终端用户管理通道。",
        ["Credentials.Capability2.Name"] = "单条凭据删除",
        ["Credentials.Capability2.Status"] = "缺失",
        ["Credentials.Capability2.Detail"] = "如果没有这个能力，管理器就还不能像真正的 passkey 管理器那样工作。",
        ["Credentials.Capability3.Name"] = "结构化凭据目录",
        ["Credentials.Capability3.Status"] = "缺失",
        ["Credentials.Capability3.Detail"] = "友好的账户标签和稳定的单项动作仍然需要协议层补齐。",
        ["Credentials.Capability4.Name"] = "清空全部凭据",
        ["Credentials.Capability4.Status"] = "可通过 Debug HID 获取",
        ["Credentials.Capability4.Detail"] = "这个能力应该归到维护区，而不是作为默认管理模式。",

        ["Security.Policy1.Title"] = "用户在场基线",
        ["Security.Policy1.Value"] = "BOOTSEL 双击",
        ["Security.Policy1.Detail"] = "当前默认流程对 bring-up 足够，但还缺更清晰的本地确认体验。",
        ["Security.Policy2.Title"] = "Client PIN 覆盖面",
        ["Security.Policy2.Value"] = "仅旧式 getPinToken",
        ["Security.Policy2.Detail"] = "权限范围 token 还没做完，所以管理器不能过度承诺安全的账户级管理动作。",
        ["Security.Policy3.Title"] = "Signed boot",
        ["Security.Policy3.Value"] = "可选开启",
        ["Security.Policy3.Detail"] = "Secure boot 和 anti-rollback 已有构建块，但仍然需要用户显式完成 provisioning。",
        ["Security.Policy4.Title"] = "调试暴露",
        ["Security.Policy4.Value"] = "默认关闭",
        ["Security.Policy4.Detail"] = "管理器应该偏向 hardened 构建，把 Debug HID 限定在维护和实验室流程里。",

        ["Maintenance.Command.Check.Label"] = "固件检查",
        ["Maintenance.Command.Check.Detail"] = "统一跑固件、probe、Rust 桌面壳层和 WinUI 桌面壳层检查。",
        ["Maintenance.Command.Build.Label"] = "默认构建",
        ["Maintenance.Command.Build.Detail"] = "离线友好的开发构建。",
        ["Maintenance.Command.Hardened.Label"] = "硬化构建",
        ["Maintenance.Command.Hardened.Detail"] = "不带 Debug HID 的发布基线。",
        ["Maintenance.Command.Probe.Label"] = "Probe 流程",
        ["Maintenance.Command.Probe.Detail"] = "为未知底板生成 preset 草案。",

        ["Page.Dashboard.Title"] = "管理器基线",
        ["Page.Dashboard.Description1"] = "桌面端现在按真正管理器来组织：概览、设备、凭据、安全、维护和活动。原来的 bring-up 话术不再是壳层中心。",
        ["Page.Dashboard.Description2"] = "界面会如实反映当前固件边界，不会在协议尚未支持单条凭据管理时，把它包装成“已经可用”。",
        ["Page.Dashboard.Action.PreferHardened"] = "优先硬化基线",
        ["Page.Dashboard.Action.ConfirmLinux"] = "确认 Linux 方案",
        ["Page.Dashboard.ReadinessTitle"] = "准备度",
        ["Page.Dashboard.ReadinessDescription"] = "哪些部分已经符合管理器方向，哪些部分仍然需要协议层继续补齐。",
        ["Page.Dashboard.PlatformTitle"] = "平台方案",
        ["Page.Dashboard.PlatformDescription"] = "Windows 和 Linux 应该共享同一套管理地图，只是控件栈不同。",
        ["Page.Dashboard.BoundaryTitle"] = "边界说明",
        ["Page.Dashboard.BoundaryDescription"] = "壳层现在应该像管理器，但仍必须诚实展示 UI 打磨和协议成熟度之间的边界。",
        ["Page.Dashboard.NowTitle"] = "现在",
        ["Page.Dashboard.NowLine1"] = "Windows 壳层已经是 WinUI 3 管理器布局。",
        ["Page.Dashboard.NowLine2"] = "侧栏和页首都围绕管理分区组织。",
        ["Page.Dashboard.NowLine3"] = "WPF 已不再属于产品路径。",
        ["Page.Dashboard.NextTitle"] = "下一步",
        ["Page.Dashboard.NextLine1"] = "为凭据管理补上权限范围通道。",
        ["Page.Dashboard.NextLine2"] = "把 user presence 配置真正接到终端用户流程。",
        ["Page.Dashboard.NextLine3"] = "Linux 保持一套务实壳层，不再分叉成两套未完成桌面栈。",

        ["Page.Devices.Title"] = "设备盘点",
        ["Page.Devices.Description"] = "真正的管理器应该先讲清楚设备角色、构建姿态和板卡身份。这个页面用来区分哪些是正常使用面，哪些是维护面，哪些只用于板卡识别。",
        ["Page.Devices.Action.Refresh"] = "刷新盘点",
        ["Page.Devices.Action.Probe"] = "安排 Probe",
        ["Page.Devices.PoliciesTitle"] = "设备策略",

        ["Page.Credentials.Title"] = "凭据管理",
        ["Page.Credentials.Description"] = "这里必须最诚实。管理器可以先展示凭据管理的结构，但在固件具备权限范围管理通道前，不能把单项操作伪装成可安全使用。",
        ["Page.Credentials.Action.PlanDelete"] = "规划单条删除",
        ["Page.Credentials.Action.KeepSummary"] = "保留摘要路径",
        ["Page.Credentials.CapabilityTitle"] = "当前能力图",
        ["Page.Credentials.EmptyStateTitle"] = "空状态方向",
        ["Page.Credentials.EmptyStateDescription"] = "在协议真正具备正式管理面前，页面应该优先展示清晰的空状态，而不是伪造凭据条目。",
        ["Page.Credentials.EmptyStateNowTitle"] = "这个页面当前应该表达的内容",
        ["Page.Credentials.EmptyStateLine1"] = "单条凭据动作被刻意按下，直到固件能安全授权这些操作。",
        ["Page.Credentials.EmptyStateLine2"] = "批量破坏性操作应该归到维护区，而不是首次使用体验。",
        ["Page.Credentials.EmptyStateLine3"] = "友好的账户标签和元数据需要结构化目录，而不是调试转储。",

        ["Page.Security.Title"] = "安全基线",
        ["Page.Security.Description"] = "管理器需要描述真实安全姿态：哪些能力今天适合发布，哪些仍依赖 Debug HID，哪些只是可选硬化而不是默认保证。",
        ["Page.Security.Action.PromoteSecureReady"] = "强调 Secure-Ready",
        ["Page.Security.Action.KeepDebugLimited"] = "限制 Debug 暴露",
        ["Page.Security.RecommendationTitle"] = "推荐的产品语气",
        ["Page.Security.RecommendationDescription"] = "以 hardened 构建、user presence 默认值和显式 provisioning 说明为主。把 Debug HID、破坏性凭据擦除和协议 bring-up 面板压缩到维护语境下。",

        ["Page.Maintenance.Title"] = "维护与发布",
        ["Page.Maintenance.Description"] = "真正的管理器仍然需要运维深度。构建检查、probe、硬化发布准备和打包流程应该继续可见，但它们应该归在维护区，而不是霸占整个应用。",
        ["Page.Maintenance.Action.PrepareRelease"] = "准备发布检查",
        ["Page.Maintenance.Action.ProbeReminder"] = "记录 Probe 提醒",
        ["Page.Maintenance.ToneTitle"] = "发布检查语气",
        ["Page.Maintenance.ToneDescription"] = "发布流程应围绕 hardened 固件展开，未知底板先走 probe，同时桌面检查同时覆盖 Rust Linux 壳层和 WinUI Windows 壳层。",

        ["Page.Activity.Title"] = "管理器活动",
        ["Page.Activity.Description"] = "这个日志用来把壳层和真实决策绑在一起：UI 方向、清理步骤以及后续范围边界都应该在这里可见。",
        ["Page.Activity.Action.RecordSnapshot"] = "记录快照",
        ["Page.Activity.Action.PinLinux"] = "固定 Linux 说明",

        ["Activity.Category.shell"] = "壳层",
        ["Activity.Category.platform"] = "平台",
        ["Activity.Category.cleanup"] = "清理",
        ["Activity.Category.overview"] = "概览",
        ["Activity.Category.devices"] = "设备",
        ["Activity.Category.credentials"] = "凭据",
        ["Activity.Category.security"] = "安全",
        ["Activity.Category.maintenance"] = "维护",
        ["Activity.Category.activity"] = "活动",

        ["Activity.Startup.Shell"] = "已初始化 WinUI 3 管理器壳层，并把桌面布局对齐到 Android-Cam-Bridge 的节奏。",
        ["Activity.Startup.PlatformWin"] = "已将 Windows 主桌面体验固定为 WinUI 3。",
        ["Activity.Startup.PlatformLinux"] = "Linux 侧继续保留 Rust + egui/eframe，以复用现有跨平台后端。",
        ["Activity.Startup.Cleanup"] = "已安排移除旧 WPF 原型及其 CI / 脚本引用。",

        ["Action.Dashboard.PreferHardened"] = "已将 hardened 固件构建标记为默认管理器推荐基线。",
        ["Action.Dashboard.ConfirmLinux"] = "已确认 Linux 当前继续使用 Rust + egui/eframe 管理器壳层。",
        ["Action.Devices.Refresh"] = "已检查设备盘点卡片，确保壳层持续围绕管理角色组织。",
        ["Action.Devices.Probe"] = "已为下一块未知 RP2350 底板安排新的 probe-board 流程。",
        ["Action.Credentials.PlanDelete"] = "已把单条凭据删除固定为下一阶段必须补齐的管理 API 里程碑。",
        ["Action.Credentials.KeepSummary"] = "已将 Debug HID 摘要列表限定在维护语境中，而不是把它升级成普通管理视图。",
        ["Action.Security.PromoteSecureReady"] = "已把 secure-boot-ready hardened 构建提升为优先审查目标。",
        ["Action.Security.KeepDebugLimited"] = "已明确把 Debug HID 限定在维护和实验室流程中。",
        ["Action.Maintenance.PrepareRelease"] = "已准备新的桌面预检路径：Rust 管理器检查加 WinUI 管理器构建。",
        ["Action.Maintenance.ProbeReminder"] = "已补充提醒：未知底板先走 probe-board，而不是直接散做调试步骤。",
        ["Action.Activity.RecordSnapshot"] = "已记录当前管理器布局重构后的壳层快照。",
        ["Action.Activity.PinLinux"] = "已固定 Linux 方向：在权限范围管理 API 稳定前，继续使用 Rust + egui/eframe。"
    };

    private static readonly IReadOnlyDictionary<string, string> EnUs = new Dictionary<string, string>
    {
        ["App.WindowTitle"] = "MeowKey Manager",
        ["App.ProductName"] = "MeowKey Manager",
        ["App.WindowSubtitle"] = "Production Manager Shell",
        ["App.ChannelLabel"] = "Manager First",
        ["App.WindowsLabel"] = "Windows",
        ["App.LinuxLabel"] = "Linux",
        ["App.WindowsSurface"] = "WinUI 3",
        ["App.LinuxSurface"] = "Rust + egui/eframe",

        ["Nav.Overview"] = "Overview",
        ["Nav.Devices"] = "Devices",
        ["Nav.Credentials"] = "Credentials",
        ["Nav.Security"] = "Security",
        ["Nav.Maintenance"] = "Maintenance",
        ["Nav.Activity"] = "Activity",

        ["Section.Overview.Title"] = "Overview",
        ["Section.Overview.Subtitle"] = "A production-oriented desktop shell reorganized with the Android-Cam-Bridge layout rhythm.",
        ["Section.Devices.Title"] = "Devices",
        ["Section.Devices.Subtitle"] = "Organized around device inventory, board roles, and release posture instead of piling up debug panels.",
        ["Section.Credentials.Title"] = "Credentials",
        ["Section.Credentials.Subtitle"] = "Expose the real management gaps instead of dressing unfinished capabilities up as complete.",
        ["Section.Security.Title"] = "Security",
        ["Section.Security.Subtitle"] = "Make current hardening boundaries, PIN scope, and user-presence constraints explicit.",
        ["Section.Maintenance.Title"] = "Maintenance",
        ["Section.Maintenance.Subtitle"] = "Build, probe, release, and recovery still matter, but they now live under maintenance instead of dominating the whole UI.",
        ["Section.Activity.Title"] = "Activity",
        ["Section.Activity.Subtitle"] = "Track shell-level decisions, cleanup work, and follow-up items for the manager surface.",

        ["Summary.PrimaryDevice.Label"] = "Primary Device",
        ["Summary.PrimaryDevice.Value"] = "MeowKey Hardened RP2350",
        ["Summary.PrimaryDevice.Detail"] = "Center the product on the release baseline device, not on a bring-up console.",
        ["Summary.ReleaseMode.Label"] = "Desktop Posture",
        ["Summary.ReleaseMode.Value"] = "Manager First",
        ["Summary.ReleaseMode.Detail"] = "Terminology and sections are organized around management actions instead of raw protocol tests.",
        ["Summary.CredentialFlow.Label"] = "Credential Flow",
        ["Summary.CredentialFlow.Value"] = "Scoped API Pending",
        ["Summary.CredentialFlow.Detail"] = "Single-delete and a structured catalog still require a formal management channel.",
        ["Summary.LinuxUi.Label"] = "Linux UI",
        ["Summary.LinuxUi.Value"] = "Rust + egui",
        ["Summary.LinuxUi.Detail"] = "Reuse the existing cross-platform backend before introducing a second unfinished desktop stack.",

        ["Dashboard.Readiness.Now"] = "Now",
        ["Dashboard.Readiness.Next"] = "Next",
        ["Dashboard.Readiness.Item1.Title"] = "The Windows shell has moved to WinUI 3",
        ["Dashboard.Readiness.Item1.Detail"] = "Navigation, chrome, summary cards, and page sections now follow a real manager layout.",
        ["Dashboard.Readiness.Item2.Title"] = "The WPF prototype is no longer on the product path",
        ["Dashboard.Readiness.Item2.Detail"] = "The old placeholder app has been removed from the active desktop path.",
        ["Dashboard.Readiness.Item3.Title"] = "Credential management still needs a scoped API",
        ["Dashboard.Readiness.Item3.Detail"] = "The firmware can still list summaries and wipe all slots through Debug HID, but it still lacks per-item manager-safe flows.",
        ["Dashboard.Readiness.Item4.Title"] = "Linux should keep one pragmatic UI stack",
        ["Dashboard.Readiness.Item4.Detail"] = "egui/eframe is still the lowest-cost Linux surface until the management API is stable.",

        ["Dashboard.Platform.Windows"] = "Windows",
        ["Dashboard.Platform.Windows.Toolkit"] = "WinUI 3",
        ["Dashboard.Platform.Windows.Detail"] = "Use a native Windows shell with custom titlebar, sidebar navigation, and management-first sections.",
        ["Dashboard.Platform.Linux"] = "Linux",
        ["Dashboard.Platform.Linux.Toolkit"] = "Rust + egui/eframe",
        ["Dashboard.Platform.Linux.Detail"] = "Reuse the current Rust backend and shape the Linux shell like a manager instead of adopting a second native widget stack.",
        ["Dashboard.Platform.Shared"] = "Shared Direction",
        ["Dashboard.Platform.Shared.Toolkit"] = "Same Information Architecture",
        ["Dashboard.Platform.Shared.Detail"] = "Overview, devices, credentials, security, maintenance, and activity should stay conceptually aligned across platforms.",

        ["Devices.Entry.Primary.Name"] = "Primary Token",
        ["Devices.Entry.Primary.Role"] = "End-user management target",
        ["Devices.Entry.Primary.Transport"] = "USB FIDO HID",
        ["Devices.Entry.Primary.State"] = "Preferred",
        ["Devices.Entry.Primary.Detail"] = "This is the baseline device shape the manager should optimize for when Debug HID is absent.",
        ["Devices.Entry.Bringup.Name"] = "Bring-up Build",
        ["Devices.Entry.Bringup.Role"] = "Developer recovery path",
        ["Devices.Entry.Bringup.Transport"] = "USB FIDO HID + Debug HID",
        ["Devices.Entry.Bringup.State"] = "Limited",
        ["Devices.Entry.Bringup.Detail"] = "Keep it available for maintenance and diagnostics, but do not let it dominate the product UI.",
        ["Devices.Entry.Probe.Name"] = "Probe Firmware",
        ["Devices.Entry.Probe.Role"] = "Board discovery workflow",
        ["Devices.Entry.Probe.Transport"] = "USB serial",
        ["Devices.Entry.Probe.State"] = "Conditional",
        ["Devices.Entry.Probe.Detail"] = "Use probe only when the board identity is unknown or when a new preset draft is required.",
        ["Devices.Item.FirmwareLabel"] = "Firmware",
        ["Devices.Item.BoardLabel"] = "Board",
        ["Devices.Item.TransportLabel"] = "Transport",
        ["Devices.Policy1.Title"] = "Default shipping baseline",
        ["Devices.Policy1.Value"] = "Hardened build",
        ["Devices.Policy1.Detail"] = "Manager copy and actions should assume Debug HID is absent in normal end-user usage.",
        ["Devices.Policy2.Title"] = "Board discovery path",
        ["Devices.Policy2.Value"] = "Probe when needed",
        ["Devices.Policy2.Detail"] = "Unknown RP2350 boards should be routed through probe instead of ad-hoc manual debug steps.",
        ["Devices.Policy3.Title"] = "Support tone",
        ["Devices.Policy3.Value"] = "Manager terms only",
        ["Devices.Policy3.Detail"] = "Use device, credential, security, and maintenance language instead of raw CTAP testing vocabulary.",

        ["Credentials.Capability1.Name"] = "Credential summary listing",
        ["Credentials.Capability1.Status"] = "Available through Debug HID",
        ["Credentials.Capability1.Detail"] = "Useful for bring-up, but not acceptable as the final end-user management channel.",
        ["Credentials.Capability2.Name"] = "Single credential delete",
        ["Credentials.Capability2.Status"] = "Missing",
        ["Credentials.Capability2.Detail"] = "Without this, the app still cannot behave like a real passkey manager.",
        ["Credentials.Capability3.Name"] = "Structured credential catalog",
        ["Credentials.Capability3.Status"] = "Missing",
        ["Credentials.Capability3.Detail"] = "Friendly account labels and stable per-item actions still require protocol support.",
        ["Credentials.Capability4.Name"] = "Clear all credentials",
        ["Credentials.Capability4.Status"] = "Available through Debug HID",
        ["Credentials.Capability4.Detail"] = "Keep this in maintenance instead of presenting it as the default management path.",

        ["Security.Policy1.Title"] = "User-presence baseline",
        ["Security.Policy1.Value"] = "BOOTSEL double tap",
        ["Security.Policy1.Detail"] = "The current default is enough for bring-up, but it still needs a clearer local confirmation story.",
        ["Security.Policy2.Title"] = "Client PIN coverage",
        ["Security.Policy2.Value"] = "Legacy getPinToken only",
        ["Security.Policy2.Detail"] = "Permission-scoped tokens are unfinished, so the manager must not over-promise safe account-level admin actions.",
        ["Security.Policy3.Title"] = "Signed boot",
        ["Security.Policy3.Value"] = "Opt-in",
        ["Security.Policy3.Detail"] = "Secure boot and anti-rollback are present as building blocks, but they still require explicit user-driven provisioning.",
        ["Security.Policy4.Title"] = "Debug exposure",
        ["Security.Policy4.Value"] = "Keep off by default",
        ["Security.Policy4.Detail"] = "The manager should bias toward hardened builds and relegate Debug HID to maintenance and lab workflows.",

        ["Maintenance.Command.Check.Label"] = "Firmware check",
        ["Maintenance.Command.Check.Detail"] = "Run firmware, probe, Rust desktop, and WinUI desktop checks together.",
        ["Maintenance.Command.Build.Label"] = "Default build",
        ["Maintenance.Command.Build.Detail"] = "Offline-friendly developer build.",
        ["Maintenance.Command.Hardened.Label"] = "Hardened build",
        ["Maintenance.Command.Hardened.Detail"] = "Release-like baseline without Debug HID.",
        ["Maintenance.Command.Probe.Label"] = "Probe workflow",
        ["Maintenance.Command.Probe.Detail"] = "Generate preset drafts for unknown boards.",

        ["Page.Dashboard.Title"] = "Manager Baseline",
        ["Page.Dashboard.Description1"] = "The desktop app is now organized like a real manager: overview, devices, credentials, security, maintenance, and activity. Bring-up language is no longer the center of the shell.",
        ["Page.Dashboard.Description2"] = "The shell stays honest about current firmware boundaries, so the UI does not pretend that per-credential management already exists when the protocol still lacks it.",
        ["Page.Dashboard.Action.PreferHardened"] = "Prefer Hardened Baseline",
        ["Page.Dashboard.Action.ConfirmLinux"] = "Confirm Linux Surface",
        ["Page.Dashboard.ReadinessTitle"] = "Readiness Focus",
        ["Page.Dashboard.ReadinessDescription"] = "What is already aligned with the manager direction, and what still needs protocol work.",
        ["Page.Dashboard.PlatformTitle"] = "Platform Choices",
        ["Page.Dashboard.PlatformDescription"] = "Windows and Linux should share the same management map even when the widget toolkit differs.",
        ["Page.Dashboard.BoundaryTitle"] = "Manager Boundary",
        ["Page.Dashboard.BoundaryDescription"] = "The shell should feel like a manager now, while still exposing the real line between UI polish and protocol maturity.",
        ["Page.Dashboard.NowTitle"] = "Now",
        ["Page.Dashboard.NowLine1"] = "The Windows shell is already a WinUI 3 manager layout.",
        ["Page.Dashboard.NowLine2"] = "Sidebar and header are centered on management sections.",
        ["Page.Dashboard.NowLine3"] = "WPF is no longer part of the product path.",
        ["Page.Dashboard.NextTitle"] = "Next",
        ["Page.Dashboard.NextLine1"] = "Add a scoped credential-management channel.",
        ["Page.Dashboard.NextLine2"] = "Attach user-presence configuration to real end-user flows.",
        ["Page.Dashboard.NextLine3"] = "Keep Linux on one pragmatic shell instead of splitting into two unfinished desktop stacks.",

        ["Page.Devices.Title"] = "Device Inventory",
        ["Page.Devices.Description"] = "A real manager starts by clarifying device roles, build posture, and board identity. This page separates normal usage surfaces from maintenance surfaces and board-discovery-only flows.",
        ["Page.Devices.Action.Refresh"] = "Refresh Inventory",
        ["Page.Devices.Action.Probe"] = "Queue Probe Pass",
        ["Page.Devices.PoliciesTitle"] = "Device Policies",

        ["Page.Credentials.Title"] = "Credential Management",
        ["Page.Credentials.Description"] = "This page must be the most honest one. The shell can show the shape of credential management, but it must not expose per-item actions as safe until the firmware supports scoped authorization.",
        ["Page.Credentials.Action.PlanDelete"] = "Plan Single Delete",
        ["Page.Credentials.Action.KeepSummary"] = "Keep Summary Path",
        ["Page.Credentials.CapabilityTitle"] = "Current Capability Map",
        ["Page.Credentials.EmptyStateTitle"] = "Empty-State Direction",
        ["Page.Credentials.EmptyStateDescription"] = "Until the protocol grows a proper management surface, the page should prefer a clear empty state over fake credential rows.",
        ["Page.Credentials.EmptyStateNowTitle"] = "What the page should communicate now",
        ["Page.Credentials.EmptyStateLine1"] = "Per-credential actions are intentionally held back until the firmware can authorize them safely.",
        ["Page.Credentials.EmptyStateLine2"] = "Bulk destructive actions belong in maintenance, not in the first-run experience.",
        ["Page.Credentials.EmptyStateLine3"] = "Friendly account labels and metadata require a structured catalog, not a debug dump.",

        ["Page.Security.Title"] = "Security Baseline",
        ["Page.Security.Description"] = "The manager needs to describe the real security posture: what is production-friendly today, what still depends on Debug HID, and what remains opt-in hardening rather than an always-on guarantee.",
        ["Page.Security.Action.PromoteSecureReady"] = "Promote Secure-Ready",
        ["Page.Security.Action.KeepDebugLimited"] = "Keep Debug Limited",
        ["Page.Security.RecommendationTitle"] = "Recommended Product Tone",
        ["Page.Security.RecommendationDescription"] = "Lead with hardened builds, user-presence defaults, and explicit provisioning notes. Keep Debug HID, destructive credential wipes, and protocol bring-up panels inside maintenance language.",

        ["Page.Maintenance.Title"] = "Maintenance and Release",
        ["Page.Maintenance.Description"] = "A real manager still needs operational depth. Build checks, probe runs, hardened release prep, and packaging remain important, but they now live under maintenance instead of dominating the whole app.",
        ["Page.Maintenance.Action.PrepareRelease"] = "Prepare Release Check",
        ["Page.Maintenance.Action.ProbeReminder"] = "Log Probe Reminder",
        ["Page.Maintenance.ToneTitle"] = "Release Checklist Tone",
        ["Page.Maintenance.ToneDescription"] = "Keep the release flow centered on hardened firmware, route unknown boards through probe, and keep desktop checks covering both the Rust Linux shell and the WinUI Windows shell.",

        ["Page.Activity.Title"] = "Manager Activity",
        ["Page.Activity.Description"] = "This log keeps the shell connected to real decisions: UI direction, cleanup steps, and scope boundaries should stay visible here while the app evolves.",
        ["Page.Activity.Action.RecordSnapshot"] = "Record Snapshot",
        ["Page.Activity.Action.PinLinux"] = "Pin Linux Note",

        ["Activity.Category.shell"] = "Shell",
        ["Activity.Category.platform"] = "Platform",
        ["Activity.Category.cleanup"] = "Cleanup",
        ["Activity.Category.overview"] = "Overview",
        ["Activity.Category.devices"] = "Devices",
        ["Activity.Category.credentials"] = "Credentials",
        ["Activity.Category.security"] = "Security",
        ["Activity.Category.maintenance"] = "Maintenance",
        ["Activity.Category.activity"] = "Activity",

        ["Activity.Startup.Shell"] = "Initialized the WinUI 3 manager shell and aligned its desktop layout rhythm with Android-Cam-Bridge.",
        ["Activity.Startup.PlatformWin"] = "Locked the primary Windows desktop experience to WinUI 3.",
        ["Activity.Startup.PlatformLinux"] = "Kept Linux on Rust + egui/eframe to reuse the existing cross-platform backend.",
        ["Activity.Startup.Cleanup"] = "Scheduled removal of the legacy WPF prototype and its CI and script references.",

        ["Action.Dashboard.PreferHardened"] = "Marked the hardened firmware build as the default manager recommendation.",
        ["Action.Dashboard.ConfirmLinux"] = "Confirmed that Linux will stay on the Rust + egui/eframe manager shell for now.",
        ["Action.Devices.Refresh"] = "Reviewed device inventory cards to keep the shell centered on management roles.",
        ["Action.Devices.Probe"] = "Queued a fresh probe-board pass for the next unknown RP2350 board.",
        ["Action.Credentials.PlanDelete"] = "Pinned per-credential delete as the next required management API milestone.",
        ["Action.Credentials.KeepSummary"] = "Kept Debug HID summary listing scoped to maintenance instead of promoting it into a normal manager view.",
        ["Action.Security.PromoteSecureReady"] = "Promoted the secure-boot-ready hardened build as the preferred review target.",
        ["Action.Security.KeepDebugLimited"] = "Kept Debug HID explicitly limited to maintenance and lab workflows.",
        ["Action.Maintenance.PrepareRelease"] = "Prepared the combined desktop preflight: Rust manager check plus WinUI manager build.",
        ["Action.Maintenance.ProbeReminder"] = "Added a reminder to route unknown boards through probe-board instead of ad-hoc debug steps.",
        ["Action.Activity.RecordSnapshot"] = "Captured the current shell snapshot after the manager-layout refactor.",
        ["Action.Activity.PinLinux"] = "Pinned Linux to Rust + egui/eframe until a stable permission-scoped management API exists."
    };
}
