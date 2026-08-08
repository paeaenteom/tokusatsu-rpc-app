// ============================================================
//  TOKU RPC 설치 프로그램
//
//  · GitHub 최신 릴리스에서 앱을 받아 설치
//  · 설치된 브라우저를 찾아 목록으로 보여주고, 고른 브라우저에만 확장을 등록
//  · 관리자 권한이 필요 없다 (사용자 레지스트리 정책만 사용)
//
//  빌드: tools\build-installer.ps1 (csc 로 단일 exe 생성)
// ============================================================
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.IO.Compression;
using System.Net;
using System.Text.RegularExpressions;
using System.Threading;
using System.Windows.Forms;
using Microsoft.Win32;

namespace TokuRpcInstaller
{
    /// <summary>정책 기반으로 확장을 자동 설치할 수 있는 크로미움 계열 브라우저</summary>
    class Browser
    {
        public string Name;        // 표시 이름
        public string PolicyPath;  // HKCU\SOFTWARE\Policies\... 하위 경로
        public string[] ExePaths;  // 설치 여부 확인용 실행 파일 후보
        public string Process;     // 실행 중인지 확인할 프로세스 이름

        public bool Installed
        {
            get
            {
                foreach (var p in ExePaths)
                    if (File.Exists(Environment.ExpandEnvironmentVariables(p))) return true;
                return false;
            }
        }
        public bool Running { get { return Process != null && System.Diagnostics.Process.GetProcessesByName(Process).Length > 0; } }
    }

    static class Config
    {
        public const string Repo = "paeaenteom/tokusatsu-rpc-app";
        public const string ExtId = "dciaobllfdcegjcdmimclgglapnhggjm";
        public static string UpdateUrl = "https://github.com/" + Repo + "/releases/latest/download/update.xml";
        public static string AppDir = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), @"Programs\TOKU RPC");
        public static string AppExe = Path.Combine(AppDir, "TOKU RPC.exe");

        public static List<Browser> Browsers = new List<Browser>
        {
            new Browser { Name = "Google Chrome", PolicyPath = @"Google\Chrome", Process = "chrome",
                ExePaths = new[] { @"%ProgramFiles%\Google\Chrome\Application\chrome.exe",
                                   @"%ProgramFiles(x86)%\Google\Chrome\Application\chrome.exe",
                                   @"%LocalAppData%\Google\Chrome\Application\chrome.exe" } },
            new Browser { Name = "Microsoft Edge", PolicyPath = @"Microsoft\Edge", Process = "msedge",
                ExePaths = new[] { @"%ProgramFiles(x86)%\Microsoft\Edge\Application\msedge.exe",
                                   @"%ProgramFiles%\Microsoft\Edge\Application\msedge.exe" } },
            new Browser { Name = "네이버 웨일", PolicyPath = @"NAVER\Whale", Process = "whale",
                ExePaths = new[] { @"%ProgramFiles%\Naver\Naver Whale\Application\whale.exe",
                                   @"%ProgramFiles(x86)%\Naver\Naver Whale\Application\whale.exe",
                                   @"%LocalAppData%\Naver\Naver Whale\Application\whale.exe" } },
            new Browser { Name = "Brave", PolicyPath = @"BraveSoftware\Brave-Browser", Process = "brave",
                ExePaths = new[] { @"%ProgramFiles%\BraveSoftware\Brave-Browser\Application\brave.exe",
                                   @"%ProgramFiles(x86)%\BraveSoftware\Brave-Browser\Application\brave.exe",
                                   @"%LocalAppData%\BraveSoftware\Brave-Browser\Application\brave.exe" } },
            new Browser { Name = "Vivaldi", PolicyPath = @"Vivaldi", Process = "vivaldi",
                ExePaths = new[] { @"%LocalAppData%\Vivaldi\Application\vivaldi.exe",
                                   @"%ProgramFiles%\Vivaldi\Application\vivaldi.exe" } },
        };
    }

    public class MainForm : Form
    {
        CheckedListBox _browsers;
        CheckBox _cbShortcut, _cbRun;
        Button _btnInstall;
        ProgressBar _bar;
        TextBox _log;
        List<Browser> _found = new List<Browser>();

        static readonly Color Bg = Color.FromArgb(18, 20, 26);
        static readonly Color Fg = Color.FromArgb(232, 238, 247);
        static readonly Color Dim = Color.FromArgb(138, 147, 166);
        static readonly Color Accent = Color.FromArgb(255, 194, 51);

        public MainForm()
        {
            Text = "TOKU RPC 설치";
            ClientSize = new Size(520, 560);
            FormBorderStyle = FormBorderStyle.FixedSingle;
            MaximizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;
            BackColor = Bg;
            ForeColor = Fg;
            Font = new Font("Segoe UI", 9F);

            var title = new Label {
                Text = "TOKU RPC", Font = new Font("Segoe UI", 18F, FontStyle.Bold),
                ForeColor = Fg, Location = new Point(24, 20), AutoSize = true };
            var sub = new Label {
                Text = "특촬 시청 정보를 Discord에 표시합니다   ·   beta 0.1",
                ForeColor = Dim, Location = new Point(26, 58), AutoSize = true };

            var lblB = new Label { Text = "확장을 설치할 브라우저", ForeColor = Accent,
                Font = new Font("Segoe UI", 9.5F, FontStyle.Bold), Location = new Point(24, 96), AutoSize = true };

            _browsers = new CheckedListBox {
                Location = new Point(24, 120), Size = new Size(470, 120),
                BackColor = Color.FromArgb(28, 31, 40), ForeColor = Fg,
                BorderStyle = BorderStyle.FixedSingle, CheckOnClick = true, IntegralHeight = false };

            var lblO = new Label { Text = "옵션", ForeColor = Accent,
                Font = new Font("Segoe UI", 9.5F, FontStyle.Bold), Location = new Point(24, 252), AutoSize = true };
            _cbShortcut = new CheckBox { Text = "바탕화면에 바로가기 만들기", Checked = true,
                Location = new Point(26, 276), AutoSize = true, ForeColor = Fg };
            _cbRun = new CheckBox { Text = "설치 후 바로 실행", Checked = true,
                Location = new Point(26, 300), AutoSize = true, ForeColor = Fg };

            _btnInstall = new Button {
                Text = "설치", Location = new Point(24, 336), Size = new Size(470, 40),
                BackColor = Accent, ForeColor = Color.FromArgb(18, 20, 26),
                FlatStyle = FlatStyle.Flat, Font = new Font("Segoe UI", 11F, FontStyle.Bold) };
            _btnInstall.FlatAppearance.BorderSize = 0;
            _btnInstall.Click += OnInstall;

            _bar = new ProgressBar { Location = new Point(24, 386), Size = new Size(470, 6), Style = ProgressBarStyle.Continuous };

            _log = new TextBox {
                Location = new Point(24, 402), Size = new Size(470, 130), Multiline = true,
                ReadOnly = true, ScrollBars = ScrollBars.Vertical, BorderStyle = BorderStyle.FixedSingle,
                BackColor = Color.FromArgb(12, 14, 18), ForeColor = Dim, Font = new Font("Consolas", 8.5F) };

            Controls.AddRange(new Control[] { title, sub, lblB, _browsers, lblO, _cbShortcut, _cbRun, _btnInstall, _bar, _log });
            DetectBrowsers();
        }

        void DetectBrowsers()
        {
            foreach (var b in Config.Browsers)
            {
                if (!b.Installed) continue;
                _found.Add(b);
                _browsers.Items.Add(b.Name + (b.Running ? "   (실행 중 — 재시작 필요)" : ""), true);
            }
            if (_found.Count == 0)
            {
                _browsers.Items.Add("설치된 브라우저를 찾지 못했습니다", false);
                _browsers.Enabled = false;
                Log("크로미움 계열 브라우저가 없어 확장은 건너뜁니다.");
            }
            else Log("브라우저 " + _found.Count + "개 발견 — 원하는 것만 체크하세요.");
        }

        void Log(string m)
        {
            if (_log.InvokeRequired) { _log.BeginInvoke((Action)(() => Log(m))); return; }
            _log.AppendText(m + Environment.NewLine);
        }
        void Progress(int v)
        {
            if (_bar.InvokeRequired) { _bar.BeginInvoke((Action)(() => Progress(v))); return; }
            _bar.Value = Math.Max(0, Math.Min(100, v));
        }

        void OnInstall(object s, EventArgs e)
        {
            _btnInstall.Enabled = false;
            _btnInstall.Text = "설치 중...";
            var picked = new List<Browser>();
            for (int i = 0; i < _found.Count; i++)
                if (_browsers.GetItemChecked(i)) picked.Add(_found[i]);
            bool shortcut = _cbShortcut.Checked, run = _cbRun.Checked;

            var t = new Thread(() => Install(picked, shortcut, run));
            t.IsBackground = true;
            t.Start();
        }

        void Install(List<Browser> picked, bool shortcut, bool run)
        {
            try
            {
                ServicePointManager.SecurityProtocol = (SecurityProtocolType)3072; // TLS 1.2
                Log("");
                Log("[1/4] 최신 버전 확인");
                Progress(5);

                string api = "https://api.github.com/repos/" + Config.Repo + "/releases/latest";
                string json;
                using (var wc = new WebClient())
                {
                    wc.Headers.Add("User-Agent", "toku-rpc-installer");
                    json = wc.DownloadString(api);
                }
                var tag = Regex.Match(json, "\"tag_name\"\\s*:\\s*\"([^\"]+)\"");
                if (tag.Success) Log("      버전 " + tag.Groups[1].Value);

                var zipUrl = Regex.Match(json, "\"browser_download_url\"\\s*:\\s*\"([^\"]*win\\.zip)\"");
                if (!zipUrl.Success) throw new Exception("릴리스에서 앱 파일을 찾지 못했습니다.");

                // ── 앱 내려받기 ──
                Log("[2/4] 앱 내려받는 중...");
                string tmp = Path.Combine(Path.GetTempPath(), "tokurpc-" + Guid.NewGuid().ToString("N").Substring(0, 8));
                Directory.CreateDirectory(tmp);
                string zip = Path.Combine(tmp, "app.zip");

                using (var wc = new WebClient())
                {
                    wc.Headers.Add("User-Agent", "toku-rpc-installer");
                    var done = new ManualResetEvent(false);
                    Exception err = null;
                    wc.DownloadProgressChanged += (o, ev) => Progress(10 + ev.ProgressPercentage / 2);
                    wc.DownloadFileCompleted += (o, ev) => { err = ev.Error; done.Set(); };
                    wc.DownloadFileAsync(new Uri(zipUrl.Groups[1].Value), zip);
                    done.WaitOne();
                    if (err != null) throw err;
                }

                // ── 설치 ──
                Log("[3/4] 설치 중...");
                Progress(65);
                foreach (var p in Process.GetProcessesByName("TOKU RPC"))
                { try { p.Kill(); p.WaitForExit(5000); } catch { } }
                Thread.Sleep(800);

                if (Directory.Exists(Config.AppDir))
                { try { Directory.Delete(Config.AppDir, true); } catch { } }
                Directory.CreateDirectory(Config.AppDir);
                ZipFile.ExtractToDirectory(zip, Config.AppDir);
                Log("      " + Config.AppDir);
                Progress(80);

                if (shortcut) MakeShortcut();

                // ── 확장 등록 ──
                Log("[4/4] 브라우저 확장 등록");
                if (picked.Count == 0) Log("      선택된 브라우저 없음 — 건너뜀");
                foreach (var b in picked)
                {
                    try
                    {
                        string key = @"SOFTWARE\Policies\" + b.PolicyPath + @"\ExtensionSettings\" + Config.ExtId;
                        using (var k = Registry.CurrentUser.CreateSubKey(key))
                        {
                            k.SetValue("installation_mode", "normal_installed", RegistryValueKind.String);
                            k.SetValue("update_url", Config.UpdateUrl, RegistryValueKind.String);
                        }
                        Log("      OK  " + b.Name + (b.Running ? "  ← 재시작 필요" : ""));
                    }
                    catch (Exception ex) { Log("      실패  " + b.Name + " : " + ex.Message); }
                }
                Progress(95);

                try { Directory.Delete(tmp, true); } catch { }

                if (run && File.Exists(Config.AppExe))
                {
                    Process.Start(new ProcessStartInfo(Config.AppExe) { WorkingDirectory = Config.AppDir });
                    Log("      앱 실행됨 (트레이 아이콘 확인)");
                }
                Progress(100);

                Log("");
                Log("설치 완료");
                bool needRestart = picked.Exists(x => x.Running);
                if (needRestart) Log("※ 확장이 적용되려면 브라우저를 완전히 종료 후 다시 켜세요.");

                Done("설치 완료", needRestart
                    ? "설치가 끝났습니다.\n\n확장을 적용하려면 브라우저를 완전히 종료했다가\n다시 실행해 주세요. (트레이 아이콘까지 닫아야 합니다)"
                    : "설치가 끝났습니다.\n\n브라우저를 실행하면 확장이 자동으로 설치됩니다.");
            }
            catch (Exception ex)
            {
                Log("");
                Log("오류: " + ex.Message);
                Done("설치 실패", "설치 중 문제가 발생했습니다.\n\n" + ex.Message);
            }
        }

        void MakeShortcut()
        {
            try
            {
                string desktop = Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory);
                string lnk = Path.Combine(desktop, "TOKU RPC.lnk");
                // WScript.Shell 을 늦은 바인딩으로 사용 (COM 참조 없이)
                Type t = Type.GetTypeFromProgID("WScript.Shell");
                object sh = Activator.CreateInstance(t);
                object sc = t.InvokeMember("CreateShortcut", System.Reflection.BindingFlags.InvokeMethod, null, sh, new object[] { lnk });
                Type st = sc.GetType();
                st.InvokeMember("TargetPath", System.Reflection.BindingFlags.SetProperty, null, sc, new object[] { Config.AppExe });
                st.InvokeMember("WorkingDirectory", System.Reflection.BindingFlags.SetProperty, null, sc, new object[] { Config.AppDir });
                st.InvokeMember("Description", System.Reflection.BindingFlags.SetProperty, null, sc, new object[] { "TOKU RPC" });
                st.InvokeMember("Save", System.Reflection.BindingFlags.InvokeMethod, null, sc, null);
                Log("      바탕화면 바로가기 생성");
            }
            catch (Exception ex) { Log("      바로가기 실패: " + ex.Message); }
        }

        void Done(string title, string msg)
        {
            if (InvokeRequired) { BeginInvoke((Action)(() => Done(title, msg))); return; }
            _btnInstall.Text = "닫기";
            _btnInstall.Enabled = true;
            _btnInstall.Click -= OnInstall;
            _btnInstall.Click += (s, e) => Close();
            MessageBox.Show(this, msg, title, MessageBoxButtons.OK, MessageBoxIcon.Information);
        }

        [STAThread]
        static void Main(string[] args)
        {
            // /S : 조용히 설치 (감지된 모든 브라우저 대상)
            if (Array.IndexOf(args, "/S") >= 0)
            {
                var f = new MainForm();
                var all = new List<Browser>();
                foreach (var b in Config.Browsers) if (b.Installed) all.Add(b);
                f.Install(all, true, true);
                return;
            }
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new MainForm());
        }
    }
}
