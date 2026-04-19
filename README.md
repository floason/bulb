# Bulb
Bulb is a chat communications protocol.

By default, the project compiles server and client libraries which provide the functions needed to establish communications via the Bulb protocol. If the `BULB_BUILD_CLI` CMake configuration parameter is specified, an additional binary is compiled, which provides a command-line interface for either the client (by default) or server libraries. This is the default method of communicating with other users, or for launching a new server, via the Bulb protocol.