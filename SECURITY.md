# Security

Chan Sampler reads audio files, FL Studio presets (`.fst`), its own presets (`.cspreset`) and the state a host saves
with a project, so a crafted file that crashes it or does worse is a security bug.

Please report one privately with GitHub's **Report a vulnerability** button (Security tab > Advisories), not in a
public issue. Include the file or steps that trigger it and the Chan Sampler version, host and OS.

Fixes go into the next release; only the latest release is supported.

Every [release](https://github.com/MaymiPR/Chan-Sampler/releases) lists its files in `SHA256SUMS.txt`, and public
releases carry GitHub build provenance attestations (`gh attestation verify <file> --repo MaymiPR/Chan-Sampler`). The
release notes say whether the Windows and macOS files are code-signed; check an unsigned download against the
checksums before running it.
