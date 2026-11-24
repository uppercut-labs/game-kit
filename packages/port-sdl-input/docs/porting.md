# Moving a port onto port-sdl-input

## 1. The layout

A layout is data: one `PsiSlot` per thing a player can bind.

```c
typedef struct {
    const char *name;       /* "X", "Start", "P AND K": what a person reads and the remap prompt asks for */
    const char *key;        /* "x", "start", "p_and_k": the settings key, "p1.x" */
    const char *old_key;    /* an earlier key still read when <key> is not set, or NULL */
    uint32_t mask;          /* the bit or bits it gives in the port's pad word; 0: the port sets it later */
    const char *defaults;   /* "key:Z, pad:south, joy:b1" */
} PsiSlot;
```

`psi_playstation_slots(out, masks)` fills sixteen of them for a PlayStation pad; the port passes the bit each button has
in its own word and then edits what differs (`slots[PSI_PS_X].defaults = "key:Z, pad:south, joy:b1";`). A port for another
console writes its own table: nothing else in the layer depends on the PlayStation. The strings and the table must
outlive the layer.

`PsiLayout` adds the four direction bits (SOCD and stick steering use them), the order a remap run asks in, and the two
bits a swap exchanges.

Default bindings are text, with these inputs: `key:<SDL scancode name>`, `pad:<south|east|west|north|l4|r4|l5|r5|SDL's
button name>`, `pad:<SDL axis name>+` or `-` (a trigger counts past a quarter, a stick past half way), `joy:b<n>`,
`joy:h<n>.up|right|down|left`, `joy:a<n>+` or `-`. `none` or NULL: no binding. Players after the first start with no keys:
the keyboard is player 1's.

## 2. The pad word

`PsiPad.buttons` is the port's word, active high, 32 bits. A port whose runtime wants the word active low, or in another
bit order, converts in the one place it hands the pad over. `lx ly rx ry` are the sticks, `0x80` centred, read only when the
player's `analog` is set.

## 3. Extras: macros that press the port's own buttons

A slot with `mask = 0` presses nothing until the port gives it bits. A game whose own button config can move what its
"punch" and "kick" buttons are calls `psi_set_slot_mask(in, player, slot, bits)` whenever it reads that config, once a frame
is enough: the bindings of the slot (a key, a stick click) then press those bits on the same frame as any button.

## 4. A swap in menus

Set the layout's `swap_a` and `swap_b` to the two bits and call `psi_set_swap(in, player, on)` each frame with whether the
game is asking that player to confirm or cancel. Which button confirms is the port's setting (its own settings key, its own
screen); the layer only exchanges the two bits, and a button held across a change of screen keeps the meaning it went down
with.

## 5. A remap screen

- One button: `psi_remap_begin(in, player, slot)`, then forward every SDL event to `psi_event` (it returns 1 for the one that
  was taken). `psi_remap_slot` is `-1` when the capture is over and `psi_remap_take_result` says how: 1 taken, 2 cancelled
  (Escape or `psi_remap_cancel`). A port's panel gives up after its own timeout with `psi_remap_cancel`.
- A run: `psi_remap_start(in, player)` asks for each slot in the layout's order, and `psi_remap_prompt` is the line to show
  ("Player 1: press X (now key:Z, pad:south; Esc keeps it)"), NULL when the run is over.
- The bindings go to the store as each is taken; the store's `save` runs when a single capture is taken and when a run
  ends. `psi_binding_text` gives a person's reading of a slot's bindings for a screen; `psi_reset_player` brings the layout's
  bindings back.

## 6. Prompts and families

`psi_family` is the family of the last device used. A port that draws button prompts asks `psi_prompt_letters` for the letter
on the button bound to each of up to eight slots (A, B, X, Y on an Xbox pad; none for a PlayStation pad's shapes, which the
game draws itself).

## 7. What the port still does

Hotkeys (a key that swaps players or opens a panel), the panel itself, reading `psi_raw` for a panel's navigation (no SOCD, no
swap, nothing blanked), turning `psi_keys_from_sdl` on in the real program, converting the pad word for its runtime, and
deciding when rumble starts and stops (`psi_rumble` lasts the milliseconds it is given and the port renews it).
