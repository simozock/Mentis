# Signing the Windows release with SignPath

The release workflow (`.github/workflows/release.yml`) signs the program and the installer through
the [SignPath Foundation](https://signpath.org) free program for open-source projects, when the
repository has the secret `SIGNPATH_API_TOKEN`. Without it the release is built unsigned.

## One-time setup

1. Publish a first (unsigned) release: the Foundation only accepts projects already released in
   the form to be signed.
2. Apply at <https://signpath.org/apply>. The README's "Code signing policy" section is the policy
   the Foundation asks for.
3. Turn on multi-factor authentication on GitHub and on SignPath for every team member.
4. On SignPath: add the "GitHub.com" trusted build system and link it to the project; create the
   artifact configurations `program` and `installer` from the two XML files here. They are drafts
   written from SignPath's reference and not yet validated: SignPath's editor checks them, and the
   Foundation may ask for restrictions (product name and version). Note the organization id, the
   project slug and the signing policy slug.
5. Install the SignPath GitHub App (<https://github.com/apps/signpath>) on this repository.
6. In the repository's Settings > Secrets and variables > Actions:
   - secret `SIGNPATH_API_TOKEN`: the API token of a SignPath CI user;
   - variables `SIGNPATH_ORGANIZATION_ID`, `SIGNPATH_PROJECT_SLUG`, `SIGNPATH_SIGNING_POLICY_SLUG`.

## Every release

The workflow sends two signing requests: `Mentis.exe` and `MentisMathText.dll` (before they go
into the installer), then the installer. Each waits up to an hour for an approver on SignPath.
Only the project's own files are signed, never the Qt libraries.
