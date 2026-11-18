# Auto Play

Plays levels by itself.

It reads the real hitboxes of spikes, blocks, orbs, pads and portals straight from the game, and the real gamemode, speed, size and gravity of the player. Nothing is read from the screen, so "show hitboxes" is **not** needed.

- Cube / robot / ball / spider: simulates the next second and presses in the middle of the safe timing
- Ship / UFO / wave / swing: finds the gap ahead and steers through it
- Yellow / pink / red / blue / green / black orbs, pads, gravity, speed and size portals
- On / off button in the pause menu

Clears made by the bot are finished like a start-position run, so they are not saved as real clears.

Not handled: dash orbs, teleports, moving objects that move fast, dual, platformer levels.
