<p align="center">
  <img src="assets/appicon/mentis-128.png" alt="Mentis" width="72">
</p>

<h1 align="center">Security policy</h1>

<p align="center"><a href="README.md">← Back to the README</a></p>

## Supported versions

Mentis is young: security fixes go into the latest released version and the `main` branch.

## Reporting a vulnerability

Please **do not open a public issue** for a security problem. Report it privately with GitHub's
[private vulnerability reporting](https://github.com/simozock/Mentis/security/advisories/new)
("Report a vulnerability" in the Security tab).

Describe the problem, how to reproduce it and its possible impact. You will get an answer as soon as
possible; the fix is released before the details are made public, and you will be credited if you
wish.

Areas that deserve particular care:

- opening `.mentis` files and importing Markdown received from others;
- attachments and links inside a map;
- the MCP server (`Mentis --mcp`): once turned on in Settings, it lets an AI agent read and change
  any map on the computer, and attach files and images to them; deleting a sheet or replacing a
  file needs your confirmation.
