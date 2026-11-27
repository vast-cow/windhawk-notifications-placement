# Windhawk Notification Placement with Mouse Avoidance

This Windhawk mod lets you choose the monitor and corner for Windows toast notifications. Every 300 ms, the mod checks the shortest straight-line distance from the cursor to the current notification window rectangle. If the cursor is on the same monitor and the distance is at or below the configured limit, the mod moves the notification to the farther of the two adjacent corners.

## Install

Requires Windhawk on x86-64 Windows 10 or Windows 11.

1. Open Windhawk and select **Create a New Mod**.
2. Replace the contents of `mod.wh.cpp` with [this fork’s source](notifications-placement.wh.cpp).
3. Select **Compile Mod**, then **Exit Editing Mode**. See the [Windhawk editing guide](https://github.com/ramensoftware/windhawk/wiki/Editing-mode-guidelines).

## Configure

In the mod’s **Settings** tab, choose a monitor and set the horizontal and vertical placement. Set **Monitor** to `0` to use the monitor under the cursor. Choosing **Center** on either axis disables mouse avoidance. The mod’s **Details** tab documents how to select a monitor by its interface name.

**Mouse avoidance distance** defaults to 48 device-independent pixels (DIP). The distance scales with the notification monitor’s DPI, so 48 DIP is 48 screen pixels at 100% scaling and 72 at 150%. A value of `0` triggers avoidance only when the cursor is inside or on the edge of the window rectangle. Negative values are treated as `0`.

## Credits and license

This fork adds mouse avoidance to m417z’s **Customize Windows notifications placement** mod. See the [original source in `ramensoftware/windhawk-mods`](https://github.com/ramensoftware/windhawk-mods/blob/main/mods/notifications-placement.wh.cpp). The mod source is licensed under the GNU General Public License v3.0.
