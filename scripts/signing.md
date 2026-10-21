# Code signing (self-signed, dev stage)

Scraps installers are Authenticode-signed with a self-signed certificate during
the pre-revenue stage. This provides file-integrity verification; it does **not**
remove SmartScreen warnings (that requires an OV/EV certificate from a trusted
CA, planned for the commercial stage).

## One-time setup (done on the dev machine, actual setup used)

Created with OpenSSL (Git Bash ships one) because PowerShell credential
handling was blocked by local security policy:

```bash
mkdir -p "$USERPROFILE/signing" && cd "$USERPROFILE/signing"
MSYS_NO_PATHCONV=1 openssl req -x509 -newkey rsa:3072 \
  -keyout scraps-dev-key.pem -out scraps-dev-cert.pem -days 1825 -nodes \
  -subj "/CN=Scraps Dev Signing/O=Scraps Development" \
  -addext "extendedKeyUsage=codeSigning" -addext "keyUsage=digitalSignature"
MSYS_NO_PATHCONV=1 openssl pkcs12 -export \
  -out scraps-dev-signing.pfx -inkey scraps-dev-key.pem \
  -in scraps-dev-cert.pem -password pass:<pfx-password>
certutil -user -addstore Root scraps-dev-cert.pem   # trust for local verify
```

- PFX lives outside the repository (`%USERPROFILE%\signing\scraps-dev-signing.pfx`,
  password recorded in the local credential store, not here; valid to 2031).
- The cert is also imported into the current-user Root store so local
  `signtool verify` can build a chain. **This is optional and local-only**:
  signing works without it (`verify` just reports UnknownError), and it never
  affects end users -- a self-signed root does not remove SmartScreen or
  "unknown publisher" warnings for them regardless (that needs an OV/EV cert).
  Import via `certutil -addstore Root <pem>` (machine store, admin, silent) or
  `Import-Certificate` to avoid the GUI confirmation dialog.

## Signing

`scripts/release.ps1` signs the installer automatically when signtool and the
certificate are present (`signtool sign /a /fd SHA256 /tr <RFC3161 TSA> /td SHA256`).

Manual:

```powershell
signtool sign /a /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 dist\Scraps-x.y.z-setup.exe
```

## Verifying

```powershell
Get-AuthenticodeSignature dist\Scraps-x.y.z-setup.exe | Format-List
```

`UnknownError` / `NotTrusted` status is expected for self-signed certs on
machines that have not installed the public key.
