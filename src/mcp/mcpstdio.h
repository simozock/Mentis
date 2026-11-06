#pragma once

// FEAT-045: "Mentis --mcp" runs the MCP server over stdio instead of the window, so agents use the
// same executable as the user (user's decision 2026-11-02).
namespace McpStdio {

// Before the QApplication: no window system (offscreen platform, unless the caller chose one) and,
// on Windows, stdin/stdout in binary mode.
void prepareProcess();

// After the QApplication and its names: one JSON-RPC message per line on stdin and stdout until
// stdin closes. Anything else (Qt's warnings) goes to stderr: a stray line on stdout would break
// the client (MCP stdio transport).
int run();

}  // namespace McpStdio
