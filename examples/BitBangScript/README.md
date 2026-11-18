# BitBangScript

Uses the module's script profile to generate precisely-timed pin edges on the
module itself, with no host involvement during execution.

The sketch loads a small token program into the 256-byte workspace:

```
0x01 0x04 200   SET_HIGH, DELAY_MS 200
0x02 0x04 200   SET_LOW,  DELAY_MS 200
0x05 0x00 5     LOOP_GOTO offset 0, 5 passes
```

It then runs `bitbangExecute(MICROSDEXP_BITBANG_PROFILE_SCRIPT)` and keeps the
bus quiet for the duration. Tokens are `SET_HIGH`, `SET_LOW`, `DELAY_US`,
`DELAY_MS`, and `LOOP_GOTO`; see the
[Bit-Bang Engine wiki](../../wiki/Bit-Bang-Engine.md) for the full token table
and timing rules.
