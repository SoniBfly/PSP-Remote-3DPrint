# Screens and controls

Five screens are registered in `src/core/main.c`: Models, Tune, Dash, Preview, Confirm. START exits from the main loop on every screen. R opens Tune from Models and from Dash only.

SELECT changes tabs on Tune (to Dash) and Dash (to Models). On Models, SELECT opens print confirmation.

| Screen | Cross | Circle | Square | Triangle | L | SELECT | D-pad |
|---|---|---|---|---|---|---|---|
| Models | — | Refresh SD and local models | Delete confirm | Pause or resume | Preview, if the file is on the PSP | Print confirm | Move the tile cursor |
| Preview | Upload to the printer | Back | Toggle supports when a second preview exists | Confirm upload and print | — | Back | Left/Right toggle supports when a second preview exists |
| Tune | Apply the selected row. On the actions row, set 200/60 and queue heat | Queue heater-off | Home confirm | `M105` | Queue speed and flow | Dash | Up/Down select a row. Left/Right change the value by 5 |
| Dash | `M105` and status | HTTP server on or off | Save `flsun.ini` | Tune | — | Models | Left/Right change the printer address octet |
| Confirm, print | Cancel | Cancel | Print | — | — | Cancel | — |
| Confirm, delete or home | Confirm | Cancel | — | — | — | Cancel | — |

Print confirmation stores the name and source when the dialog opens. Square is the only key that starts a print. Cross, Circle, and SELECT cancel. Delete and home use their own confirmation. Cross confirms those; Circle and SELECT cancel.

A command already in flight is rejected instead of started again. Leaving a screen still works. Heater-off is queued, not an emergency stop.
