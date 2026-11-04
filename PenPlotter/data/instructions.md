# FergoPlotter v1.1

- `G0` / `G1`: XY movement
- `G20` / `G21`: inches / millimeters
- `G90` / `G91`: absolute / relative positioning
- `G92`: set current XY coordinates
- `G28`: home
- `M3` / `M4`: pen down
- `M5` / `M9`: pen up
- `M2` / `M30`: end job and release control
- `Z > 0`: pen up; `Z = 0`: pen down

USB realtime commands: `?` status, `!` pause, `~` resume, Ctrl-X stop.

Pause finishes the current move before holding. STOP clears queued motion and raises the pen. USB/UGS and web motion control are mutually exclusive during a job.

