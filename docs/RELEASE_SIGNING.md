# Apple signing for automated releases

Configure the following repository Actions secrets using your own Apple Developer account. Do not paste passwords, certificates or private keys into a chat, commit, issue, or PR. Certificate exports must contain their private keys. Both P12 exports currently use the same password.

| Secret | Value |
|---|---|
| PW_APP_CERTIFICATE_P12 | Base64-encoded Developer ID Application P12 export |
| PW_INSTALLER_CERTIFICATE_P12 | Base64-encoded Developer ID Installer P12 export |
| PW_CERTIFICATE_PASSWORD | P12 export password |
| PW_APP_SIGNING_IDENTITY | Complete Developer ID Application identity name |
| PW_INSTALLER_SIGNING_IDENTITY | Complete Developer ID Installer identity name |
| PW_APPLE_ID | Apple ID authorized for notarization |
| PW_APPLE_TEAM_ID | Apple Developer team ID |
| PW_APPLE_APP_PASSWORD | App-specific Apple ID password for notarytool |

After the release workflow is available on the repository's default branch, run Signed and notarized Mac release for the reviewed revision. It refuses missing credentials, compiles and tests, signs both bundles with hardened runtime, signs the installer, submits it to Apple's notary service, staples the ticket, verifies Gatekeeper acceptance, installs the package on the runner, and validates installed plug-ins. The signing keychain exists only on that ephemeral CI runner.

This workflow is implemented but cannot be considered verified until it completes with actual certificates and the resulting downloaded package passes a clean-Mac quarantine-preserving installation test. Signing does not replace the functional and real-vocal acceptance gates in RELEASE_ACCEPTANCE.md.
