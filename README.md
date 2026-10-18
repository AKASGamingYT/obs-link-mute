# OBS Link Mute

OBS 32.2 leaves audio monitoring on when a source is muted. This plugin turns the monitor off with the mute, and lets each source switch back to preview without restarting OBS.

It is built for 64-bit OBS Studio 32.2 on Windows.

## Install

Download [obs-link-mute-setup.exe](https://github.com/AKASGamingYT/obs-link-mute/releases/latest) and run it, then restart OBS.

To install by hand, unzip `obs-link-mute-windows-x64.zip` into:

`%ProgramData%\obs-studio\plugins`

The zip already contains the `obs-link-mute` folder. Restart OBS after extracting it.

## Modes

Open **Tools → Audio Monitoring**.

- **Monitor Off** — the source is not sent to your headphones.
- **Monitor Only (mute output)** — you hear it, and the stream does not. This is preview. OBS 32.2 still stores this mode, but no longer lists it in Advanced Audio Properties.
- **Monitoring Enabled** — headphones and the stream both hear it. Muting does not mute your headphones.
- **Monitoring Enabled (mute also mutes monitor)** — same as Monitoring Enabled, except mute silences your headphones too. Unmute turns monitoring back on.

Sources already set to Monitoring Enabled start with mute linked to the monitor. Monitor Only is left alone.

## Build

From this folder:

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
```

That builds the plugin, `dist\obs-link-mute-setup.exe`, and `dist\obs-link-mute-windows-x64.zip`. You need a 64-bit MinGW gcc on `PATH` (the script also checks a local w64devkit install).
