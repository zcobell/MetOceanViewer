-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (c) 2026 Zach Cobell
--
-- Lays out the DMG window (CPACK_DMG_DS_STORE_SETUP_SCRIPT): CPack mounts the
-- writable image and runs this with the volume name; Finder then records the
-- layout in the volume's .DS_Store. The window is the size of
-- dmg-background.png (660 x 400), whose arrow runs between the two icons.
-- CPack copies the background to .background/background.png.

on run argv
	set volumeName to item 1 of argv
	tell application "Finder"
		tell disk volumeName
			open
			set current view of container window to icon view
			set toolbar visible of container window to false
			set statusbar visible of container window to false
			-- {left, top, right, bottom}: a 660 x 400 content area
			set the bounds of container window to {200, 120, 860, 520}
			set viewOptions to the icon view options of container window
			set arrangement of viewOptions to not arranged
			set icon size of viewOptions to 128
			set text size of viewOptions to 13
			set background picture of viewOptions to file ".background:background.png"
			set position of item "MetOceanViewer.app" of container window to {170, 190}
			set position of item "Applications" of container window to {490, 190}
			close
			open
			update without registering applications
			delay 2
			close
		end tell
	end tell
end run
