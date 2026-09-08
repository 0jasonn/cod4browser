## What is this Folder?

COD4 includes a 3 Band EQ filter that's built within the Miles SDK as a ".flt" file (basically a DLL).

Originally, the milesEq.flt was hacked to work, but it crashes in the "coup" level, so I had the AI rebuild the plugin using the Miles SDK

This folder contains the source for the 3Band EQ Miles `.flt` plugin.
`build.bat` requires a locally installed Miles SDK; generated binaries are ignored.
The browser uses its OpenAL audio path and does not load this plugin.
