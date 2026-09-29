# Security policy

Gity sits between you and your git credentials, so security reports are welcome and taken
seriously.

## Reporting a vulnerability

**Please do not open a public issue for a security problem.** Report it privately through
GitHub: on this repository's **Security** tab, choose **Report a vulnerability**. That opens a
private advisory only the maintainer can see.

Include what you can of:

* what the problem is and what an attacker could do with it;
* the steps, repository or remote URL shape that reproduces it;
* Gity's version (Settings ▸ About, or the commit you built), your OS, and `git --version`.

You will get an acknowledgement within a week. This is a personal project maintained in spare
time, so a fix may take longer than that; you will be kept informed, and credited in the release
notes unless you prefer not to be.

## Supported versions

Only the latest commit on `main` is supported. There are no maintained release branches yet.

## What Gity does with credentials — so you can judge a report

* **Gity stores no passwords or tokens.** Credentials are handed to git's own credential helper
  (`git credential approve`) and read back through it; they live wherever your helper keeps them
  — the system keyring, a store file, GitHub CLI. See ARCHITECTURE.md, ADR-010.
* Secrets are passed to git on **standard input**, never on a command line where other processes
  could read them, and are **never written to logs**, the command log, or Gity's settings. The
  command log redacts passwords embedded in remote URLs.
* The credential prompt helper, `gity-askpass`, is found **next to the `gity` executable**, never
  on `PATH`.
* Gity runs your `git` binary, so git's own hooks, filters and configuration run as they would in
  a terminal. A malicious repository can do to Gity exactly what it can do to `git` itself; opening
  untrusted repositories carries the same risks as running `git` in them.

Reports that break any of these promises are especially welcome.
