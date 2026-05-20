# CoDriverVR Discord Release Publishing

This workflow posts CoDriverVR release announcements to a Discord channel using an incoming webhook. The package zip is not uploaded to Discord; the post links to wherever the zip is hosted.

The publish script is dry-run by default. It writes the exact message to `dist\discord-post-preview.md` and only posts when `-Post` is supplied.

## Create the Discord webhook

1. In Discord, open the target release channel.
2. Open channel settings.
3. Go to Integrations.
4. Create a new webhook.
5. Name it `CoDriverVR Releases`.
6. Copy the webhook URL.

Keep the webhook URL private. Anyone with it can post to that channel. If it leaks, delete or regenerate the webhook in Discord.

## Save the webhook locally

Save the webhook URL as a user environment variable so it is not committed to the repo:

```powershell
[Environment]::SetEnvironmentVariable("CODRIVERVR_DISCORD_WEBHOOK_URL", "PASTE_WEBHOOK_URL_HERE", "User")
```

Open a new PowerShell window after setting it, or set it for the current window too:

```powershell
$env:CODRIVERVR_DISCORD_WEBHOOK_URL = "PASTE_WEBHOOK_URL_HERE"
```

## Dry-run preview

Run this first. It does not post to Discord.

```powershell
.\scripts\Publish-CoDriverVR-Discord.ps1 -DownloadUrl "https://example.com/CoDriverVR-preview-20260519.zip"
```

Review:

```text
dist\discord-post-preview.md
```

## Post to Discord

Once the preview looks right, add `-Post`:

```powershell
.\scripts\Publish-CoDriverVR-Discord.ps1 -DownloadUrl "https://example.com/CoDriverVR-preview-20260519.zip" -Post
```

You can override the version label or zip path:

```powershell
.\scripts\Publish-CoDriverVR-Discord.ps1 `
  -Version "preview-20260519" `
  -ZipPath ".\dist\CoDriverVR-preview-20260519.zip" `
  -DownloadUrl "https://example.com/CoDriverVR-preview-20260519.zip" `
  -Post
```

## Test channel flow

Before posting to the public group:

1. Create a private Discord test channel.
2. Create a webhook for that test channel.
3. Temporarily set `CODRIVERVR_DISCORD_WEBHOOK_URL` to the test webhook.
4. Run the publish command with `-Post`.
5. Check the post on desktop and mobile Discord.
6. Switch the environment variable back to the public release channel webhook.

## Safety checks

- Do not commit webhook URLs.
- Do not paste webhook URLs into release notes or screenshots.
- The script disables Discord mentions with `allowed_mentions`, so accidental `@everyone` text will not ping the server.
- The script posts link-only release messages. Upload the zip to your chosen host first, then pass that URL with `-DownloadUrl`.
