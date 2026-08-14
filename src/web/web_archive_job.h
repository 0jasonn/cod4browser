#pragma once

// Frame-driven proof that the browser filesystem seam can enumerate an IWD
// and read members without blocking the browser event loop.
void WebArchiveJob_Start();
void WebArchiveJob_Cancel();
void WebArchiveJob_Frame();

