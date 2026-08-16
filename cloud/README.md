# Pendant Sarvam relay

This Mumbai-ready Cloud Run relay uses the caller's Sarvam key only in memory
for the active voice socket. It does not write audio, transcripts, or keys to
disk or logs. Its only job is codec bridging: the pendant uses compact Opus
frames, while Sarvam's real-time speech services use PCM.

## Deploy

```sh
gcloud run deploy pendant-sarvam-relay \
  --source cloud --region asia-south1 --min-instances 1
```

The companion app only asks the user for a Sarvam API key. That key is
encrypted with Android Keystore and sent only during the active voice session.
The relay URL is a build-time service setting, not an API key or user-facing
credential.

For a public release, add Firebase App Check or an API gateway for abuse
control before distributing the APK.
