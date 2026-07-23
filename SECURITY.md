# Security policy

## Supported version

Security fixes target the latest published `0.4.x` release. Development
snapshots and older `0.4.x` releases are not maintained separately; users are
expected to update to the latest patch release.

## Reporting a vulnerability

Use GitHub's private **Report a vulnerability** form in the repository's
Security tab. Include the affected command or API, configuration and path
conditions, reproduction steps, impact, and any proposed mitigation.

Do not publish exploit details, credentials, private project data, or a working
proof of concept in a public issue. If private reporting is unavailable, open a
minimal public issue asking the maintainers to enable a private contact channel
without including sensitive details.

Relevant reports include command execution, path traversal, unsafe deletion,
symlink attacks, artifact replacement outside the project, child-process or
signal handling flaws, memory corruption, and bundled dependency issues.

Solar does not automatically download tools or run installation scripts. A
report about an external EDA tool should also be sent to that tool's upstream
security contact when the flaw is not in Solar's orchestration boundary.
