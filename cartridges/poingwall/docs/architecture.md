# Poing Wall Architecture

## One ball without a game server

The server only relays snapshots, so the consoles have to agree among themselves
who owns the ball. Each one applies the same rule to the same list of player IDs,
so they reach the same answer without a vote or a message about it: the lowest
ID among the consoles that are ready moves the ball and publishes it.

## Ready and settling

A console that has just joined does not yet know what the room is doing. If it
had the lowest ID and took charge at once, it would start a second ball from its
own starting position, and the ball on every screen would jump.

So a console waits 700 ms after `welcome` before setting its ready flag. Only
ready consoles count in the election. During that wait it follows the current
leader, so when it does take over it continues from where the ball really is.

## Handing the ball over

| Event | What happens |
|---|---|
| A console joins with a higher ID | The wall grows at the right place in the order. The leader does not change. |
| A console joins with the lowest ID | It settles, follows, then takes over. The old leader sees a lower ready ID and starts following. |
| A follower leaves | The wall shrinks. If the ball was on the part that disappeared, it moves to the new end. |
| The leader leaves | The next-lowest ID becomes leader on its next update, continuing from its own copy of the ball. |
| A console reconnects | The firmware may give it a new ID, so it settles again like a new console. |

Two consoles can claim the ball for a moment during a handover, because each
learns about the other only through a snapshot. A follower obeys the claimant
with the lowest ID, so the picture stays consistent, and the moment ends with the
next snapshot.

## Smooth motion from 20 Hz snapshots

Snapshots arrive about 20 times a second, or fewer on a busy console, but a
console draws up to 30 frames a second. Following them directly would make the
ball stutter, so a follower runs the same physics as the leader between
snapshots and replaces its copy when a new one arrives. The physics uses fixed
33 ms steps, so a slow board and a fast computer predict the same motion.

## Drawing across a screen edge

Ball and floor come from [Poing](../../poing/README.md). Three changes make them
work on a wall:

- Every span is clipped, because the ball is often partly off this screen.
- The ball's checker pattern turns with its position on the wall, so both halves
  match when it straddles an edge.
- The floor has one vanishing point, at the middle of the whole wall, so its
  lines run on from one screen to the next.
