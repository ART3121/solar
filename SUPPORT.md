# Solar support

## Questions and ideas

Use [GitHub Discussions](https://github.com/ART3121/solar/discussions) for
installation help, project questions, workflow ideas, and examples. Include
your Linux distribution, `solar --version`, relevant `solar doctor` output,
and the command you are trying to run.

Do not include private HDL, credentials, or complete proprietary logs. Reduce
the problem to a small public example when possible.

## Reproducible bugs

Use [GitHub Issues](https://github.com/ART3121/solar/issues) when Solar behaves
incorrectly and the problem can be reproduced. Start with the bug report form
and attach:

- the exact Solar command and exit code;
- the smallest safe `solar.toml` and source example;
- relevant stderr and the bounded log below `.solar/logs/`;
- tool versions from `solar doctor --all`;
- whether the same result occurs after `solar check` and `solar scan`.

Questions without a reproduction may be moved to Discussions. Feature requests
must preserve Solar's role as a small orchestrator around established EDA tools.

## Security

Do not report vulnerabilities in a public issue or Discussion. Follow
[SECURITY.md](SECURITY.md) and use GitHub private vulnerability reporting.
