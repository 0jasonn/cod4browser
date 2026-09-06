# Reporting security issues

This browser port is experimental. Imported archives, saves and browser
messages are untrusted inputs. A passing synthetic suite or dependency
inventory is not a security certification.

Use the repository's private vulnerability reporting option when it is
available. Otherwise open an issue requesting a private contact without
including exploit details. Do not attach proprietary game assets, private
saves, keys, credentials, or another user's files. Prefer a minimal synthetic
reproducer, exact source revision, browser/tool versions, and a description of
the affected boundary. No response-time or support guarantee is currently made.

The linked dependency inventory is `tools/web_dependencies.json`. It separates
browser artifacts from native test dependencies; findings must identify the
actually linked component and reachable parser/device path. Distribution
qualification checks pair exact source and site but do not replace a security
review or establish full campaign compatibility.
