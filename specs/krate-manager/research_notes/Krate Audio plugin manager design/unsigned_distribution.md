# Distributing an unsigned, un-notarized desktop app and unsigned plugin installers on macOS, Windows and Linux (2026)

Research date: 2026-10-29. Scope: what users of Krate Audio's unsigned Inno Setup .exe, unsigned macOS .pkg and Linux tar.gz actually experience, what a self-updating unsigned Tauri 2 "plugin manager" would face, and every free or near-free mitigation, given the developer will never buy an Apple Developer membership or a Windows OV/EV certificate.

Conventions: every OS-behaviour claim carries the date of the source. "Inference" sections are my reasoning, not sourced fact. Where sources conflict, both are given.

---

## KQ1a. macOS Gatekeeper behaviour for unsigned / un-notarized apps on Sonoma 14, Sequoia 15 and Tahoe 26

### Takeaway
Sequoia (Sept 2024) removed the Control-click→Open bypass; the only supported path is launch → System Settings → Privacy & Security → "Open Anyway" (button lives ~1 hour). On Tahoe 26.6.x (tested Aug 2026) a *quarantined* ad-hoc-signed app is blocked by default and runs only after the user enables "Anywhere"; a *completely unsigned* arm64 binary never runs on Apple Silicon at all. An app that is **not quarantined** (installed by a pkg, or written by a non-quarantining process) launches without any Gatekeeper dialog even when only ad-hoc signed.

### Cited Findings
- Sequoia dropped the Control-click override for software that isn't correctly signed/notarized; replacement path is to launch once, then System Settings > Privacy & Security > "Open Anyway", then authenticate (2024 coverage of the Sequoia release) — [AppleInsider](https://appleinsider.com/inside/macos-sequoia/tips/whats-changed-in-runtime-protection-for-macos-sequoia); [iDownloadBlog](https://www.idownloadblog.com/apple-macos-sequoia-gatekeeper-change-install-unsigned-apps-mac/); [9to5Mac 2024-08-13](https://9to5mac.com/2024/08/13/security-bite-apple-finally-making-it-harder-to-override-gatekeeper-is-a-telling-move/)
- Apple's current Mac User Guide (page defaults to macOS 27 "Golden Gate") gives the steps as Apple menu > System Settings > Privacy & Security > Security section > "Open Anyway" > enter password; states the button "is available for about an hour after you try to open the app"; the app is then saved as an exception; the page no longer mentions Control-click, and warns that overriding is "the most common way that a Mac gets infected with malware" — [Apple Support: Open a Mac app from an unknown developer](https://support.apple.com/guide/mac-help/open-a-mac-app-from-an-unknown-developer-mh40616/mac)
- Sequoia 15.0 flow as observed by Howard Oakley (2024-10-01): first-launch warning halts launch; "open Privacy & Security settings, where you can click the button to Open Anyway"; a second warning ("don't open this unless you are certain it is from a trustworthy source") and a third authentication dialog follow; "this exception mechanism only applies to the app's first run" and "there's no way to unapprove that particular version" (comment 2024-10-02) — [Eclectic Light, Living with(out) notarization](https://eclecticlight.co/2024/10/01/living-without-notarization/)
- macOS 15.1 (Nov 2024): for an app with a *missing or broken* signature the dialog reads "The application "Finder" does not have permission to open "(null)"" and no "Open Anyway" button appears; removing quarantine did not help in that case. Ad-hoc-signed apps still got the "Open anyway" button (reader reports 2024-11-12) — [Eclectic Light comments](https://eclecticlight.co/2024/10/01/living-without-notarization/); [OSNews 2024-11-02](https://www.osnews.com/story/141055/bug-or-intentional-macos-15-1-completely-removes-ability-to-launch-unsigned-applications/) (OSNews readers disagreed about whether this was universal; one reported Open Anyway still worked on 15.1)
- Tahoe 26.6.1 test (published 2026-08-11, fresh VM): **ad-hoc signed + quarantined** → default: app offered to Trash; with "Anywhere" enabled: warning dialog, runs after confirmation. **Ad-hoc signed, not quarantined** → "launched normally, without any warning dialog". **Completely unsigned** → blocked in every condition, even with Anywhere ("completely unsigned code has never been allowed in Apple silicon Macs"). **Broken signature** → blocked in every condition. Adding the app to the Developer Tools list did not let a quarantined app run. Oakley's conclusion: "All executable code must now be signed" (at least ad hoc) — [Eclectic Light, How can you run code that hasn't been notarised?](https://eclecticlight.co/2026/08/11/how-can-you-run-code-that-hasnt-been-notarised/)
- Tahoe has had a run of Gatekeeper-bypass CVEs patched in 26.2, 26.4 (CVE-2026-20684, released 2026-03-24) and 26.6/26.7; third-party guides claim 26.5 (2026-05-11) tightened Gatekeeper further (CVE aggregators and a third-party blog; not verified against Apple's security notes) — [Wiz CVE-2026-20684](https://www.wiz.io/vulnerability-database/cve/cve-2026-20684); [macos-tahoe.com guide](https://macos-tahoe.com/blog/app-is-damaged-cant-be-opened-macos-tahoe-gatekeeper-quarantine-fix-2026/)
- A third-party guide claims the "Open Anyway" button appears only for ~1 hour; Apple's own page confirms the hour — [TechWiser](https://techwiser.com/how-to-install-and-open-unverified-apps-on-macos-sequoia/); [Apple Support](https://support.apple.com/guide/mac-help/open-a-mac-app-from-an-unknown-developer-mh40616/mac)

### Inferences
- Sonoma 14 users still have Control-click → Open (the sources date the removal to Sequoia); Krate's instructions must branch on macOS version.
- Oakley's Tahoe article never says whether, after the "Trash" dialog, the Open Anyway button still appears in System Settings for an ad-hoc signed app; the 2024 Sequoia reports say it does. Treat "Open Anyway still works for ad-hoc apps on Tahoe" as **probable but unverified**; a 10-minute test on a Tahoe VM settles it.
- The "exception applies to the first run of that version" statement implies every self-update of a *quarantined* app would need re-approval; the practical escape is to make the update not quarantined (see KQ1d/KQ1e).

### Gaps
- No Apple primary source states the Sequoia Control-click removal in release notes; coverage is press (AppleInsider, 9to5Mac). Apple's support page simply no longer lists it.
- Whether the 15.1 "(null)" no-button behaviour for unsigned apps persists in Tahoe for *ad-hoc* apps is unresolved (Oakley's test shows Trash-by-default; no System Settings check reported).

---

## KQ1b. `spctl --master-disable` status in 2026, and whether ad-hoc signing (`codesign -s -`) changes anything / is required on Apple Silicon

### Takeaway
`spctl --master-disable` no longer disables Gatekeeper; on Sequoia and Tahoe it merely reveals an "Anywhere" radio button in Privacy & Security that lets *signed* (including ad-hoc signed) but un-notarized apps run after a confirmation dialog, with Gatekeeper and XProtect still executing. Ad-hoc signing is mandatory on Apple Silicon: an arm64 binary with no signature is killed on launch; Apple's linker applies an ad-hoc signature automatically and any post-link modification requires re-signing with `codesign -s -`.

### Cited Findings
- Tahoe 26.6 (2026-08-06): `spctl` man page now says "operations that modify the rule database or the global state of the assessment subsystem will no longer be supported"; running `sudo spctl --master-disable` then choosing "Anywhere" "doesn't disable Gatekeeper or on-demand XProtect, but merely enables you to run signed apps that haven't been notarised"; `spctl --status` reported assessments disabled while XProtect stayed enabled, "not consistent with the man page" — [Eclectic Light, Can you disable Gatekeeper and XProtect?](https://eclecticlight.co/2026/08/06/can-you-disable-gatekeeper-and-xprotect/)
- Sequoia: the command "does not directly turn off Gatekeeper"; it reveals the "Anywhere" option which must then be selected in System Settings; `spctl --global-disable` is the documented alias — [iBoysoft](https://iboysoft.com/tips/allow-apps-to-run-sequoia.html); [troz.net 2024](https://troz.net/post/2024/sequoia_app_permissions/)
- Apple Silicon requires at least an ad-hoc signature; the kernel error is "attempt to execute completely unsigned code (must be at least ad-hoc signed)"; "a simple ad-hoc signature issued locally is sufficient, which includes signatures which are now generated automatically by the linker" — [Eclectic Light 2020-08-22](https://eclecticlight.co/2020/08/22/apple-silicon-macs-will-require-signed-code/); [Eclectic Light 2021-01-26](https://eclecticlight.co/2021/01/26/when-you-dont-have-permission-to-run-an-app-on-an-m1-mac/)
- LLVM: "if you use a custom workflow involving tools that modify a binary after linking (e.g. strip or install_name_tool) you might need to manually call codesign(1) to re-sign it … a simple ad-hoc signature is sufficient" — [LLVM Discourse](https://discourse.llvm.org/t/ad-hoc-code-signature-anomaly-on-arm64-macos/57628); symptom is `zsh: killed` with no crash report — [Apple Developer Forums](https://developer.apple.com/forums/thread/825362)
- Whole-bundle re-sign after post-processing: `codesign --force --deep -s - /path/to/program.app` — [Rust users forum](https://users.rust-lang.org/t/how-to-create-macos-bundles-for-apple-silicon-they-always-seem-broken/132678)
- Tauri's macOS signing doc: ad-hoc signing (identity `-`) is recommended on Apple Silicon "where code-signing is required for all apps from the Internet", but "Ad-hoc code signing does not prevent MacOS from requiring users to whitelist the installation" in Privacy & Security; with a free Apple ID "you will not be able to notarize your application and it will still show up as not verified" — [Tauri v2 macOS code signing](https://v2.tauri.app/distribute/sign/macos/)
- Oakley (2026-01-17): Apple has given no warnings "of any changes to requirements for developer certificates, notarisation, or ad hoc code signing to come in macOS 27 or beyond" — [Eclectic Light](https://eclecticlight.co/2026/01/17/whats-happening-with-code-signing-and-future-macos/)

### Inferences
- Ad-hoc signing is therefore not a mitigation of Gatekeeper, it is a precondition for the binary to execute at all; Krate's CI must ad-hoc sign every Mach-O (VST3/AU bundles, the Tauri app, any helper) **after** the last modification step (strip, lipo, rpath edits) or Apple Silicon users get a silent kill.
- Telling users to run `spctl --master-disable` is a worse instruction in 2026 than "Open Anyway": it requires Terminal plus a Settings change and still produces a dialog.

### Gaps
- No Apple document was found that states the Apple Silicon ad-hoc requirement in so many words; the evidence is the kernel message and Apple engineer forum answers reported second-hand.

---

## KQ1c. Unsigned .pkg: Installer.app vs `sudo installer -pkg`, and quarantined plug-ins loaded by a DAW

### Takeaway
A browser-downloaded unsigned pkg is quarantined and Installer.app refuses it ("from an unidentified developer"); the Open Anyway flow in System Settings works for pkgs, and `sudo installer -pkg … -target /` is the documented CLI route. Files that Installer.app writes do **not** carry the quarantine attribute, which is why DISTRHO ships Cardinal as an unsigned pkg; a plug-in that *is* quarantined is only loadable by a host if notarized, and "Open Anyway" does not exist for plug-in bundles.

### Cited Findings
- Cardinal wiki: "Note: The macOS builds are not signed or notarized." "macOS will not place binaries installed from a pkg under "quarantine"." "As long as you are able to run the installer, the plugins should run." Instruction: "Simply download the pkg file, right-click on it and select "install"." — [DISTRHO/Cardinal wiki: Install](https://github.com/DISTRHO/Cardinal/wiki/Install)
- Apple notarization doc (as surfaced in search results; my direct fetch returned only the page title): "in macOS 10.15 and later, apps can load quarantined plug-ins only if the plug-in is notarized" — [Apple Developer: Notarizing macOS software before distribution](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)
- JUCE forum (Dec 2024): a tester loading an unsigned VST3 sees "can't be opened because the developer can't be verified" with no Settings prompt; "'Open anyway' is only for applications"; "Open Anyway no longer works for dynamic libraries"; fixes discussed: `xattr -d -r com.apple.quarantine` on the .vst3, the xattred GUI tool, or ship a pkg installer (tester approves the installer in Security settings) — [JUCE forum: Mac "Open Anyway" not working for Unsigned Mac VST3](https://forum.juce.com/t/mac-open-anyway-not-working-for-unsigned-mac-vst3/64574)
- Unsigned, quarantined pkg: "macOS informs you that the package is from an unidentified developer, so can't be opened" — [Eclectic Light 2021-03-16](https://eclecticlight.co/2021/03/16/how-to-slip-unsigned-apps-past-gatekeeper-in-big-sur/); a Sequoia user's fix: `sudo xattr -r -d com.apple.quarantine ExifTool-12.92.pkg` — [ExifTool forum](https://exiftool.org/forum/index.php?msg=87112)
- CLI install of unsigned pkgs as used by an MDM vendor: `sudo installer -verboseR -pkg /tmp/test.pkg -target /` — [Hexnode](https://www.hexnode.com/mobile-device-management/help/script-to-install-unsigned-packages-on-mac.md)
- Homebrew cask policy: "An installer package that requires certificate verification to be disabled is not eligible." — [Homebrew Acceptable Casks](https://docs.brew.sh/Acceptable-Casks)
- KVR users' standard fix for plug-ins: `sudo xattr -rd com.apple.quarantine /Library/Audio/Plug-Ins/VST3/` (and Components/), then reboot; some note an additional flag may need removal — [KVR: MacOS xattr dilemma](https://www.kvraudio.com/forum/viewtopic.php?p=8948902); [KVR: Install Unsigned VST3 Plugins on MacOS Ventura](https://www.kvraudio.com/forum/viewtopic.php?p=8946047)

### Inferences
- Krate's existing unsigned-pkg route is the *least bad* no-certificate option on macOS: one Open Anyway (or one `sudo installer`) per download, and the installed VST3/AU are unquarantined so every DAW loads them. Shipping zips of bare .vst3/.component bundles would be strictly worse (quarantined bundles, no Open Anyway for plug-ins, users must run `xattr`).
- Whether `sudo installer -pkg` skips the Gatekeeper assessment entirely is widely reported but I found no authoritative source; treat as "works in practice, unverified".
- The pkg itself must still be ad-hoc-signed inside (the Mach-Os), not the pkg wrapper (productsign needs a Developer ID Installer cert, which is ruled out).

### Gaps
- No source states Installer.app's quarantine behaviour for payloads in Apple documentation; Cardinal's wiki and the unquarantined-launch result in Oakley's Tahoe test are the evidence.
- Not verified on Tahoe specifically whether "Open Anyway" is offered for a quarantined unsigned .pkg (the JUCE thread and iBoysoft describe Sequoia).

---

## KQ1d. Quarantine propagation: do files written by an already-approved app inherit `com.apple.quarantine`?

### Takeaway
Quarantine is opt-in per *writing process*, not inherited from the writer's own quarantine state: files get the xattr only if the writing app has `LSFileQuarantineEnabled=true` in its Info.plist, is listed in Apple's CoreTypes Exceptions.plist, or sets the xattr explicitly. `curl`, `tar`/`unzip` output and most non-browser apps do not quarantine. So a Tauri manager that downloads with a Rust HTTP client and extracts with its own code will write unquarantined plug-in bundles **unless** its Info.plist turns quarantine on.

### Cited Findings
- Oakley (2025-12-08, macOS 26): non-sandboxed apps' new files by default "don't have a quarantine xattr attached to them"; "The same should also apply to files created by sandboxed apps"; "When [LSFileQuarantineEnabled] is set to true, all files created by that app should bear the xattr"; CoreTypes Exceptions.plist can force it for listed apps (Transmission etc.); "there's no way the user can alter which apps add the quarantine xattr"; curl is "one notable exemption" and curl-downloaded files "don't gain the xattr when extracted from archives either" — [Eclectic Light, Who decides to quarantine files?](https://eclecticlight.co/2025/12/08/who-decides-to-quarantine-files/)
- Apple notes (via Unit 42) that SCP and curl do not mark downloads as quarantined and that unzip/tar output does not inherit the attribute — [Palo Alto Unit 42](https://unit42.paloaltonetworks.com/?p=137110)
- DTS engineer Quinn on an auto-updater whose unpacked new app got quarantined on Ventura 13.1+: called it "weird", because "quarantine is typically an opt-in system, so programs that download things have to explicitly set it"; the OP's apps were not sandboxed — [Apple Developer Forums thread 730314](https://developer.apple.com/forums/thread/730314)
- A sandboxed app that set the key to false still had downloads quarantined; adding `com.apple.security.files.user-selected.executable` changed the outcome (anecdotal) — [Apple Developer Forums thread 683551](https://developer.apple.com/forums/thread/683551)
- Chromium explicitly sets the quarantine attribute on downloads itself (source) — [Chromium quarantine_mac.mm](https://chromium.googlesource.com/chromium/src/+/4b7f2d40ed547f844c3b42b712f035e90357e1e5/content/common/quarantine/quarantine_mac.mm)

### Inferences
- The decisive design fact for the manager: if its Info.plist does **not** carry `LSFileQuarantineEnabled=true` and the app is not sandboxed, plug-ins it installs and its own replacement bundle will not be quarantined, so Gatekeeper never assesses them; they need only be ad-hoc signed. This makes the manager a *better* install path than a browser download of the pkg. This must be confirmed by inspecting the built Info.plist and running `xattr -l` on an installed bundle.
- The Ventura 13.1 forum case shows there are undocumented edge cases (possibly the archive-extraction API used); Krate should extract with its own tar code rather than shelling out to Archive Utility.

### Gaps
- I could not determine whether Tauri 2's bundler emits `LSFileQuarantineEnabled` in Info.plist by default (no source found). Check `src-tauri/target/release/bundle/macos/*.app/Contents/Info.plist`.
- No source covers URLSession-based downloads specifically; Tauri uses Rust `reqwest`, not URLSession, so the question is moot for the manager but open for any Swift helper.

---

## KQ1e. Does an unsigned app that replaces itself during self-update need re-approval?

### Takeaway
Approval is bound to the specific version (CDHash) and to the first run of a quarantined app, so a quarantined replacement would need re-approval; but if the replacement bundle is written without the quarantine xattr (the default for a non-quarantining writer), Gatekeeper does not assess it and an ad-hoc-signed replacement launches with no dialog (Tahoe 26.6.1 test). The risk is entirely in whether the update is quarantined, not in the signature.

### Cited Findings
- "this exception mechanism only applies to the app's first run"; "there's no way to unapprove that particular version"; changing an app's contents changes its CDHashes and macOS refuses to launch a modified app unless re-signed (Oakley, 2024-10-01/02) — [Eclectic Light](https://eclecticlight.co/2024/10/01/living-without-notarization/)
- Unquarantined ad-hoc-signed app on Tahoe 26.6.1 "launched normally, without any warning dialog" — [Eclectic Light 2026-08-11](https://eclecticlight.co/2026/08/11/how-can-you-run-code-that-hasnt-been-notarised/)
- Real-world failure when the updated bundle *was* quarantined: "App is damaged and can't be opened" after update (Apple forum, Ventura 13.1+) — [Apple Developer Forums 758537](https://developer.apple.com/forums/thread/758537); [thread 730314](https://developer.apple.com/forums/thread/730314)
- Separate failure class: an auto-updater leaving the top-level bundle with an invalid signature (inner framework valid) causes dyld abort at launch; quarantine ruled out in that report (Cursor/Electron, 2026) — [Cursor forum](https://forum.cursor.com/t/bug-report-cursor-3-6-31-crashes-on-launch-electron-framework-code-signature-invalidated-after-auto-update/162274)

### Inferences
- For the Krate manager: (1) build the .app.tar.gz with the whole bundle ad-hoc signed as the final CI step; (2) make sure the extraction does not add quarantine; (3) verify by `xattr -l` and `codesign --verify --deep --strict` on the replaced bundle in a test. If all three hold, self-update of the unsigned manager produces no Gatekeeper prompt at all on Sequoia/Tahoe.
- The first install (downloaded in a browser as a .dmg/.pkg/.zip) is the only guaranteed Gatekeeper encounter; shipping the manager as an unsigned **pkg** (like the plug-ins) keeps the user's one approval to the installer and leaves the installed .app unquarantined.

### Gaps
- Nobody has published a test of exactly this (ad-hoc Tauri app replacing itself on Tahoe); the conclusion is assembled from the quarantine rules plus Oakley's unquarantined-launch result.

---

## KQ1f. Homebrew cask distribution: does it sidestep the problem, and would Homebrew accept Krate's casks?

### Takeaway
No. Homebrew 5.0 (2025-11-12) deprecated `--no-quarantine`/`--quarantine` and declared un-codesigned casks deprecated, with all casks that fail Gatekeeper checks to be **disabled in September 2026** (i.e. already in effect at research date); plus the notability bar for a self-submitted GitHub project is 90 forks / 90 watchers / 225 stars. A personal tap remains possible but is unsupported and gives no Gatekeeper relief.

### Cited Findings
- Homebrew 5.0.0 (2025-11-12): `--no-quarantine`/`--quarantine` deprecated because "Homebrew does not wish to easily provide circumvention to macOS security features"; "Casks without codesigning are deprecated"; Homebrew "will disable all Homebrew/homebrew-cask casks that fail Gatekeeper checks in September 2026" — [brew.sh Homebrew 5.0.0](https://brew.sh/2025/11/12/homebrew-5.0.0/)
- A TomatoBar issue says the flag was already dropped in 4.7 (Homebrew/brew#20755) and `brew install --cask --no-quarantine` no longer works; workaround given is manual `xattr -r -d com.apple.quarantine` (conflicts with the 5.0 "deprecated" wording; version unverified) — [TomatoBar issue 102](https://github.com/ivoronin/TomatoBar/issues/102)
- Acceptable Casks: artefacts "must pass Homebrew's Gatekeeper checks and must not require System Integrity Protection or Gatekeeper to be disabled or bypassed"; casks "must use a download published by the developer" — [Homebrew Acceptable Casks](https://docs.brew.sh/Acceptable-Casks)
- Notability: GitHub project needs "at least 30 forks, 30 watchers or 75 stars"; for self-submission by the repo owner "at least 90 forks, 90 watchers or 225 stars"; repos under 30 days old ineligible — [Homebrew Package Acceptance Policy](https://docs.brew.sh/Package-Acceptance-Policy)
- Rejected casks can be hosted in your own tap, which Homebrew does not support — [homebrew-cask rejected_casks FAQ (historic)](https://github.com/Homebrew/homebrew-cask/blob/7500ef06a820d7a27ecd77cd35a831ea544b5022/doc/faq/rejected_casks.md)

### Inferences
- A `krate-audio/homebrew-tap` with pkg-based casks would still work mechanically (the pkg installer path is unquarantined output), but brew would run the pkg via `installer` — this is in effect the same as the CLI route in KQ1c and buys nothing beyond convenience for Terminal users.

### Gaps
- Whether Homebrew's September 2026 disablement has actually been executed and how `brew audit` treats pkg-only casks today was not verified (post-dates my sources).

---

## KQ2a. Windows: SmartScreen for unsigned downloads, how reputation builds, self-signed certs, Smart App Control; Tauri NSIS vs Inno Setup

### Takeaway
Microsoft's 2026 guidance: an unsigned file triggers "Windows protected your PC" with "More info → Run anyway"; a self-signed certificate behaves exactly like no signature; reputation is per file hash and resets with every release ("Unsigned files must build reputation anew with every update"); it takes "several weeks and hundreds of clean installs" with no submission mechanism for consumers; EV no longer bypasses SmartScreen. Windows 11 Smart App Control (enforcement mode) blocks unsigned, unknown binaries outright regardless of download origin. Installer technology (NSIS vs Inno) is irrelevant.

### Cited Findings
- Microsoft Learn (updated 2026-08-17): table — "No signature: Warning — 'Windows protected your PC'; User must choose 'Run anyway' before the app can run. Enterprise policy can prevent continuation entirely." "Self-signed Certificate: Warning — Same behavior as no signature." "When a file is not signed, SmartScreen reputation must build for each new version of your files, starting with zero reputation. Reputation cannot transfer from previous versions unless both were signed using the same publisher identity." "There is no exact threshold, but it can take several weeks and hundreds of clean installs from a wide audience." "There is no need (or mechanism) to manually submit a file for SmartScreen reputation review for consumer endpoints." "EV certificates no longer bypass SmartScreen." Store apps are never warned. — [SmartScreen reputation for Windows app developers](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation)
- Same page: "Smart App Control will block execution of unsigned files unless the file has a positive reputation. Smart App Control signature checks apply to all executable files, not just those downloaded from the Internet."
- Smart App Control overview (2026-09-30): "Malware, Potentially Unwanted Apps (PUA), and unknown, unsigned code are blocked by default"; in enforcement mode "Apps cannot be run unless they are recognized by Microsoft's app intelligence services, or they are signed with a trusted certificate"; evaluation mode auto-disables SAC for developers/power users — [Smart App Control](https://learn.microsoft.com/en-us/windows/apps/develop/smart-app-control/overview)
- Unsigned Tauri NSIS `-setup.exe` triggers the SmartScreen dialog; project told users it is "expected for unsigned builds"; "SmartScreen trust is tracked per file hash, so each new release starts untrusted" — [OpenScreen PR #285](https://github.com/getopenscreen/openscreen/pull/285/files)
- Tauri produces NSIS `-setup.exe`; Authenticode signing is configured via certificate thumbprint or a `signCommand` hook — [Tauri Windows installer docs](https://v2.tauri.app/fr/distribute/windows-installer/)
- Mark-of-the-Web is written by browsers and email clients as the `Zone.Identifier` ADS; Git and (pre-22) 7-Zip do not set it; 7-Zip 22+ propagates it into extracted files; MotW is lost on non-NTFS volumes; Edge now also writes a separate SmartScreen stream — [Outflank](https://outflank.nl/blog/2020/03/30/mark-of-the-web-from-a-red-teams-perspective/); [textslashplain](https://textslashplain.com/2016/04/04/downloads-and-the-mark-of-the-web); [SANS ISC 2025-03-03](https://isc.sans.edu/diary/Mark%20of%20the%20Web%3A%20Some%20Technical%20Details/31732)

### Inferences
- NSIS vs Inno makes no difference: SmartScreen keys on the file hash and publisher certificate, not on the installer engine. Krate's Inno installers and a Tauri NSIS installer are in the same boat; every release of every plug-in starts at zero.
- A Smart-App-Control-enforced Windows 11 PC (consumer clean installs where evaluation mode decided to enforce) cannot run the unsigned manager or unsigned plugin installers at all; the only user remedy is turning SAC off (which, per Microsoft's general SAC documentation, cannot be re-enabled without a reset — not re-verified here). Krate must document this as "unsupported unless you disable Smart App Control".
- Updater-launched installers: a Rust HTTP download does not write `Zone.Identifier`, so the SmartScreen *download* prompt very probably does not fire when the manager launches an installer it downloaded itself (inference from the MotW mechanics; no source tests this specific case). SAC still applies.

### Gaps
- No Microsoft source states that SmartScreen's "Windows protected your PC" check requires MotW; the Edge "separate SmartScreen stream" note suggests additional signals. Needs an empirical test: download an unsigned exe with `reqwest`, run via `ShellExecute`, observe.
- Figures on what fraction of Windows 11 consumer machines run SAC in enforcement mode were not researched.

---

## KQ2b. Windows: free / near-free signing options in 2026 (Azure Artifact Signing, SignPath Foundation, Certum)

### Takeaway
Azure Artifact Signing (renamed from Trusted Signing; $9.99/month; no token; CI-friendly) is open to **organizations** in the EU and ten other jurisdictions but to **individuals only in the US or Canada** (quickstart dated 2026-05-21). SignPath Foundation offers genuinely free Authenticode signing to OSI-licensed open-source projects with the Foundation as publisher, subject to a code-signing policy, MFA, CI-built artefacts and per-release manual approval. Certum's Open Source Code Signing set is ~€69 incl. smart card + reader and signs as "Open Source Developer, <your name>".

### Cited Findings
- Artifact Signing quickstart (ms.date 2026-05-21): "Public Trust certificates are available to organizations in the United States, Canada, the European Union, the United Kingdom, Australia, New Zealand, Japan, South Korea, Singapore, Switzerland, Norway, and Israel. Individual developers must be located in the United States or Canada." Individual validation pulls name/address from an Azure billing account of type Individual and runs through AU10TIX ID verification; organization validation takes "1 to 20 business days" and needs a business identifier, website and domain-matching email — [Quickstart: Set up Artifact Signing](https://learn.microsoft.com/en-us/azure/artifact-signing/quickstart)
- Price: "Starts at $9.99/month", "No hardware token required", integrates with GitHub Actions; "reputation accumulates over time based on download volume and behavior" — [SmartScreen reputation page](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation)
- Rename: "as of Jan 2026, Microsoft renamed the Azure 'Trusted Signing Account' to now be an 'Artifact Signing Account'" (JUCE forum) — [JUCE forum: Azure Code Signing for plugin developers](https://forum.juce.com/t/azure-code-signing-for-plugin-developers-guide/60391?page=6)
- Microsoft Q&A (June 2026, AI-generated answer, unverified): an Irish non-incorporated sole trader "does not meet the current Public Trust eligibility for individuals" — [Microsoft Q&A](https://learn.microsoft.com/en-us/answers/questions/5915697/can-an-irish-sole-trader-(non-incorporated)-qualif); individual onboarding outside US/Canada reported paused during preview (2025) — [Microsoft Q&A](https://learn.microsoft.com/en-us/answers/questions/2244424/how-to-initiate-identity-validation-for-an-individ)
- SignPath Foundation terms (draft code of conduct): OSI-approved license, no commercial dual licensing, "may not contain any proprietary, non open-source component" (System Libraries excepted); actively maintained; binaries "built from source in a verifiable way through an automated build"; roles author/reviewer/approver; MFA for all team members on SignPath and the repo; a "Code signing policy" section on home and download pages crediting SignPath Foundation; product name/version metadata; "SignPath Foundation is the publisher"; each release needs manual signing approval; acceptance at SignPath's discretion with no appeal; no fees — [signpath.org/terms](https://signpath.org/terms); [signpath.org](https://signpath.org/)
- Projects already using it publish the required policy pages, e.g. pandoc, Lyrion, Super Productivity, Ink/Stitch — [pandoc code signing policy](https://pandoc.org/code-signing-policy.html); [Lyrion](https://lyrion.org/signing-policy/); [Super Productivity](https://super-productivity.com/code-signing/); [Ink/Stitch](https://inkstitch.org/code-signing-policy)
- Certum "Open Source Code Signing - set": from €69.00 gross; includes cryptoCertum 3.7 card and a reader (ACS ACR39T-A1 or Omnikey 6121); certificate contains "natural person data prefixed with 'Open Source Developer' phrase"; card non-refundable — [Certum shop](https://shop.certum.eu/open-source-code-signing.html); third-party listings quote $49.99–$108/yr — [sslpoint](https://sslpoint.com/eu/code-signing); a GitHub thread claims the open-source cert "is NOT Microsoft certified" (old, unverified) — [daisy/ace-gui issue 15](https://github.com/daisy/ace-gui/issues/15)

### Inferences
- Krate Audio appears to be an individual EU developer (not verified); if so Artifact Signing's Public Trust is closed to them unless they register a legal entity (then $9.99/month + Azure subscription, EU-eligible). The historical "3-year-old organization" rule is **not** in the current quickstart; see Gaps.
- SignPath Foundation is the only path that is both zero-cost and eligible for an EU individual, and it is Windows-only. Because the certificate is the Foundation's and shared across many projects, signed Krate binaries would most likely inherit the Foundation certificate's existing SmartScreen reputation (inference; SignPath makes no such promise). Cost is process: public code-signing policy page, MFA, CI-only builds, per-release approval clicks, and the installer must be built in a CI SignPath can verify (GitHub Actions is the common case).
- Certum is ruled out by the user's constraint (it is a purchase), noted only for completeness.

### Gaps
- Current Artifact Signing *organization* eligibility details (minimum business age; the 2024 "3 years" rule) were not found in the 2026 quickstart; the FAQ page was not fetched.
- SignPath Foundation's approval timeline and whether it signs non-GitHub-Actions pipelines (e.g. self-hosted CMake builds) were not stated; also unconfirmed whether a monorepo shipping nine plugins + a manager counts as one "project".

---

## KQ2c. winget / Scoop / Chocolatey as channels for unsigned installers

### Takeaway
The Windows Package Manager repository policies (page updated 2026-08-30) contain **no code-signing requirement** for EXE installers; unsigned NSIS/Inno installers are submitted in practice (MSIX/AppX must be signed as a Windows platform rule). winget verifies the SHA-256 and runs the installer, which still shows SmartScreen for an unsigned EXE. I found no policy text for Scoop or Chocolatey on signing.

### Cited Findings
- winget repository policies (document v1.0 dated 2021-05-22, page updated 2026-08-30): sections 1.1–2.12 cover accurate metadata, security (no malware, no dynamic code download), testability, ISV release location ("Products from download websites will not be allowed"); no clause mentions digital signatures or Authenticode — [Windows Package Manager repository policies](https://learn.microsoft.com/en-us/windows/package-manager/package/windows-package-manager-policies)
- An open-source project documents submitting its unsigned NSIS exe to winget-pkgs as "a second, unsigned route"; "Windows runs an unsigned .exe after a SmartScreen prompt, but refuses outright to install an unsigned MSIX/AppX" — [OpenScreen PR #285](https://github.com/getopenscreen/openscreen/pull/285)
- Microsoft Q&A on distributing unsigned/self-signed MSIX via winget confirms MSIX needs a trusted signature — [Microsoft Q&A](https://learn.microsoft.com/en-us/answers/questions/878524/how-to-destributed-unsigned-or-self-signed-msix-us)
- winget stores a SHA256 per approved installer and checks the download against it — [TweakTown](https://www.tweaktown.com/guides/11540/i-install-and-update-most-of-my-apps-with-this-windows-command-now-and-i-stopped-downloading-sketchy-installers/index.html)
- Chocolatey community feed: human moderation of each package version since Oct 2014; vendors/established maintainers can be "Trusted"; nothing found requiring Authenticode — [Progress: Four Chocolatey Security Features](https://www.progress.com/amp/four-chocolatey-security-features/dWU5d09jQTFGZndVVnJod2xERzk5TENYb204PQ2)

### Inferences
- winget is a viable zero-cost channel for Krate's Inno installers (and the manager's NSIS installer) as long as each manifest points at the GitHub Release asset; it does not remove SmartScreen but it does remove the browser download step (so no MotW on the file winget fetches — inference; winget's own download path may or may not apply MotW, untested).

### Gaps
- Scoop: no documentation retrieved on signature policy for `extras`/main buckets.
- Chocolatey's current moderation rules on unsigned installers were not retrieved from docs.chocolatey.org.

---

## KQ3. Linux: AppImage vs Flatpak vs deb/rpm expectations; Flatpak sandbox and ~/.vst3

### Takeaway
No signing problem on Linux. Tauri's updater works on the AppImage form (the AppImage is the update bundle). A Flatpak'd manager would, by default, be unable to write `~/.vst3` or `/usr/lib/vst3` without `--filesystem` grants; `/usr` is a reserved path that `--filesystem` cannot grant at all. Flatpak DAWs discover plugins through the `org.freedesktop.LinuxAudio.Plugins` extension or need a `host-os` override, so a Flatpak manager writing to `~/.vst3` would still be invisible to Flatpak DAWs. Native deb/rpm/AppImage avoids all of this.

### Cited Findings
- Tauri updater: on Linux "the AppImage itself is the updater bundle, with a .sig file" — [Tauri updater plugin docs](https://v2.tauri.app/plugin/updater/)
- Flatpak sandbox: beyond runtime and app, only `~/.var/app/$FLATPAK_ID` and `$XDG_RUNTIME_DIR/app/$FLATPAK_ID` are reachable/writable; `--filesystem=home` grants home; `~/some/dir` grants an arbitrary home-relative path; reserved paths where `--filesystem` "will have no effect": `/app, /bin, /dev, /etc, /lib, /lib32, /lib64, /proc, /run/flatpak, /run/host, /sbin, /usr`; `host-os` mounts host `/usr` etc. at `/run/host` (read-only, by implication) — [Flatpak sandbox permissions](https://docs.flatpak.org/en/latest/sandbox-permissions.html)
- Flatpak Bitwig cannot see plugins in system VST3 paths "even if the plugins are installed in the user home directory"; workaround: `filesystems=host-os` override plus adding `/var/run/host/usr/lib/vst3` as a plugin location — [Fedora Discussion](https://discussion.fedoraproject.org/t/bitwig-flatpak-working-with-vst-clap-plugins/88280); [KVR Linux DAW thread](https://www.kvraudio.com/forum/viewtopic.php?p=8583421)
- Flatpak DAW manifests set `VST3_PATH` to `/app/extensions/Plugins/vst3` and declare the `org.freedesktop.LinuxAudio.Plugins` extension; plugins "must use same runtime as the plugin host" and "are installed via CLI and are invisible in Flathub frontend" — [Flathub discourse (Renoise)](https://discourse.flathub.org/t/could-we-add-renoise/4770); [Linux App Summit 2022 slides](https://conf.linuxappsummit.org/event/4/contributions/143/attachments/32/57/LAS%202022%20-%20Pro%20audio.pdf)
- Ardour users' consensus: Flatpak builds "generally do not allow accessing outside files"; for DAWs a native install (.deb or official binary) is preferable — [Ardour discourse](https://discourse.ardour.org/t/changing-ladspa-lv2-plugin-paths-flatpak/109209)

### Inferences
- Ship the manager as AppImage (+ deb/rpm from Tauri's bundler) and keep installing plug-ins to `~/.vst3`; do not Flatpak the manager. Users on Flatpak DAWs are already outside the `~/.vst3` convention and need either the `host-os` override or a Flatpak plugin extension, which is a separate packaging effort the manager cannot perform from inside a sandbox.
- A Flatpak manager would need `--filesystem=~/.vst3:create` (and `~/.clap`, `~/.lv2`) in its manifest; Flathub reviewers accept narrow home paths more readily than `home` (common practice; not sourced).

### Gaps
- Tauri's Flatpak support status and its docs page were not fetched.
- AppImage-vs-Flatpak user expectations for audio tools were not quantified; only the Ardour/KVR anecdotes.

---

## KQ4. Self-update of an unsigned app: Sparkle and the Tauri updater

### Takeaway
Sparkle's EdDSA archive signature and Tauri's updater signature are independent of Apple/Authenticode code signing and both function for ad-hoc/unsigned apps: Sparkle's maintainer states Sparkle's own test app is "EdDSA signing + adhoc-signed", and Tauri's updater signature "cannot be disabled" and requires no platform certificate. Neither signature satisfies Gatekeeper or SmartScreen; whether the macOS update launches cleanly depends solely on it not being quarantined (KQ1d/e).

### Cited Findings
- Sparkle maintainer (2023-07-02): "Sparkle's test app goes through the same process (EdDSA signing + adhoc-signed)." Sparkle "only supports rotation, but not removal" of EdDSA keys — [Sparkle discussion 2401](https://github.com/sparkle-project/Sparkle/discussions/2401)
- Sparkle docs: "Notarize and code sign the application via Apple's Developer ID program (if possible)"; EdDSA signing of the archive recommended; with Library Validation enabled "the system may not let your application load Sparkle" when ad-hoc signed; "Some code signing policies may otherwise not take effect and be testable when updating from/to non-notarized builds" — [Sparkle documentation](https://sparkle-project.org/documentation/)
- Sparkle rejects DSA-only updates since 2.3.2; CVE-2025-0509 (fixed 2.6.4) allowed replacing a signed update, bypassing (Ed)DSA checks — [Sparkle 2.6.3/2.6.4 advisory](https://github.com/sparkle-project/Sparkle/discussions/2551); [CVE-2025-0509](https://security-tracker.debian.org/tracker/CVE-2025-0509)
- Tauri updater: "needs a signature to verify that the update is from a trusted source … This cannot be disabled"; keys from `tauri signer generate`; private key via `TAURI_SIGNING_PRIVATE_KEY` (".env files do not work!"); artefacts: macOS `.app.tar.gz` + `.sig`, Linux AppImage + `.sig`, Windows installer + `.sig`; Windows `installMode` passive (default)/basicUi/quiet, "the application is automatically exited when the install step is executed"; no mention of Apple signing or notarization anywhere — [Tauri updater plugin](https://v2.tauri.app/plugin/updater/)
- Tauri updater 2.5.0 fixed macOS update installation when the user is not an admin — [Tauri updater v2.5.0 release](https://v2.tauri.app/release/updater/v2.5.0/)
- A Japanese write-up notes Tauri's updater uses minisign-format signatures, a separate mechanism from Apple code signing that "does not satisfy Gatekeeper" — [Classmethod (JP)](https://dev.classmethod.jp/articles/shuntaka-tauri-macos-codesign-notarization-updater-github-releases/)
- Third-party release notes: unsigned Tauri builds crashed at startup when the updater plugin was registered without its config; fix was to register it only when config exists — [relay-baton v1.3.1 notes](https://raw.githubusercontent.com/dgl1231/relay-baton/main/./release-notes/v1.3.1.md)

### Inferences
- Nothing in Tauri's updater depends on code signing on any OS; the remaining risks are (a) macOS: quarantine of the extracted bundle and an unsigned/broken-signature bundle after extraction (ad-hoc sign the bundle *before* tarring; tar preserves the signature since it is inside the Mach-O/`_CodeSignature`), (b) Windows: SAC-enforced machines, (c) Windows: if MotW is somehow applied, SmartScreen will prompt mid-update in passive mode.
- The minisign key becomes the single root of trust for the manager and every plugin it installs; it should live in a hardware-backed or at least offline store, because without Apple/Microsoft identity there is no second factor a user can verify.

### Gaps
- No Tauri GitHub issue specifically about unsigned macOS self-update was found; the Apple-forum cases are from other updaters.
- Not verified whether Tauri's macOS updater strips or preserves xattrs during extraction, or which tar implementation it uses.

---

## KQ5. How other free/open-source audio developers handle this; actual README/FAQ wording

### Takeaway
The two well-documented patterns are: (1) Cardinal/DISTRHO ships unsigned pkgs on macOS and unsigned exes on Windows and says so in one sentence each, relying on pkg-installed files being unquarantined; (2) Surge XT and Dexed appear (per a third-party signature tracker) to be Developer-ID signed and notarized under an individual maintainer's account. I found no primary wording from Airwindows, Vital or OB-Xd.

### Cited Findings
- Cardinal (DISTRHO), macOS: "Note: The macOS builds are not signed or notarized." / "macOS will not place binaries installed from a pkg under "quarantine"." / "As long as you are able to run the installer, the plugins should run." / "Simply download the pkg file, right-click on it and select "install"." Windows: "Note: The Windows builds are not signed, so Windows will say they are from an "untrusted developer"." No xattr or Settings instructions given. — [Cardinal wiki: Install](https://github.com/DISTRHO/Cardinal/wiki/Install); README mirrors the "untrusted developer" warning — [git.kx.studio Cardinal](https://git.kx.studio/DISTRHO/Cardinal)
- Surge XT 1.3.4: listed as signed "Developer ID Application: Paul Walker (547U7G7MZC)", notarized, hardened runtime (third-party tracker, "last verified May 2026"; security details partly paywalled) — [Desktop Insights: Surge XT](https://desktopinsights.com/apps/surge-xt); the Surge README itself says nothing about signing, only "download them from our website" — [surge README](https://github.com/surge-synthesizer/surge/blob/main/README.md)
- Dexed 1.0.1 (2026-05-24): listed as code-signed and notarized with hardened runtime (same third-party tracker) — [Desktop Insights: Dexed](https://desktopinsights.com/apps/dexed)
- JUCE forum (Dec 2024) recommends the Surge and ZL-Audio packaging scripts as starting points for a pkg installer with `distribution.xml` — [JUCE forum](https://forum.juce.com/t/mac-open-anyway-not-working-for-unsigned-mac-vst3/64574)
- A downstream Airwindows-based plugin's KVR listing: "not signed with an Apple developer certificate, so Gatekeeper may block it on first use" (not Airwindows itself) — [KVR: Air-G Taped Mini](https://www.kvraudio.com/product/air-g-taped-mini-by-air-g-audio)
- Example Windows wording from an unsigned open-source project: dialog is "expected for unsigned builds, not a sign of a malicious download"; user clicks "More info", then "Run anyway" — [OpenScreen PR #285](https://github.com/getopenscreen/openscreen/pull/285/files); another project maintains a dedicated "Running the unsigned Instrument on Windows (SmartScreen & antivirus)" page — [open-bart docs](https://open-bart.readthedocs.io/en/latest/standalone/SMARTSCREEN.html) (title only; content not fetched)
- Commercial-but-notarized vendor still needing an FAQ for AU/VST3 enabling on macOS, including `xattr` steps — [Syntheway](https://syntheway.net/fix-au-vst-vst3-macos.htm)
- Oakley's advice to unfunded developers: distribute source for local builds, "have another registered developer sign and notarize your app", provide checksums — [Eclectic Light 2024-10-01](https://eclecticlight.co/2024/10/01/living-without-notarization/)

### Inferences
- Cardinal's wording is the model for Krate's current state: it is honest, two sentences, and pairs with the pkg trick. Krate's docs should add the Sequoia/Tahoe-specific step ("if Installer refuses: System Settings → Privacy & Security → Open Anyway within an hour, or `sudo installer -pkg … -target /`") that Cardinal omits.
- Surge's and Dexed's notarization under an individual's Developer ID is the "volunteer's account" pattern; the certificate CN is that person's name, not the project's (consistent with Apple's rule that only the Account Holder creates Developer ID certs).

### Gaps
- Airwindows' current macOS signing status: no primary source found.
- Vital (commercial, Matt Tytel) and OB-Xd (discoDSP) were not researched beyond the failed combined search; both are commercial-adjacent and likely signed (unverified).
- The Desktop Insights data is a scraper; Surge's own release notes should be checked before quoting it as fact.

---

## KQ6. Is there any zero-cost path to notarization, and what are its risks?

### Takeaway
The only zero-cost paths are social: a contributor or friend who already pays for the Apple Developer Program signs and notarizes Krate builds under *their* Developer ID (the Surge/Dexed pattern), or a project is adopted by a foundation that holds an account. There is no Apple equivalent of SignPath Foundation; SignPath itself is Windows/Authenticode only in every source found. The risks fall on the account holder: the certificate names them, Apple notifies them of each notarization, and Apple can revoke the certificate or terminate the account over anything signed with it.

### Cited Findings
- "only the Account Holder that belongs to either the Apple Developer Program or the Apple Developer Enterprise Program is allowed to create Developer ID certificates" — [Apple Xcode Help](https://help.apple.com/xcode/mac/current/en.lproj/dev88332a81e.html)
- "You can only notarize apps that you sign with a Developer ID certificate" — [Apple: Notarizing macOS software before distribution](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution.md)
- "Any developer can notarize signed software, not just the owner of the Developer ID signing certificate", and the account holder receives email for notarizations under their account (2019 blog; dated) — [Bitsplitting: A Real Gatekeeper](https://bitsplitting.org/?p=1304)
- The team ID is embedded in the certificate CN, e.g. "Developer ID Application: Bob's Software (AB3456789Z)" — [msweet.org 2020-12-10](https://www.msweet.org/blog/2020-12-10-macos-notarization.html)
- Apple "reacts pretty quickly to invalidate a developer's code-signing certificate if they use it to sign apps that contain malware"; a forum poster declined to sign open-source binaries for fear of "risking termination of their account, which hosts other apps" — [Apple Developer Forums 835876](https://origin-devforums.apple.com/forums/thread/835876); [mjtsai 2019-04-22](https://mjtsai.com/blog/?p=25065)
- Tauri docs: a free Apple ID can sign but "you will not be able to notarize your application and it will still show up as not verified" — [Tauri macOS signing](https://v2.tauri.app/distribute/sign/macos/)
- SignPath Foundation terms reference MSI installers and DLLs and never mention macOS — [signpath.org/terms](https://signpath.org/terms)
- Oakley's explicit suggestion: "have another registered developer sign and notarize your app" — [Eclectic Light 2024-10-01](https://eclecticlight.co/2024/10/01/living-without-notarization/)
- Surge XT's notarization appears under an individual maintainer's Developer ID (third-party tracker) — [Desktop Insights](https://desktopinsights.com/apps/surge-xt)

### Inferences
- A borrowed account changes the trust story for users (the dialog names a stranger), creates a bus-factor on every release (the holder must run `notarytool` or share an App Store Connect API key with CI), and makes the holder liable under their Apple agreement for Krate's binaries. It also fails the moment they stop paying — though already-notarized builds keep working (Apple staples tickets; the JUCE thread notes "signed plugins keep working after it lapses").
- An Open Collective / foundation-held account (e.g. the project joining an umbrella like a software freedom nonprofit) is the structurally sound variant of the same thing, but no audio-specific example was found.
- Given the user's hard constraint, the honest conclusion is: on macOS there is no free technical escape from one Gatekeeper interaction per fresh download; the manager can reduce it to a single approval of the manager's own installer by writing everything else unquarantined.

### Gaps
- Apple Developer Program License Agreement text on sharing certificates/credentials with third parties was not retrieved; the risk statements above are from community sources.
- Apple's fee-waiver program (nonprofits, accredited educational institutions, government) was not researched; it is believed to be US/selected-countries-only and to exclude Developer ID distribution, but this is unverified.
- No evidence found of any foundation or sponsor offering notarization-as-a-service to unaffiliated open-source audio projects.
