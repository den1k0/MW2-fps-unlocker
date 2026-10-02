#pragma once

#include <string>

// -----------------------------------------------------------------------------
// "Is there a newer build?" - the one thing the launcher asks the network.
//
// The source of truth is a single number in the repository, `version.txt` on the
// main branch, read over HTTPS from raw.githubusercontent.com. The number is the
// same build number the window shows beside the buttons, so there is nothing to
// keep in step by hand beyond bumping that file when a new EXE is published.
//
// Deliberately read-only: this reports, and the window offers to open the
// download page. Nothing here downloads or replaces the running EXE. A program
// that overwrites its own binary is the behaviour Windows Defender already
// flags this tool for, and there is no reason to make that worse.
// -----------------------------------------------------------------------------
namespace update {

// The published build number, or -1 when it could not be read. `error` is then
// a short phrase fit to put on a button - "could not reach GitHub", "no release
// published yet" - rather than a diagnostic.
//
// Blocks for as long as the request takes, so call it off the UI thread.
int LatestBuild(std::wstring& error);

// Where someone who needs the new EXE is sent.
const wchar_t* DownloadPage();

} // namespace update
