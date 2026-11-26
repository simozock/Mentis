# Signing the Windows release with Certum (cloud)

The alternative to SignPath (`installer/signpath/README.md`): a Certum "Open Source Code Signing in
the Cloud" certificate in the maintainer's name, whose key stays on Certum's servers (SimplySign).
No card, reader or token. Certum offers no unattended signing on CI, so the program and the
installer are signed on the maintainer's PC by `tools/sign-windows-certum.ps1`.

## One-time setup

1. Buy "Open Source Code Signing in the Cloud" in the Certum shop, activate it in the Certum panel
   and pass the identity check (documents: <https://support.certum.eu/en/code-signing-required-documents/>).
2. Install the SimplySign app on the phone and SimplySign Desktop on the PC (Certum's support site).
3. On the PC: `winget install JRSoftware.InnoSetup`, the Windows SDK signing tools (`signtool.exe`)
   and `gh`, logged in with `gh auth login`.
4. In the repository's Settings > Secrets and variables > Actions > Variables: `WINDOWS_SIGNING`
   = `certum`. Remove the variable to go back to the CI installer (signed by SignPath if its
   secret exists, otherwise unsigned).
5. The README's "Code signing policy" names the certificate's provider: update it to Certum.

## Every release

1. The tag builds everything except the Windows installer and creates a **draft** release: the
   app's update check does not see it yet.
2. Open SimplySign Desktop and connect with the code from the SimplySign app.
3. `.\tools\sign-windows-certum.ps1 -Version X.Y.Z` downloads the program built by the tag's run
   (artifact `windows-stage`, kept 30 days), signs `Mentis.exe` and `MentisMathText.dll`, builds the
   installer from the tag's `installer/mentis.iss`, signs it and its uninstaller, and adds it with
   the updated `SHA256SUMS` to the draft.
4. Check the draft, then publish it: on GitHub, or by running the script again with `-Publish`.

The installer built on the PC has no GitHub attestation (`gh attestation verify`): the other
packages keep theirs.
