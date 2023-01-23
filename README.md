# packRAT

Deep peristence management system

"They say to live off the land.  Well, we're in the landscaping business."


## Installation

Pretty simple: `make`


## Example usage

1. Build packrat: `make`
2. Launch the packrat console: `./packrat`
3. Start listening with the `l[isten]` command
4. Generate a beacon with the `b[eacon]` command
5. Select the platform you want the beacon to run on
6. Copy the command and paste it into a terminal on the target machine
7. The beacon should connect back to the packrat console
8. List all beacons with the `ls` command
9. Connect with a beacon with the `use <id>` command
10. Show status with the `status` command
11. Run a local binary on the remote machine with the `run <command>` command
12. List all pending sessions with the `ls` command
13. Connect with a session with the `use <id>` command
14. Interact with the session
15. Exit session console with the `exit` command
16. Exit beacon console with the `exit` command
17. Exit packrat console with the `exit` command


## TODO
* Encryption (both binary and resulting sessions)
* Option for remote binary execution
* Create run flag for direct, n-line output (don't generate session)
* Pre-staged scripts on beacon connect