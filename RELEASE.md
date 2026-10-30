# Tweebbo for PlayStation Vita 1.4.4

Optional update: keep playing on your current version and update whenever convenient.
You do not need to install this immediately.

- Adds support for smaller future downloads using unchanged installed game files.
- Versions through 1.4.3 first need this complete bridge update.
- Shows small/full download sizes and preparation progress.
- Offers an explicit full download when a small update cannot be used.
- Preserves the previous file if replacement fails, along with saved characters
  and settings. Normal exit waits until active update work finishes.

The in-game updater saves a verified VPK in ux0:data/Tweebbo/tweebbo.vpk. Open VitaShell and install that file; your installed app is retained while downloading and preparing it.

Updating still needs storage space for the complete reconstructed VPK.
Host integrity/resume/atomic replacement tests and native builds pass. Physical
console installation, suspend/power loss and performance acceptance remain pending.

Full changelog: https://tweebbo.lol/changelog
